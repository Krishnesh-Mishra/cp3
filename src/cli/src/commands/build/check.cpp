// =============================================================================
// src/cli/src/commands/check.cpp - `cp3c check` (frontend + worker diagnostics).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdCheck.
//
// Loads the entry file plus its module graph (prelude injection, import
// resolution), type-checks the entry unit and renders diagnostics. Codegen is
// not run for output, but the worker surface is resolved here so the D80
// diagnostics (CP3905-CP3911, CP3601) are available in check mode exactly as
// in build/run mode; non-worker codegen diagnostics stay part of build/run.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

#include "detail/cache/cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/pipeline.hpp"
#include "detail/core/util.hpp"
#include "detail/core/worker_module.hpp"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/core/ast.hpp"

namespace cp3::cli {

namespace {

bool isWorkerDiagnostic(const Diagnostic& d) {
    return d.code == diagcode::kWorkerMissingAnnotation ||
           d.code == diagcode::kWorkerVoidSend ||
           d.code == diagcode::kWorkerPayloadNotCopyable ||
           d.code == diagcode::kWorkerPayloadMismatch ||
           d.code == diagcode::kWorkerEnclosingCapture ||
           d.code == diagcode::kWorkerComputedPath ||
           d.code == diagcode::kWorkerHandleMember ||
           d.code == diagcode::kWorkerEntryShape ||
           d.code == diagcode::kWorkerListenerParameter ||
           d.code == diagcode::kWorkerModuleForm ||
           d.code == diagcode::kModuleNotFound;
}

}  // namespace

int cmdCheck(const CliOptions& opt) {
    SourceManager sm;
    SourceFile* file = nullptr;
    std::string err;
    if (!loadEntrySource(opt.file, sm, file, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }

    // Spec 03: the project's package.json selects the plain type-name
    // vocabulary and the strictNumericCasts policy.
    CliOptions effective = opt;
    applyProjectTypeSettings(findProjectRoot(fs::path(opt.file)),
                             effective.compile);

    CollectingDiagnosticSink sink;
    const std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *file, sink);
    // The native bindings the program imports: sema type-checks the calls
    // against them, codegen lowers them.
    effective.compile.nativeBindings = graph->nativeBindings();
    SemaResult sema;
    CompilationUnit* entry = graph->entry();
    std::unordered_map<std::uint32_t, SemaResult> unitSemas;
    if (entry != nullptr && entry->ast) {
        analyzeProgramGraph(*graph, sema, unitSemas, sink, effective.compile);
    }
    // A `type: package` entry module with no exports fails only at pack time
    // (the packer reports CP3604 for the missing contract after the fact).
    // Surface it at check time so an author sees it on every save. The warning
    // is emitted with the rest of the diagnostics below, through the same
    // render path.
    const bool isPackageProjectEntry =
        entry != nullptr && entry->ast && isPackageProject(findProjectRoot(fs::path(opt.file)));
    const bool packageEntryExports = [&]() {
        if (!isPackageProjectEntry) return true;
        for (const StmtPtr& s : entry->ast->body) {
            if (s && s->kind == NodeKind::ExportDeclaration) return true;
        }
        return false;
    }();
    if (opt.reportCycle) {
        // Cycle decision summary (automatic cycle handling, Stages 0-1):
        //   * cycleCapableClasses - classes that can reach themselves through
        //     strong edges (weak edges excluded, unknown fail-closed).
        //   * cycleEscapeSites - one line per strong field storing an object
        //     of a cycle-capable (or unknown) type.
        //   * manual-weak share - the fraction of the cycle-capable classes
        //     that still need a manually marked `weak` edge. A class counts
        //     as needing one when the CP3401 cycle diagnostic names it in its
        //     "reference cycle ... has no weak edge" path. The rest are exempt
        //     single self-references (e.g. `Node.next`), which must keep
        //     working. Groups already broken by a user `weak` edge never
        //     become strong-cycle-capable, so they do not appear here.
        const std::unordered_set<std::string> capable(
            sema.cycleCapableClasses.begin(), sema.cycleCapableClasses.end());
        std::unordered_set<std::string> unresolved;
        for (const Diagnostic& d : sink.diagnostics()) {
            if (d.code != diagcode::kTypeMismatch) continue;
            if (d.message.find("has no weak edge") == std::string::npos) {
                continue;
            }
            // The cycle path is the only identifier-bearing phrase in this
            // message, so any capable class named in it is unresolved.
            std::string token;
            for (const char ch : d.message) {
                const bool ident =
                    (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '_';
                if (ident) {
                    token.push_back(ch);
                } else {
                    if (capable.count(token)) unresolved.insert(token);
                    token.clear();
                }
            }
            if (capable.count(token)) unresolved.insert(token);
        }

        const int total = static_cast<int>(sema.cycleCapableClasses.size());
        std::fprintf(stderr, "cycle-capable classes (%d):\n", total);
        int needsWeak = 0;
        for (const std::string& c : sema.cycleCapableClasses) {
            if (unresolved.count(c) != 0) {
                ++needsWeak;
                std::fprintf(stderr, "  needs weak (unresolved): %s\n",
                             c.c_str());
            } else {
                std::fprintf(stderr, "  exempt (self-reference): %s\n",
                             c.c_str());
            }
        }

        std::vector<std::string> sites = sema.cycleEscapeSites;
        std::sort(sites.begin(), sites.end());
        sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
        std::fprintf(stderr, "store/escape sites (%d):\n",
                     static_cast<int>(sites.size()));
        for (const std::string& s : sites) {
            std::fprintf(stderr, "  store/escape: %s\n", s.c_str());
        }

        const double pct = total == 0
                               ? 0.0
                               : 100.0 * needsWeak / static_cast<double>(total);
        std::fprintf(stderr, "manual weak share: %d/%d (%.0f%%)\n", needsWeak,
                     total, pct);
        // Stages 2-3 (compiler-injected non-owning edges) are deferred: this
        // report previews the analysis without changing any ownership.
        std::fprintf(stderr,
                     "auto-weak preview: no change (deferred); break cycles "
                     "with an explicit weak edge\n");

        // The collector's view (the whole-program reference graph): every
        // cycle-capable strongly connected component with the concrete field
        // path that forms the cycle, and every acyclic class as zero-cost.
        std::fprintf(stderr, "cycle components (%d):\n",
                     static_cast<int>(sema.cycleComponents.size()));
        for (const auto& component : sema.cycleComponents) {
            std::string names;
            for (const std::string& c : component.classes) {
                if (!names.empty()) names += ",";
                names += c;
            }
            std::fprintf(stderr, "  cycle-capable: %s%s\n", names.c_str(),
                         component.hasOpaque ? " (reaches an opaque carrier)"
                                             : "");
            for (const std::string& path : component.paths) {
                std::fprintf(stderr, "    path: %s\n", path.c_str());
            }
        }
        std::fprintf(stderr, "acyclic classes (zero collector cost) (%d):\n",
                     static_cast<int>(sema.acyclicClasses.size()));
        for (const std::string& c : sema.acyclicClasses) {
            std::fprintf(stderr, "  zero-cost: %s\n", c.c_str());
        }
    }

    const RenderOptions ro = makeRenderOptions(effective, file->path);
    if (isPackageProjectEntry && !packageEntryExports) {
        Diagnostic d(diagcode::kPackageNoExports, Severity::Warning,
                     entry->ast->span,
                     "package entry module exports nothing: the package "
                     "produces no public surface. Add an exported "
                     "function/class/type or set 'type': 'application'.");
        CollectingDiagnosticSink packageSink;
        packageSink.report(std::move(d));
        renderDiagnostics(packageSink.diagnostics(), ro, stderr);
    }
    renderDiagnostics(sink.diagnostics(), ro, stderr);
    if (sink.hasErrors()) {
        return kExitCompileError;
    }
    if (entry == nullptr || !entry->ast) return kExitSuccess;

    const std::filesystem::path entryPath(opt.file);
    const std::string moduleName =
        moduleNameFor(entryPath, findProjectRoot(entryPath));
    CodegenOptions options;
    options.importedEnums = importedEnumsFor(*graph, *entry);
    options.importedClasses = importedClassesFor(*graph, *entry);
options.importedInterfaces = importedInterfacesFor(*graph, *entry);
    options.typeSystem = effective.compile.typeSystem;
    options.nativeBindings = effective.compile.nativeBindings;

    // File-form worker units: load/sema/lower each one in its own graph, the
    // same way build/run does. Their diagnostics are compile errors.
    const std::vector<WorkerPathSite> sites = collectWorkerPathSites(*entry->ast);
    std::vector<Diagnostic> workerDiagnostics;
    if (!sites.empty()) {
        ResolvedWorkers workers =
            resolveWorkerUnits(sites, entryPath.parent_path(), moduleName, sm,
                               false, effective.compile);
        workerDiagnostics = std::move(workers.diagnostics);
        options.workers = std::move(workers.sites);
        renderDiagnostics(workerDiagnostics, ro, stderr);
        for (const Diagnostic& d : workerDiagnostics) {
            if (d.severity == Severity::Error ||
                d.severity == Severity::Fatal) {
                return kExitCompileError;
            }
        }
    }

    // Inline worker form and the main-side send surface. Run the entry's
    // codegen without writing output, and surface only the worker diagnostics:
    // check mode is not a build, so codegen diagnostics unrelated to workers
    // are not part of `check`.
    CollectingDiagnosticSink cgSink;
    (void)generateModule(
        {*entry->ast, sema, moduleName, cgSink, false, options});
    std::vector<Diagnostic> surface;
    for (const Diagnostic& d : cgSink.diagnostics()) {
        if (isWorkerDiagnostic(d)) surface.push_back(d);
    }
    renderDiagnostics(surface, ro, stderr);
    for (const Diagnostic& d : surface) {
        if (d.severity == Severity::Error || d.severity == Severity::Fatal) {
            return kExitCompileError;
        }
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
