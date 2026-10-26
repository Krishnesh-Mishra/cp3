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
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
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
    if (entry != nullptr && entry->ast) {
        analyzeProgram(*entry->ast, sema, sink, effective.compile);
    }

    const RenderOptions ro = makeRenderOptions(effective, file->path);
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
