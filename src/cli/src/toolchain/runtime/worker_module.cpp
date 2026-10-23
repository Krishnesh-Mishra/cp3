// =============================================================================
// src/cli/src/toolchain/worker_module.cpp - implements detail/worker_module.hpp.
//
// OWNERSHIP: cli module (src/cli/**). The worker-file linkage of spec 13 / D80:
//
//   * resolve each `new Worker("./x.cp3")` literal against the entry file's
//     folder, appending `.cp3` when the literal omits it (the same rule
//     `ProgramLoader::loadLocal` applies to imports);
//   * load the worker unit in a fresh module graph (its own imports, preludes
//     and folder-relative resolutions), run sema on it with its own
//     `SemaResult`, and read its `Thread<In, Out>` annotation - the channel's
//     single source of truth;
//   * generate the worker TU with `CodegenOptions::workerModule`, and hand the
//     entry's codegen one `WorkerSite` per call site plus the generated module
//     for the toolchain to compile and link.
//
// Two sites that name the same file share one generated TU (deduped by
// canonical path) but each keeps its own node id. Unit diagnostics - a missing
// file, a failed sema, a missing annotation, a codegen error - are collected in
// `ResolvedWorkers::diagnostics`; the caller renders them and stops when
// `failed` is set, so a broken worker never silently compiles without its TU.
// =============================================================================
#include "detail/core/worker_module.hpp"

#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/meta/module.hpp"

#include "detail/core/commands.hpp"
#include "detail/toolchain/pipeline.hpp"

namespace cp3::cli {

namespace {

namespace fs = std::filesystem;

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

fs::path resolveWorkerPath(const fs::path& folder, const std::string& literal) {
    std::error_code ec;
    fs::path candidate = folder / fs::path(literal);
    if (!fs::is_regular_file(candidate, ec) && !endsWith(literal, ".cp3")) {
        candidate += ".cp3";
    }
    if (!fs::is_regular_file(candidate, ec)) return {};
    return candidate;
}

std::string canonicalKey(const fs::path& path) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(path, ec);
    return (ec ? path : canonical).generic_string();
}

std::string sanitizeStem(std::string stem) {
    for (char& c : stem) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (!(std::isalnum(byte) != 0 || c == '_')) c = '_';
    }
    if (stem.empty()) stem = "worker";
    return stem;
}

std::string uniqueModuleName(const std::string& literal,
                             std::set<std::string>& used) {
    const std::string stem = sanitizeStem(fs::path(literal).stem().string());
    std::string name = stem;
    int suffix = 2;
    while (!used.insert(name).second) {
        name = stem + "_" + std::to_string(suffix);
        ++suffix;
    }
    return name;
}

// The enum a top-level statement declares, including `export enum`.
const EnumDeclaration* topLevelEnum(const Stmt& s) {
    if (s.kind == NodeKind::EnumDeclaration) {
        return &static_cast<const EnumDeclaration&>(s);
    }
    if (s.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(s);
        if (ed.declaration &&
            ed.declaration->kind == NodeKind::EnumDeclaration) {
            return static_cast<const EnumDeclaration*>(ed.declaration.get());
        }
    }
    return nullptr;
}

// The include path of the worker's generated header as seen from the entry's
// generated .cpp: both live under the build dir, the entry inside its module
// name's parent (empty for the common bare-stem case).
std::string workerHeaderPath(const std::string& entryModuleName,
                             const std::string& workerModuleName) {
    const fs::path from = fs::path(entryModuleName).parent_path();
    const fs::path hpp(workerModuleName + ".hpp");
    if (from.empty()) return hpp.generic_string();
    return hpp.lexically_relative(from).generic_string();
}

// One entry compile's worker resolution state: the dedupe maps, the modules
// already generated, and the diagnostics collected on the way.
struct WorkerResolver {
    SourceManager& sm;
    bool lineDirectives;
    CollectingDiagnosticSink& sink;
    const std::string& entryModuleName;
    const fs::path& entryFolder;
    ResolvedWorkers& out;
    std::unordered_map<std::string, std::string> nameByKey;
    std::unordered_map<std::string, WorkerChannel> channelByKey;
    std::set<std::string> usedNames;
    const CompileOptions& compile;

    std::string resolveSite(const WorkerPathSite& site);
    bool generateUnit(const fs::path& resolved, const WorkerPathSite& site,
                      const std::string& key);
};

