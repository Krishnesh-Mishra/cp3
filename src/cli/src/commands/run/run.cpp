// =============================================================================
// src/cli/src/commands/run.cpp - `cp3c build` / `cp3c run`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdBuildOrRun.
//
// Loads the module graph (prelude injection, import resolution), runs sema on
// the entry unit, threads the resulting SemaResult into codegen, then hands the
// generated module to the toolchain driver. `execute` selects `run` semantics.
// =============================================================================
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/codegen_program.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

#include "detail/cache/cache.hpp"
#include "detail/cache/package_cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/core/diagnostics.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/pipeline.hpp"
#include "detail/core/util.hpp"
#include "detail/core/worker_module.hpp"

namespace cp3::cli {

namespace {

// Everything the frontend + codegen steps need beyond the source itself.
struct BuildContext {
    CliOptions* opt = nullptr;
    const RenderOptions* render = nullptr;
    const std::string* moduleName = nullptr;  // e.g. "src/index"
    bool execute = false;                     // `cp3c run` was requested
};

// The load+sema half of the pipeline. exitCode is kExitSuccess when `graph`
// carries an analyzed entry unit to hand to codegen. `program` selects the
// whole-program codegen path (a resolved local import exists); in that mode
// every emittable unit is checked and its SemaResult kept in `unitSemas` -
// each unit's parser restarts nodeIds (D6), so the tables are per unit and
// cannot be merged. `worker` is the resolved canonical worker import of spec
// 13, if any; the worker unit lowers through its own TU, not this program.
struct FrontendResult {
    std::unique_ptr<ModuleGraph> graph;
    SemaResult sema;
    std::unordered_map<std::uint32_t, SemaResult> unitSemas;
    bool program = false;
    int exitCode = kExitSuccess;
};

FrontendResult runFrontend(SourceManager& sm, SourceFile& file,
                          const BuildContext& ctx) {
    FrontendResult result;
    CollectingDiagnosticSink feSink;
    result.graph = loadModuleGraph(sm, file, feSink);
    // The native bindings the program imports: sema type-checks the calls
    // against them, codegen lowers them.
    ctx.opt->compile.nativeBindings = result.graph->nativeBindings();
    CompilationUnit* entry = result.graph->entry();
    if (entry != nullptr && entry->ast) {
        result.program = hasProgramModules(*result.graph);
        analyzeProgramGraph(*result.graph, result.sema, result.unitSemas,
                            feSink, ctx.opt->compile);
    }
    renderDiagnostics(feSink.diagnostics(), *ctx.render, stderr);
    if (feSink.hasErrors()) {
        // Every frontend error is a real compile error.
        result.exitCode = kExitCompileError;
    } else if (entry == nullptr || !entry->ast) {
        // Unreachable invariant: the loader always yields a parsed entry unit.
        // Report a real error rather than continuing into codegen with no AST.
        std::fprintf(stderr,
                     "cp3c: internal error: the module loader produced no "
                     "entry unit\n");
        result.exitCode = kExitCompileError;
    }
    return result;
}

// Shared tail of both codegen paths: render the codegen diagnostics and turn
// them into an exit code.
int codegenOutcome(CollectingDiagnosticSink& cgSink, const GeneratedModule& mod,
                   const BuildContext& ctx) {
    renderDiagnostics(cgSink.diagnostics(), *ctx.render, stderr);
    if (cgSink.hasErrors()) {
        return kExitCompileError;
    }
    if (mod.source.empty() && mod.header.empty()) {
        // Unreachable invariant: codegen either emits a module or reports an
        // error. Never hand an empty module to the toolchain silently.
        std::fprintf(stderr,
                     "cp3c: internal error: code generation produced no "
                     "output\n");
        return kExitCompileError;
    }
    return kExitSuccess;
}

// The codegen half for a single-unit graph.
int codegenEntry(const CompilationUnit& entry, const SemaResult& sema,
                 const BuildContext& ctx, const CodegenOptions& options,
                 GeneratedModule& mod) {
    CollectingDiagnosticSink cgSink;
    // --debug carries `#line` mapping so DWARF and gdb point at the .cp3 file.
    const bool lineDirectives =
        ctx.opt->compile.profile == BuildProfile::Debug;
    mod = generateModule(
        {*entry.ast, sema, *ctx.moduleName, cgSink, lineDirectives, options});
    return codegenOutcome(cgSink, mod, ctx);
}

// Assembles the whole-program unit list: every loaded unit with a parsed AST,
// the entry carrying `frontend.sema` and every other unit its own table from
// the frontend pass. Prelude units (their items arrive through importers) and
// units without a checked table are skipped.
std::vector<CheckedModuleUnit> checkedUnits(const FrontendResult& frontend) {
    std::vector<CheckedModuleUnit> units;
    for (CompilationUnit* unit : frontend.graph->units()) {
        if (unit == nullptr || !unit->ast || unit->isPrelude) continue;
        const SemaResult* sema = nullptr;
        if (unit->isEntry) {
            sema = &frontend.sema;
        } else {
            const auto it = frontend.unitSemas.find(unit->id);
            if (it == frontend.unitSemas.end()) continue;
            sema = &it->second;
        }
        units.push_back(CheckedModuleUnit{unit, sema});
    }
    return units;
}

// The codegen half for a local-module graph.
int codegenProgram(const FrontendResult& frontend, const BuildContext& ctx,
                   const CodegenOptions& options, GeneratedModule& mod) {
    CollectingDiagnosticSink cgSink;
    const bool lineDirectives =
        ctx.opt->compile.profile == BuildProfile::Debug;
    mod = generateProgramModule({*frontend.graph, checkedUnits(frontend),
                                 *ctx.moduleName, cgSink, lineDirectives,
                                 options});
    return codegenOutcome(cgSink, mod, ctx);
}

// The units and context of one worker-file resolution (`prepareWorkers`): the
// entry AST to scan, the folder its literals resolve against, and the sink the
// generated modules feed into.
struct WorkerInputs {
    const CompilationUnit& entry;
    fs::path folder;
    const std::string& moduleName;
    SourceManager& sm;
    const BuildContext& ctx;
};

// The worker-file half of codegen (D80, spec 13): resolves every
// `new Worker("<path>")` site that starts a real file unit, lowers each worker
// unit in its own graph, and returns the entry's codegen options plus the extra
// TUs the toolchain must compile. Worker-unit diagnostics are rendered here -
// they are the user's compile errors too.
int prepareWorkers(const WorkerInputs& in, CodegenOptions& options,
                   std::vector<detail::BuildRequest::ExtraModule>& extras,
                   std::vector<GeneratedModule>& modules,
                   std::vector<std::string>& workerStdModules) {
    const std::vector<WorkerPathSite> sites = collectWorkerPathSites(*in.entry.ast);
    if (sites.empty()) return kExitSuccess;
    const bool lineDirectives =
        in.ctx.opt->compile.profile == BuildProfile::Debug;
    ResolvedWorkers workers = resolveWorkerUnits(sites, in.folder,
                                                 in.moduleName, in.sm,
                                                 lineDirectives,
                                                 in.ctx.opt->compile);
    renderDiagnostics(workers.diagnostics, *in.ctx.render, stderr);
    if (workers.failed) {
        return kExitCompileError;
    }
    options.workers = std::move(workers.sites);
    modules = std::move(workers.modules);
    workerStdModules = std::move(workers.stdModules);
    for (std::size_t i = 0; i < modules.size(); ++i) {
        extras.push_back(detail::BuildRequest::ExtraModule{
            workers.moduleNames[i], &modules[i], workers.sourceKeys[i]});
    }
    return kExitSuccess;
}

// Every installed package that contributed a unit to this program carries its
// implementation beside the declaration surface: either a cached object for
// this platform (`pak/<platform>/<module>.o`, used as is) or the C++ source
// generated from its original modules (`pak/**/*.cpp`, compiled here). The
// entry's `index.cp3` only supplies declarations. Returns the implementation
// units in link order, with the include roots source units need.
std::vector<detail::BuildRequest::PackageImplementation>
collectPackageImplementations(const ModuleGraph& graph) {
    std::vector<detail::BuildRequest::PackageImplementation> out;
    std::unordered_set<std::string> seen;
    for (CompilationUnit* unit : graph.compilationOrder()) {
        if (unit == nullptr || !unit->isPackage || unit->packageRoot.empty()) {
            continue;
        }
        const fs::path root = unit->packageRoot;
        std::error_code ec;

        // Prefer the package's cached binary for this platform. The install
        // step has already checked that an excluded-source package has one;
        // using it here is what makes a `.cp3pak` self-contained and instant.
        PlatformCacheManifest cache;
        if (installedPlatformCache(root, cache) && !cache.artifacts.empty()) {
            const std::size_t before = out.size();
            std::vector<const PlatformCacheArtifact*> artifacts;
            for (const PlatformCacheArtifact& artifact : cache.artifacts) {
                if (!artifact.member.empty()) artifacts.push_back(&artifact);
            }
            std::sort(artifacts.begin(), artifacts.end(),
                      [](const PlatformCacheArtifact* a,
                         const PlatformCacheArtifact* b) {
                          return a->member < b->member;
                      });
            for (const PlatformCacheArtifact* artifact : artifacts) {
                const fs::path object =
                    (root / artifact->member).lexically_normal();
                const std::string key = object.generic_string();
                if (!seen.insert(key).second) continue;
                detail::BuildRequest::PackageImplementation impl;
                impl.object = object;
                impl.sourceKey = key;
                impl.cached = true;
                out.push_back(std::move(impl));
            }
            if (out.size() != before) continue;
        }

        const fs::path pak = root / "pak";
        if (!fs::is_directory(pak, ec)) continue;

        std::vector<fs::path> sources;
        std::vector<fs::path> includeDirs{pak};
        for (const auto& entry :
             fs::recursive_directory_iterator(pak, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            const fs::path& path = entry.path();
            if (path.extension() == ".cpp") {
                sources.push_back(path);
            } else if (path.extension() == ".hpp" ||
                       path.extension() == ".h") {
                includeDirs.push_back(path.parent_path());
            }
        }
        std::sort(sources.begin(), sources.end());
        std::sort(includeDirs.begin(), includeDirs.end());

        for (const fs::path& source : sources) {
            const std::string key = source.generic_string();
            if (!seen.insert(key).second) continue;
            detail::BuildRequest::PackageImplementation impl;
            impl.source = source;
            impl.sourceKey = key;
            std::string last;
            for (const fs::path& dir : includeDirs) {
                const std::string text = dir.generic_string();
                if (text == last) continue;
                impl.includeDirs.push_back(dir);
                last = text;
            }
            out.push_back(std::move(impl));
        }
    }
    return out;
}

int runOnPackageError(const std::string& entry) {
    // CLI-README 5.1: a package build produces a .cp3pak - there is nothing
    // to run, so `run` is a usage error for a package project.
    std::fprintf(stderr,
                 "cp3c: '%s' is a 'type: package' project; packages have "
                 "no runnable output\n",
                 entry.c_str());
    std::fprintf(stderr,
                 "cp3c: use 'cp3c build' to produce the .cp3pak, or "
                 "'cp3c exec <pkg>' to run a package tool\n");
    return kExitUsageError;
}

}  // namespace

int cmdBuildOrRun(const CliOptions& opt, bool execute) {
    SourceManager sm;
    SourceFile* file = nullptr;
    std::string err;
    if (!loadEntrySource(opt.file, sm, file, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }

    const fs::path entryPath(opt.file);
    const fs::path projectRoot = findProjectRoot(entryPath);
    const std::string moduleName = moduleNameFor(entryPath, projectRoot);
    // `exec` builds one of a package's `bin` programs: the manifest is a
    // package, but this entry is a runnable application, not pack input.
    // `test` does the same for test files inside a package project.
    const bool packageProject = isPackageProject(projectRoot) && !opt.execTool &&
                                !opt.testMode;
    if (packageProject && execute) return runOnPackageError(opt.file);

    // The project's type-system vocabulary (spec 03) joins the CLI options.
    CliOptions effective = opt;
    applyProjectTypeSettings(projectRoot, effective.compile);

    // Release builds use a project-local PGO profile when one exists. An
    // explicit --pgo path wins; --no-pgo disables only this autodetection.
    if (effective.compile.profile == BuildProfile::Release &&
        !effective.noPgo && effective.pgoProfile.empty() &&
        !effective.pgoGenerate) {
        std::error_code pgoEc;
        const fs::path candidate =
            projectRoot / ".cp3" / "pgo" / "default.profdata";
        if (fs::is_regular_file(candidate, pgoEc)) {
            effective.pgoProfile = candidate.string();
        }
    }
    if (!effective.pgoProfile.empty()) {
        std::error_code pgoEc;
        fs::path profilePath(effective.pgoProfile);
        if (profilePath.is_relative()) {
            const fs::path absolute = fs::absolute(profilePath, pgoEc);
            if (!pgoEc) profilePath = absolute;
        }
        if (!fs::is_regular_file(profilePath, pgoEc)) {
            std::fprintf(stderr, "cp3c: PGO profile not found: %s\n",
                         profilePath.string().c_str());
            return kExitUsageError;
        }
        effective.pgoProfile = profilePath.string();
    }
    if (effective.pgoGenerate && effective.pgoGenerateDir.empty()) {
        effective.pgoGenerateDir =
            (projectRoot / ".cp3" / "pgo" / "raw").string();
    }

    const RenderOptions ro = makeRenderOptions(effective, file->path);
    const BuildContext ctx{&effective, &ro, &moduleName, execute};

    reportRunProgress(execute, effective.verbose,
                      "checking source and imports...");
    FrontendResult frontend = runFrontend(sm, *file, ctx);
    if (frontend.exitCode != kExitSuccess) return frontend.exitCode;
    CompilationUnit* entry = frontend.graph->entry();

    reportRunProgress(execute, effective.verbose,
                      "generating native code...");
    CodegenOptions options;
    options.importedEnums = importedEnumsFor(*frontend.graph, *entry);
    options.importedClasses = importedClassesFor(*frontend.graph, *entry);
options.importedInterfaces = importedInterfacesFor(*frontend.graph, *entry);
    options.typeSystem = effective.compile.typeSystem;
    options.nativeBindings = effective.compile.nativeBindings;
    std::vector<GeneratedModule> workerModules;
    std::vector<detail::BuildRequest::ExtraModule> extras;
    std::vector<std::string> workerStdModules;
    const int workerExit = prepareWorkers(
        WorkerInputs{*entry, fs::path(file->path).parent_path(), moduleName, sm,
                     ctx},
        options, extras, workerModules, workerStdModules);
    if (workerExit != kExitSuccess) return workerExit;

    GeneratedModule mod;
    const int cgExit = frontend.program
                           ? codegenProgram(frontend, ctx, options, mod)
                           : codegenEntry(*entry, frontend.sema, ctx, options,
                                          mod);
    if (cgExit != kExitSuccess) return cgExit;

    if (effective.verbose && effective.compile.maxMemoryMb != 0) {
        std::fprintf(stderr, "cp3c: --max-memory cap: %llu MB (codegen phase)\n",
                     static_cast<unsigned long long>(
                         effective.compile.maxMemoryMb));
    }

    if (packageProject) {
        detail::PackInput input;
        input.opt = &effective;
        input.entryPath = &entryPath;
        input.moduleName = &moduleName;
        input.module = &mod;
        input.source = file;
        input.program = entry->ast.get();
        input.nativeBindings = &effective.compile.nativeBindings;
        input.execute = execute;
        return detail::packProject(input);
    }

    // Every resolved "std/..." specifier in the whole graph (any unit, not just
    // the entry) selects that module's native backend for this build (D68), and
    // so do the worker units' (they live in their own graphs, so the resolver
    // recorded them separately).
    std::vector<std::string> importedStdModules =
        cp3::cli::importedStdModules(*frontend.graph);
    {
        std::unordered_set<std::string> seen(importedStdModules.begin(),
                                             importedStdModules.end());
        for (const std::string& specifier : workerStdModules) {
            if (seen.insert(specifier).second) {
                importedStdModules.push_back(specifier);
            }
        }
    }
    const std::vector<detail::BuildRequest::PackageImplementation>
        packageImplementations = collectPackageImplementations(*frontend.graph);
    detail::BuildRequest request;
    request.opt = &effective;
    request.entry = &entryPath;
    request.moduleName = &moduleName;
    request.module = &mod;
    request.importedStdModules = &importedStdModules;
    request.extraModules = &extras;
    request.packageImplementations = &packageImplementations;
    request.execute = execute;
    reportRunProgress(execute, effective.verbose,
                      "preparing native build...");
    return detail::buildWithToolchain(request);
}

// ---------------------------------------------------------------------------
// `cp3 test [file.cp3 ...]`
// ---------------------------------------------------------------------------
// Runs every test file as a program (build + execute) and reports a
// summary. Explicit files come from the positionals; with no arguments the
// test files are discovered under the project root: `tests/` and `test/`
// directories (recursively, `*.cp3`), then `*.test.cp3` at the root. A
// package project is allowed: each test file runs with `testMode` set, so
// it builds as an application (importing the package entry) instead of
// being rejected as a package project.
namespace {

void collectTestFiles(const fs::path& dir, std::vector<fs::path>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == ".cp3") {
            out.push_back(it->path());
        }
    }
}

}  // namespace

int cmdTest(const CliOptions& opt) {
    std::vector<std::string> files;
    files.insert(files.end(), opt.positionals.begin(), opt.positionals.end());
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    const fs::path projectRoot = findProjectRoot(cwd / "placeholder.cp3");
    if (files.empty()) {
        std::vector<fs::path> discovered;
        collectTestFiles(projectRoot / "tests", discovered);
        collectTestFiles(projectRoot / "test", discovered);
        if (discovered.empty() && fs::is_directory(projectRoot, ec)) {
            for (const auto& item :
                 fs::directory_iterator(projectRoot, ec)) {
                if (ec) break;
                const fs::path path = item.path();
                const std::string name = path.filename().string();
                if (path.extension() == ".cp3" &&
                    name.size() > 9 &&
                    name.compare(name.size() - 9, 9, ".test.cp3") == 0) {
                    discovered.push_back(path);
                }
            }
        }
        std::sort(discovered.begin(), discovered.end());
        discovered.erase(
            std::unique(discovered.begin(), discovered.end()),
            discovered.end());
        for (const fs::path& path : discovered) {
            files.push_back(path.string());
        }
    }
    if (files.empty()) {
        std::fprintf(stderr,
                     "cp3c: no test files found (looked in 'tests/', "
                     "'test/' and '*.test.cp3'; pass files explicitly: "
                     "'cp3c test tests/example.cp3')\n");
        return kExitUsageError;
    }
    int passed = 0;
    int failed = 0;
    for (const std::string& file : files) {
        CliOptions one = opt;
        one.file = file;
        one.positionals.clear();
        one.testMode = true;
        const int rc = cmdBuildOrRun(one, true);
        if (rc == kExitSuccess) {
            ++passed;
        } else {
            ++failed;
            std::fprintf(stderr, "cp3c: test failed: %s (exit %d)\n",
                         file.c_str(), rc);
        }
    }
    std::fprintf(stderr, "cp3c: %d passed, %d failed (%zu total)\n", passed,
                 failed, files.size());
    return failed == 0 ? kExitSuccess : kExitCompileError;
}

}  // namespace cp3::cli
