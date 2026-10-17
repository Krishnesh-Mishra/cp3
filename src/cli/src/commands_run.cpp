// =============================================================================
// cli/commands_run.cpp — `cp3c build` / `cp3c run`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdBuildOrRun.
//
// Loads the module graph (prelude injection, import resolution), runs sema on
// the entry unit, threads the resulting SemaResult into codegen, then hands the
// generated module to the toolchain driver. `execute` selects `run` semantics.
// =============================================================================
#include <cstdio>
#include <memory>
#include <string>

#include "cp3/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

#include "private/cache.hpp"
#include "private/commands.hpp"
#include "private/diagnostics.hpp"
#include "private/pipeline.hpp"
#include "private/util.hpp"

namespace cp3::cli {

int cmdBuildOrRun(const CliOptions& opt, bool execute) {
    SourceManager sm;
    SourceFile* file = nullptr;
    std::string err;
    if (!loadEntrySource(opt.file, sm, file, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }
    const RenderOptions ro = makeRenderOptions(opt, file->path);

    CollectingDiagnosticSink feSink;
    const std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *file, feSink);
    CompilationUnit* entry = graph->entry();
    SemaResult sema;
    if (entry != nullptr && entry->ast) {
        analyzeProgram(*entry->ast, sema, feSink);
    }
    renderDiagnostics(feSink.diagnostics(), ro, stderr);
    if (feSink.hasErrors()) {
        if (onlyNotImplemented(feSink.diagnostics())) {
            return notImplemented(execute ? "run" : "build");
        }
        return kExitCompileError;
    }

    // The loader always yields an entry unit with a parsed AST; guard anyway
    // so a future loader change can never turn into a crash here.
    if (entry == nullptr || !entry->ast) {
        return notImplemented(execute ? "run" : "build");
    }

    const fs::path entryPath(opt.file);
    const std::string moduleName = moduleNameFor(entryPath, findProjectRoot(entryPath));

    CollectingDiagnosticSink cgSink;
    const GeneratedModule mod = generateModule(*entry->ast, sema, moduleName, cgSink);
    renderDiagnostics(cgSink.diagnostics(), ro, stderr);
    if (cgSink.hasErrors()) {
        if (onlyNotImplemented(cgSink.diagnostics())) {
            return notImplemented(execute ? "run" : "build");
        }
        return kExitCompileError;
    }

    // The codegen stage is still a stub (or produced no output): report the
    // scaffold marker so the test harness skips instead of failing while the
    // stage is unimplemented (ARCHITECTURE §4).
    if (mod.source.empty() && mod.header.empty()) {
        return notImplemented(execute ? "run" : "build");
    }

    if (opt.verbose && opt.compile.maxMemoryMb != 0) {
        std::fprintf(stderr, "cp3c: --max-memory cap: %llu MB (codegen phase)\n",
                     static_cast<unsigned long long>(opt.compile.maxMemoryMb));
    }
    return detail::buildWithToolchain(opt, entryPath, moduleName, mod, execute);
}

}  // namespace cp3::cli
