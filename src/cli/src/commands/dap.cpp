// =============================================================================
// src/cli/src/commands/dap.cpp - `cp3c dap` command surface (debug adapter glue).
//
// OWNERSHIP: cli module (src/cli/**) for the command; the protocol server and
// the gdb session live in src/dap/** (cp3/dap.hpp).
//
// The command owns ONE thing: adapting the in-process build pipeline into the
// transport-neutral BuildFn the server calls. The build reuses the exact stages
// `cp3c build` runs, but with the Debug profile so codegen emits `#line`
// directives (src/codegen/src/support/line_map.cpp) and the toolchain compiles
// with -g -O0. Everything is in-process: the adapter's stdout carries DAP
// frames only, never compiler output (diagnostics are rendered into a string
// and forwarded as `output` events).
// =============================================================================
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/codegen.hpp"
#include "cp3/dap.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/module.hpp"
#include "cp3/options.hpp"
#include "cp3/source.hpp"

#include "detail/cache.hpp"
#include "detail/commands.hpp"
#include "detail/diagnostics.hpp"
#include "detail/package_json.hpp"
#include "detail/pipeline.hpp"
#include "detail/util.hpp"

namespace cp3::cli {
namespace {

// Renders diagnostics into a string: the DAP server forwards compiler output
// as `output` events, so it must never reach stderr mid-session.
std::string renderToText(const std::vector<Diagnostic>& diagnostics,
                         const RenderOptions& options) {
    std::FILE* stream = std::tmpfile();
    if (stream == nullptr) return {};
    renderDiagnostics(diagnostics, options, stream);
    std::fflush(stream);
    std::rewind(stream);
    std::string text;
    char buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        text.append(buffer, got);
    }
    std::fclose(stream);
    return text;
}

// The launch `program` resolved against the launch cwd (the adapter's own cwd
// is the extension host's, so it cannot be trusted to match).
fs::path resolveEntry(const std::string& program, const std::string& cwd) {
    fs::path entry(program);
    if (entry.is_relative() && !cwd.empty()) entry = fs::path(cwd) / entry;
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(entry, ec);
    return ec ? entry : canonical;
}

// One Debug build behind `launch`: the same frontend + toolchain stages
// `cp3c build --debug` runs, with diagnostics captured instead of printed.
dap::BuildOutcome buildDebugProgram(const std::string& program,
                                    const std::string& cwd) {
    dap::BuildOutcome outcome;
    try {
        const fs::path entryPath = resolveEntry(program, cwd);
        SourceManager sm;
        SourceFile* file = nullptr;
        std::string err;
        if (!loadEntrySource(entryPath.string(), sm, file, err)) {
            outcome.log = err + "\n";
            return outcome;
        }
        const fs::path projectRoot = findProjectRoot(entryPath);
        const std::string moduleName = moduleNameFor(entryPath, projectRoot);
        CliOptions opt;
        opt.file = entryPath.string();
        opt.compile.profile = BuildProfile::Debug;
        const RenderOptions render = makeRenderOptions(opt, file->path);

        std::string log;
        CollectingDiagnosticSink feSink;
        std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *file, feSink);
        CompilationUnit* entry = graph != nullptr ? graph->entry() : nullptr;
        SemaResult sema;
        if (entry != nullptr && entry->ast) {
            analyzeProgram(*entry->ast, sema, feSink);
        }
        log += renderToText(feSink.diagnostics(), render);
        if (feSink.hasErrors() || entry == nullptr || !entry->ast) {
            outcome.log = log.empty() ? "cp3c: the frontend reported errors\n" : log;
            return outcome;
        }
        if (isPackageProject(projectRoot)) {
            outcome.log = "'" + program + "' is a 'type: package' project; "
                          "packages have no runnable output\n";
            return outcome;
        }
        CollectingDiagnosticSink cgSink;
        // --debug codegen carries `#line` mapping so DWARF and gdb point at the
        // .cp3 file (the whole point of the debug adapter).
        const GeneratedModule mod =
            generateModule({*entry->ast, sema, moduleName, cgSink,
                            /*lineDirectives=*/true});
        log += renderToText(cgSink.diagnostics(), render);
        if (cgSink.hasErrors() || (mod.source.empty() && mod.header.empty())) {
            outcome.log = log.empty() ? "cp3c: code generation produced no module\n" : log;
            return outcome;
        }
        std::vector<std::string> importedStdModules;
        for (const ModuleDependency& dep : graph->dependencies()) {
            if (dep.resolution == ModuleResolution::Std) {
                importedStdModules.push_back(dep.specifier);
            }
        }
        detail::BuildRequest request;
        request.opt = &opt;
        request.entry = &entryPath;
        request.moduleName = &moduleName;
        request.module = &mod;
        request.importedStdModules = &importedStdModules;
        request.execute = false;
        const int exitCode = detail::buildWithToolchain(request);
        if (exitCode != kExitSuccess) {
            log += "cp3c: build failed (exit " + std::to_string(exitCode) + ")\n";
            outcome.log = log;
            return outcome;
        }
        outcome.built = true;
        outcome.exePath = makeBuildLayout(entryPath).exePath.string();
        outcome.log = log.empty() ? "cp3c: built " + moduleName + " (debug)\n" : log;
        return outcome;
    } catch (const std::exception& error) {
        outcome.log = std::string("cp3c: internal error while building: ") +
                      error.what() + "\n";
        return outcome;
    } catch (...) {
        outcome.log = "cp3c: internal error while building: unknown exception\n";
        return outcome;
    }
}

}  // namespace

int cmdDap(const CliOptions& opt) {
    (void)opt;  // the adapter owns its per-launch options (Debug profile)
    return dap::runStdioServer(&buildDebugProgram);
}

}  // namespace cp3::cli