bool WorkerResolver::generateUnit(const fs::path& resolved,
                                  const WorkerPathSite& site,
                                  const std::string& key) {
    SourceFile* file = nullptr;
    std::string error;
    if (!loadEntrySource(resolved.string(), sm, file, error)) {
        sink.error(diagcode::kModuleNotFound, site.span, error);
        return false;
    }
    std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *file, sink);
    CompilationUnit* unit = graph != nullptr ? graph->entry() : nullptr;
    if (unit == nullptr || !unit->ast) {
        sink.error(diagcode::kModuleNotFound, site.span,
                   "cannot load worker module '" + site.literal + "'");
        return false;
    }
    // The worker unit's own native bindings (an `import native` in the
    // worker): they are type-checked and lowered like the entry's.
    CompileOptions workerCompile = compile;
    workerCompile.nativeBindings = graph->nativeBindings();
    SemaResult sema;
    if (!analyzeProgram(*unit->ast, sema, sink, workerCompile)) return false;
    const std::optional<WorkerChannel> channel =
        resolveWorkerChannel(*unit->ast, sema);
    if (!channel) {
        sink.error(diagcode::kWorkerMissingAnnotation, site.span,
                   "the worker module '" + site.literal + "' must default-export "
                   "an arrow whose single parameter is `thread: Thread<In, Out>` "
                   "(the annotation is the single source of truth for its "
                   "channel)");
        return false;
    }
    const std::string moduleName = uniqueModuleName(site.literal, usedNames);
    CodegenOptions options;
    options.workerModule = true;
    options.typeSystem = compile.typeSystem;
    options.importedEnums = importedEnumsFor(*graph, *unit);
    options.nativeBindings = workerCompile.nativeBindings;
    GeneratedModule module =
        generateModule({*unit->ast, sema, moduleName, sink, lineDirectives, options});
    if (module.source.empty()) return false;  // codegen diagnostics explain
    nameByKey[key] = moduleName;
    channelByKey[key] = *channel;
    out.moduleNames.push_back(moduleName);
    out.sourceKeys.push_back(key);
    out.modules.push_back(std::move(module));
    return true;
}

std::string WorkerResolver::resolveSite(const WorkerPathSite& site) {
    const fs::path resolved = resolveWorkerPath(entryFolder, site.literal);
    if (resolved.empty()) {
        sink.error(diagcode::kModuleNotFound, site.span,
                   "cannot find worker module '" + site.literal + "'");
        return std::string();
    }
    const std::string key = canonicalKey(resolved);
    const auto known = nameByKey.find(key);
    if (known != nameByKey.end()) return known->second;
    if (!generateUnit(resolved, site, key)) return std::string();
    return nameByKey[key];
}

}  // namespace

std::vector<const EnumDeclaration*> importedEnumsFor(const ModuleGraph& graph,
                                                     const CompilationUnit& unit) {
    std::vector<const EnumDeclaration*> out;
    for (const CompilationUnit* other : graph.units()) {
        if (other == nullptr || other == &unit || !other->ast) continue;
        for (const auto& stmtPtr : other->ast->body) {
            if (!stmtPtr) continue;
            const EnumDeclaration* e = topLevelEnum(*stmtPtr);
            if (e != nullptr && !e->name.empty()) out.push_back(e);
        }
    }
    return out;
}

ResolvedWorkers resolveWorkerUnits(const std::vector<WorkerPathSite>& sites,
                                   const fs::path& entryFolder,
                                   const std::string& entryModuleName,
                                   SourceManager& sm, bool lineDirectives,
                                   const CompileOptions& compile) {
    ResolvedWorkers out;
    CollectingDiagnosticSink sink;
    WorkerResolver resolver{sm,          lineDirectives, sink,
                            entryModuleName, entryFolder,   out,
                            {},          {},             {}, compile};
    for (const WorkerPathSite& site : sites) {
        const std::string moduleName = resolver.resolveSite(site);
        if (moduleName.empty()) continue;
        const fs::path resolved = resolveWorkerPath(entryFolder, site.literal);
        const auto channel = resolver.channelByKey.find(canonicalKey(resolved));
        if (channel == resolver.channelByKey.end()) continue;  // unreachable
        WorkerSite workerSite;
        workerSite.nodeId = site.nodeId;
        workerSite.moduleName = moduleName;
        workerSite.headerPath = workerHeaderPath(entryModuleName, moduleName);
        workerSite.in = channel->second.in;
        workerSite.out = channel->second.out;
        workerSite.inlineSite = false;
        out.sites.push_back(std::move(workerSite));
    }
    out.diagnostics = sink.diagnostics();
    out.failed = sink.hasErrors();
    return out;
}

}  // namespace cp3::cli
