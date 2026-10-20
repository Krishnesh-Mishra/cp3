// =============================================================================
// src/cli/src/commands/run.cpp - `cp3c build` / `cp3c run`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdBuildOrRun.
//
// Loads the module graph (prelude injection, import resolution), runs sema on
// the entry unit, threads the resulting SemaResult into codegen, then hands the
// generated module to the toolchain driver. `execute` selects `run` semantics.
// =============================================================================
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

#include "detail/cache.hpp"
#include "detail/commands.hpp"
#include "detail/diagnostics.hpp"
#include "detail/package_json.hpp"
#include "detail/pipeline.hpp"
#include "detail/util.hpp"
#include "detail/worker_module.hpp"

namespace cp3::cli {

namespace {

// Everything the frontend + codegen steps need beyond the source itself.
struct BuildContext {
    const CliOptions* opt = nullptr;
    const RenderOptions* render = nullptr;
    const std::string* moduleName = nullptr;  // e.g. "src/index"
    bool execute = false;                     // `cp3c run` was requested
};

// The load+sema half of the pipeline. exitCode is kExitSuccess when `graph`
// carries an analyzed entry unit to hand to codegen.
struct FrontendResult {
    std::unique_ptr<ModuleGraph> graph;
    SemaResult sema;
    int exitCode = kExitSuccess;
};

FrontendResult runFrontend(SourceManager& sm, SourceFile& file,
                          const BuildContext& ctx) {
    FrontendResult result;
    CollectingDiagnosticSink feSink;
    result.graph = loadModuleGraph(sm, file, feSink);
    CompilationUnit* entry = result.graph->entry();
    if (entry != nullptr && entry->ast) {
        analyzeProgram(*entry->ast, result.sema, feSink, ctx.opt->compile);
    }
    renderDiagnostics(feSink.diagnostics(), *ctx.render, stderr);
    if (feSink.hasErrors()) {
        result.exitCode = onlyNotImplemented(feSink.diagnostics())
                              ? notImplemented(ctx.execute ? "run" : "build")
                              : kExitCompileError;
    } else if (entry == nullptr || !entry->ast) {
        // The loader always yields a parsed entry unit; guard anyway so a
        // future loader change can never turn into a crash here.
        result.exitCode = notImplemented(ctx.execute ? "run" : "build");
    }
    return result;
}

// The codegen half. Returns kExitSuccess when `mod` holds real generated code
// (an empty module keeps the not-implemented marker for the harness).
int codegenEntry(const CompilationUnit& entry, const SemaResult& sema,
                 const BuildContext& ctx, const CodegenOptions& options,
                 GeneratedModule& mod) {
    CollectingDiagnosticSink cgSink;
    // --debug carries `#line` mapping so DWARF and gdb point at the .cp3 file.
    const bool lineDirectives =
        ctx.opt->compile.profile == BuildProfile::Debug;
    mod = generateModule(
        {*entry.ast, sema, *ctx.moduleName, cgSink, lineDirectives, options});
    renderDiagnostics(cgSink.diagnostics(), *ctx.render, stderr);
    if (cgSink.hasErrors()) {
        return onlyNotImplemented(cgSink.diagnostics())
                   ? notImplemented(ctx.execute ? "run" : "build")
                   : kExitCompileError;
    }
    if (mod.source.empty() && mod.header.empty()) {
        return notImplemented(ctx.execute ? "run" : "build");
    }
    return kExitSuccess;
}

// The worker half of codegen (spec 13): resolves the canonical worker import,
// lowers the worker unit by itself, and returns both the entry's codegen
// options and the extra TU the toolchain must compile. A worker unit that does
// not lower leaves `options.worker` unset, so the entry's `worker(...)` stays a
// CP3002 skip (the worker unit's own diagnostics are never rendered: the entry
// is the unit the user compiled).
struct WorkerLinkage {
    CodegenOptions options;
    GeneratedModule module;  // empty when there is no compilable worker
    std::string moduleName;
    std::string sourceKey;
};

WorkerLinkage prepareWorker(const CompilationUnit& entry, const ModuleGraph& graph,
                            const std::string& moduleName, bool lineDirectives,
                            const CompileOptions& compile) {
    WorkerLinkage out;
    const std::optional<WorkerUnit> worker =
        findWorkerUnit(*entry.ast, graph, moduleName);
    if (!worker || !programCallsWorker(*entry.ast)) return out;
    CollectingDiagnosticSink workerSink;
    out.module =
        generateWorkerModule(*worker, lineDirectives, workerSink, compile);
    if (out.module.source.empty()) {
        out.module = GeneratedModule{};
        return out;
    }
    out.options.worker = WorkerImport{worker->moduleName, worker->headerPath};
    out.moduleName = worker->moduleName;
    out.sourceKey = worker->sourceKey;
    return out;
}

int runOnPackageError(const std::string& entry) {
    // CLI-README §5.1: a package build produces a .cp3pak - there is nothing
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
    const bool packageProject = isPackageProject(projectRoot);
    if (packageProject && execute) return runOnPackageError(opt.file);

    // The project's type-system vocabulary (spec 03) joins the CLI options.
    CliOptions effective = opt;
    applyProjectTypeSettings(projectRoot, effective.compile);

    const RenderOptions ro = makeRenderOptions(effective, file->path);
    const BuildContext ctx{&effective, &ro, &moduleName, execute};

    FrontendResult frontend = runFrontend(sm, *file, ctx);
    if (frontend.exitCode != kExitSuccess) return frontend.exitCode;
    CompilationUnit* entry = frontend.graph->entry();

    const bool lineDirectives =
        effective.compile.profile == BuildProfile::Debug;
    const WorkerLinkage worker = prepareWorker(*entry, *frontend.graph,
                                               moduleName, lineDirectives,
                                               effective.compile);

    CodegenOptions codegenOptions = worker.options;
    codegenOptions.typeSystem = effective.compile.typeSystem;
    GeneratedModule mod;
    const int cgExit =
        codegenEntry(*entry, frontend.sema, ctx, codegenOptions, mod);
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
        input.execute = execute;
        return detail::packProject(input);
    }

    // Every resolved "std/..." specifier in the whole graph (any unit, not just
    // the entry) selects that module's native backend for this build (D68).
    std::vector<std::string> importedStdModules;
    for (const ModuleDependency& dep : frontend.graph->dependencies()) {
        if (dep.resolution == ModuleResolution::Std) {
            importedStdModules.push_back(dep.specifier);
        }
    }
    detail::BuildRequest request;
    request.opt = &effective;
    request.entry = &entryPath;
    request.moduleName = &moduleName;
    request.module = &mod;
    request.importedStdModules = &importedStdModules;
    std::vector<detail::BuildRequest::ExtraModule> extras;
    if (!worker.module.source.empty()) {
        extras.push_back(detail::BuildRequest::ExtraModule{
            worker.moduleName, &worker.module, worker.sourceKey});
    }
    request.extraModules = &extras;
    request.execute = execute;
    return detail::buildWithToolchain(request);
}

}  // namespace cp3::cli
