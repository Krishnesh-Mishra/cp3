// =============================================================================
// cli/commands_run.cpp — `cp3c build` / `cp3c run`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdBuildOrRun.
//
// Runs the frontend (parse + sema), then codegen, then hands the generated
// module to the toolchain driver. `execute` selects `run` semantics.
// =============================================================================
#include <cstdio>
#include <string>

#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
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
    ProgramPtr program = parseFile(*file, feSink);
    analyzeProgram(*program, feSink);
    renderDiagnostics(feSink.diagnostics(), ro, stderr);
    if (feSink.hasErrors()) {
        if (onlyNotImplemented(feSink.diagnostics())) {
            return notImplemented(execute ? "run" : "build");
        }
        return kExitCompileError;
    }

    const fs::path entry(opt.file);
    const std::string moduleName = moduleNameFor(entry, findProjectRoot(entry));

    CollectingDiagnosticSink cgSink;
    const GeneratedModule mod = generateModule(*program, cgSink, moduleName);
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
    return detail::buildWithToolchain(opt, entry, moduleName, mod, execute);
}

}  // namespace cp3::cli
