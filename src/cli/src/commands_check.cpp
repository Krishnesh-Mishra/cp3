// =============================================================================
// cli/commands_check.cpp - `cp3c check` (frontend only).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdCheck.
//
// Loads the entry file plus its module graph (prelude injection, import
// resolution), type-checks the entry unit and renders diagnostics; no C++
// generation and no toolchain involvement.
// =============================================================================
#include <cstdio>
#include <memory>
#include <string>

#include "cp3/analysis.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

#include "private/commands.hpp"
#include "private/pipeline.hpp"
#include "private/util.hpp"

namespace cp3::cli {

int cmdCheck(const CliOptions& opt) {
    SourceManager sm;
    SourceFile* file = nullptr;
    std::string err;
    if (!loadEntrySource(opt.file, sm, file, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }

    CollectingDiagnosticSink sink;
    const std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *file, sink);
    SemaResult sema;
    CompilationUnit* entry = graph->entry();
    if (entry != nullptr && entry->ast) {
        analyzeProgram(*entry->ast, sema, sink);
    }

    renderDiagnostics(sink.diagnostics(), makeRenderOptions(opt, file->path),
                      stderr);
    if (sink.hasErrors()) {
        if (onlyNotImplemented(sink.diagnostics())) return notImplemented("check");
        return kExitCompileError;
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
