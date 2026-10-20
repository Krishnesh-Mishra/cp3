// =============================================================================
// src/cli/src/detail/commands.hpp - the command surface behind runCli (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in the per-concern src/commands/*.cpp files (STYLE.md
// §3/§8: headers declare, `.cpp` defines).
//
// Every entry point returns the documented exit code:
//   0 success | 1 compile error | 2 usage error | 3 not implemented
// (`run` additionally propagates the built program's own exit code.)
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/cli.hpp"
#include "cp3/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/options.hpp"
#include "cp3/source.hpp"

#include "detail/diagnostics.hpp"
#include "detail/pipeline.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

// -----------------------------------------------------------------------------
// Options parsed by runCli and handed to the commands.
// -----------------------------------------------------------------------------
struct CliOptions {
    CompileOptions compile{};
    std::string file;                     // resolved .cp3 entry (may be empty)
    std::vector<std::string> positionals; // extra positionals (init dir, exec pkg)
    std::vector<std::string> programArgs; // args after `--` for `run`
    bool verbose = false;
    bool all = false;                     // clean --all
    bool yes = false;                     // init non-interactive
    bool once = false;                    // dev --once (run one pass then exit)
    bool autoSelect = false;              // install --auto (GitHub selector)
    std::string initType;                 // init --type application|package
    std::string toolchain;                // --toolchain <id|path> (one command)
    bool projectScope = false;            // toolchain --project
    bool globalScope = false;             // toolchain --global
};

// -----------------------------------------------------------------------------
// Shared command helpers.
// -----------------------------------------------------------------------------

// Reports `<command>` as unimplemented (exit 3 + marker) on stderr.
int notImplemented(const std::string& command);

// Resolves the default entry file for `build` with no explicit file argument:
// package.json "main", then the conventional entry names.
std::string defaultEntryFile();

// True when every error/fatal diagnostic is the dedicated not-implemented code
// (CP3002). Such a failure is reported with the exit-3 + marker convention so
// tooling (and tests/runner/run_tests.py) can tell "unimplemented feature" from
// a genuine compile error while stages come online (ARCHITECTURE §4, §12).
bool onlyNotImplemented(const std::vector<Diagnostic>& diagnostics);

// Builds the render options for a command (TTY/--verbose -> pretty + color).
RenderOptions makeRenderOptions(const CliOptions& opt, std::string fallbackPath);

// Loads the entry file into a SourceManager (which must outlive the tokens,
// since token lexemes are views into SourceFile::text). Returns false with a
// message in `err` when the file cannot be read.
bool loadEntrySource(const std::string& path, SourceManager& sm, SourceFile*& out,
                     std::string& err);

// -----------------------------------------------------------------------------
// Command implementations (one per concern).
// -----------------------------------------------------------------------------
int cmdCheck(const CliOptions& opt);
int cmdBuildOrRun(const CliOptions& opt, bool execute);
int cmdDev(const CliOptions& opt);
int cmdFmt(const CliOptions& opt);
int cmdClean(const CliOptions& opt);
int cmdInit(const CliOptions& opt);
int cmdToolchain(const CliOptions& opt);
int cmdExec(const CliOptions& opt);
int cmdInstall(const CliOptions& opt);
int cmdLsp(const CliOptions& opt);
int cmdDap(const CliOptions& opt);

namespace detail {

// Everything buildWithToolchain needs from the frontend half of cmdBuildOrRun:
// pointers into the caller's locals (valid for the duration of the call).
struct BuildRequest {
    // An extra generated TU (spec 13: the worker modules the entry imports).
    // Written beside the entry's generated files and compiled before it.
    struct ExtraModule {
        std::string moduleName;         // output stem, e.g. "worker_impl"
        const GeneratedModule* module = nullptr;  // generated C++ (cpp/hpp)
        std::string sourceKey;          // incremental cache key (the .cp3 path)
    };

    const CliOptions* opt = nullptr;
    const fs::path* entry = nullptr;         // as resolved by the CLI
    const std::string* moduleName = nullptr;  // e.g. "src/index"
    const GeneratedModule* module = nullptr;  // generated C++ (cpp/hpp)
    const std::vector<std::string>* importedStdModules = nullptr;  // D68 selection
    const std::vector<ExtraModule>* extraModules = nullptr;  // worker modules
    bool execute = false;                     // `cp3c run` was requested
};

// Compiles + links the generated module through the detected toolchain,
// reusing cached objects when nothing changed. `execute` runs the produced
// binary and propagates its exit code. `importedStdModules` carries the
// program's resolved "std/..." import specifiers (as written), which select
// the native backends (D68) wired into the build.
int buildWithToolchain(const BuildRequest& request);

// Everything `packProject` needs from the frontend half of cmdBuildOrRun:
// pointers into the caller's locals (valid for the duration of the call).
struct PackInput {
    const CliOptions* opt = nullptr;
    const fs::path* entryPath = nullptr;      // as resolved by the CLI
    const std::string* moduleName = nullptr;  // e.g. "src/index"
    const GeneratedModule* module = nullptr;  // generated C++ (cpp/hpp)
    const SourceFile* source = nullptr;       // entry source file
    const Program* program = nullptr;         // parsed entry AST
    bool execute = false;                     // `cp3c run` was requested
};

// `cp3c build` for a `"type": "package"` project (CLI-README §5): validates the
// mandatory `export default`, writes intermediates under .cp3/build/, and
// produces .cp3/out/<name>.cp3pak carrying package.json + generated index.cp3
// + the transpiled C++ under pak/ + any build.files. No toolchain involved.
int packProject(const PackInput& input);

}  // namespace detail

}  // namespace cp3::cli
