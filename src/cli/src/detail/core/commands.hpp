// =============================================================================
// src/cli/src/detail/commands.hpp - the command surface behind runCli (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in the per-concern src/commands/*.cpp files (STYLE.md
// §3/§8: headers declare, `.cpp` defines).
//
// Every entry point returns the documented exit code:
//   0 success | 1 compile error | 2 usage error
// (`run` and `exec` additionally propagate the launched program's exit code.)
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/cli.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/host/native_bindings.hpp"
#include "cp3/host/options.hpp"
#include "cp3/core/source.hpp"

#include "detail/core/diagnostics.hpp"
#include "detail/toolchain/pipeline.hpp"
#include "detail/core/util.hpp"

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
    bool reportCycle = false;             // --report: cycle decisions report
    bool showRuntime = false;             // --show-runtime (--show): copy runtime sources
    bool all = false;                     // clean --all
    bool yes = false;                     // init non-interactive
    bool once = false;                    // dev --once (run one pass then exit)
    bool autoSelect = false;              // install --auto (GitHub selector)
    bool interactive = false;             // editor-plugin --interactive
    bool force = false;                   // publish/link/install/editor overwrite
    bool dryRun = false;                  // publish/editor dry run
    bool json = false;                    // machine-readable command output
    bool listLinks = false;               // link --list
    std::string initType;                 // init --type application|package
    std::string initTypeSystem;           // init --type-system typescript|cpp|rust
    std::string outDir;                   // publish --out <dir|file>
    std::string name;                     // link --name <name>
    std::string editor;                   // editor-plugin --editor <id|path>
    std::string vsix;                     // editor-plugin --vsix <path>
    std::string extensionsDir;            // editor-plugin --extensions-dir <dir>
    std::string out;                      // editor-plugin --out <file-or-dir>
    std::string toolchain;                // --toolchain <id[@version]|path>
    bool strictToolchain = false;         // --strict-toolchain
    bool projectScope = false;            // toolchain --project
    bool globalScope = false;             // toolchain --global
    // Release-build overrides (detail/toolchain/toolchain.hpp). `cp3 build`
    // selects the release profile by default; these flags let a caller opt out
    // of individual optimizations without falling back to the incremental
    // profile. `run`/`dev` keep the fast incremental defaults.
    bool noLto = false;                   // --no-lto
    bool noNativeIsa = false;             // --no-native
    bool noUnrollLoops = false;           // --no-unroll
    bool strictFp = false;                // --strict-fp (keep math errno)
    bool noPgo = false;                   // --no-pgo (disable profile autodetect)
    bool pgoGenerate = false;             // --pgo-generate[=<dir>]
    std::string pgoProfile;               // --pgo <file.profdata>
    std::string pgoGenerateDir;           // --pgo-generate=<dir>
    std::string optLevel;                 // --opt-level <0-3|s|z>
    // Internal: `exec` builds a package's declared tool. The package manifest
    // says `"type": "package"`, but the tool itself is a runnable program, so
    // build/run must treat it as an application rather than pack it.
    bool execTool = false;
    // Internal: `test` builds each test file as a runnable program even
    // inside a `"type": "package"` project, so `cp3 test` works for
    // packages (the test binary imports the package entry).
    bool testMode = false;
};

// -----------------------------------------------------------------------------
// Shared command helpers.
// -----------------------------------------------------------------------------

// Resolves the default entry file for `build` with no explicit file argument:
// package.json "main", then the conventional entry names.
std::string defaultEntryFile();

// Builds the render options for a command (TTY/--verbose -> pretty + color).
RenderOptions makeRenderOptions(const CliOptions& opt, std::string fallbackPath);

// Short phase updates for an interactive `cp3c run`. Kept on stderr so the
// program's stdout remains byte-for-byte its own output; non-TTY and verbose
// invocations already have their own output policy.
void reportRunProgress(bool execute, bool verbose, const char* phase);

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
int cmdTest(const CliOptions& opt);
// Interactive family, version and scope flow, shared by `cp3c toolchain`.
int runToolchainWizard(const CliOptions& opt);

// Machine-readable `cp3c toolchain list --json`.
int toolchainListJson();
int cmdExec(const CliOptions& opt);
int cmdInstall(const CliOptions& opt);
int cmdPublish(const CliOptions& opt);
int cmdLink(const CliOptions& opt);
int cmdUnlink(const CliOptions& opt);
int cmdRecompile(const CliOptions& opt);
int cmdEditorPlugin(const CliOptions& opt);
int cmdLsp(const CliOptions& opt);
int cmdDap(const CliOptions& opt);
int cmdDocs(const std::vector<std::string>& args);
// `native-install` owns its arguments (SDK paths and repeatable options) and
// therefore receives the raw token list, including the command word.
int cmdNativeInstall(const std::vector<std::string>& args);

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

    // One implementation unit of an installed package. The package's generated
    // `index.cp3` is only a declaration surface; the real definitions live in
    // either a shipped cached object (`object`, used as is) or the archive's
    // `pak/` C++ source (`source`, compiled here when no cache exists).
    struct PackageImplementation {
        fs::path source;                 // empty when `cached` is true
        fs::path object;                 // installed cached artifact (.o)
        std::vector<fs::path> includeDirs;
        std::string sourceKey;
        bool cached = false;
    };

    const CliOptions* opt = nullptr;
    const fs::path* entry = nullptr;         // as resolved by the CLI
    const std::string* moduleName = nullptr;  // e.g. "src/index"
    const GeneratedModule* module = nullptr;  // generated C++ (cpp/hpp)
    const std::vector<std::string>* importedStdModules = nullptr;  // D68 selection
    const std::vector<ExtraModule>* extraModules = nullptr;  // worker modules
    // Installed packages' `pak/` C++ implementations (declaration surfaces are
    // linked against these, never regenerated as stubs).
    const std::vector<PackageImplementation>* packageImplementations = nullptr;
    // The native bindings the program imports (`import native "id" as alias`)
    // travel with the compile options: their include roots, defines,
    // libraries and runtime DLLs join the build.
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
    // The native bindings the module graph resolved (`import native "<id>"`).
    // Packing embeds their metadata and assets so the archive is buildable
    // without a separate platform bundle.
    const std::vector<NativeBinding>* nativeBindings = nullptr;
    bool execute = false;                     // `cp3c run` was requested
};

// `cp3c build` for a `"type": "package"` project (CLI-README §5): validates the
// mandatory `export default`, writes intermediates under .cp3/build/, and
// produces .cp3/out/<name>.cp3pak carrying package.json + generated index.cp3
// + the transpiled C++ under pak/ + any build.files. No toolchain involved.
int packProject(const PackInput& input);

}  // namespace detail

}  // namespace cp3::cli
