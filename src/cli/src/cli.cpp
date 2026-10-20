// =============================================================================
// src/cli/src/cli.cpp - the `cp3c` command surface: entry, dispatch, top-level usage.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::runCli (cp3/cli.hpp).
//
// Responsibilities: argument parsing, the global commands, and dispatch to the
// per-concern command implementations (src/commands/*.cpp). The frontend stages
// are wired in through the frozen boundaries; rendering is owned by the CLI
// (DECISIONS D2).
//
// Exit codes (also relied on by tests/runner/run_tests.py):
//   0 success | 1 compile error | 2 usage error | 3 not implemented
// `run` additionally propagates the built program's own exit code.
// =============================================================================
#include "cp3/cli.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cp3/version.hpp"

#include "detail/commands.hpp"
#include "detail/editor_plugin.hpp"
#include "detail/scripts.hpp"
#include "detail/toolchain.hpp"
#include "detail/util.hpp"

namespace cp3 {
namespace {

void printUsage(std::FILE* out) {
    std::fputs(
        "cp3c - the C+++ (.cp3) reference compiler\n"
        "\n"
        "Usage:\n"
        "  cp3c <command> [options] [--] [args]\n"
        "\n"
        "Commands:\n"
        "  run <file.cp3>     Build then execute a program\n"
        "  build [file.cp3]   Transpile + link, or pack (\"type\": \"package\")\n"
        "  check <file.cp3>   Frontend only: parse + type-check (no C++ generation)\n"
        "  dev [file.cp3]     Watch mode: re-check on save (--once for a single pass)\n"
        "  fmt <file.cp3>     Zero-config formatter\n"
        "  clean              Remove .cp3/build and .cp3/out (--all clears .cp3/packages)\n"
        "  init [dir]         Scaffold a new project (package.json + src/index.cp3)\n"
        "  toolchain          Choose the C++ compiler (list/detect/use/clear)\n"
        "  install [src]      Install a .cp3pak / package dir (bare: package.json deps)\n"
        "  editor-plugin      Editor extension wizard/actions (alias editor-plugins)\n"
        "  exec <pkg> [args]  Run a package's bundled tool\n"
        "  lsp [--stdio]      Language server on stdio (for editor integrations)\n"
        "  dap [--stdio]      Debug adapter on stdio (for editor debugging)\n"
        "  version            Print the compiler version\n"
        "  help               Print this help\n"
        "\n"
        "Options:\n"
        "  -h, --help            Print this help\n"
        "  -v, --version         Print the compiler version\n"
        "      --release         (build/run) whole-program build with LTO\n"
        "      --debug           (build/run) -O0 plus debug info\n"
        "      --verbose         Show toolchain choice and pipeline steps\n"
        "      --toolchain <id|path>  Use this compiler for one command\n"
        "      --project         (toolchain) save the choice in package.json\n"
        "      --global          (toolchain) save the choice for all projects\n"
        "      --max-memory <MB|N%>  Cap codegen-phase memory (default: 70% of system RAM)\n"
        "      --all             (clean) also remove .cp3/packages\n"
        "      --yes, -y         (init) accept defaults, never prompt\n"
        "      --type <t>        (init) pre-select project type (application|package)\n"
        "      --auto            (install) auto-pick a GitHub release asset\n"
        "      --once            (dev) run one watch pass and exit\n"
        "      --json, --interactive, --dry-run, --force  (editor-plugin) output/prompts\n"
        "      --editor <id|path>, --vsix <path>  (editor-plugin) target and package\n"
        "      --extensions-dir <dir>, --out <path>  (editor-plugin) overrides\n"
        "\n"
        "Exit codes:\n"
        "  0 success   1 compile error   2 usage error   3 not implemented\n",
        out);
}

bool startsWithDash(std::string_view s) { return !s.empty() && s[0] == '-'; }

std::vector<std::string> collectArgs(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc) : 0);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }
    return args;
}

// Parses "<MB>" or "<N%>" into a megabyte cap. Returns false when malformed.
bool parseMaxMemory(const std::string& value, std::uint64_t& outMb) {
    if (value.empty()) return false;
    if (value.back() == '%') {
        const std::string number = value.substr(0, value.size() - 1);
        if (number.empty()) return false;
        char* end = nullptr;
        const double pct = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || *end != '\0' || pct <= 0.0) return false;
        const std::uint64_t total = cp3::cli::totalPhysicalMemoryMb();
        if (total == 0) return false;
        outMb = static_cast<std::uint64_t>(static_cast<double>(total) * pct / 100.0);
        if (outMb == 0) outMb = 1;
        return true;
    }
    char* end = nullptr;
    const long mb = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || mb <= 0) return false;
    outMb = static_cast<std::uint64_t>(mb);
    return true;
}

// Simple boolean flags shared by every command. Returns true when `t` matched.
bool matchFlag(const std::string& t, cp3::cli::CliOptions& opt, bool& sawRelease,
               bool& sawDebug) {
    if (t == "--release") {
        sawRelease = true;
        opt.compile.profile = cp3::BuildProfile::Release;
        return true;
    }
    if (t == "--debug") {
        sawDebug = true;
        opt.compile.profile = cp3::BuildProfile::Debug;
        return true;
    }
    if (t == "--verbose" || t == "-v") { opt.verbose = true; return true; }
    if (t == "--all") { opt.all = true; return true; }
    if (t == "--yes" || t == "-y") { opt.yes = true; return true; }
    if (t == "--once") { opt.once = true; return true; }
    if (t == "--auto") { opt.autoSelect = true; return true; }
    if (cp3::cli::matchEditorFlag(t, opt)) return true;
    if (t == "--project") { opt.projectScope = true; return true; }
    if (t == "--global") { opt.globalScope = true; return true; }
    return false;
}

// Handles the value-taking options (--max-memory, --type). Returns true when
// `t` was recognized; a malformed value is reported through `err` (the caller
// aborts). `i` is advanced past a separate value token.
bool parseValueOption(const std::vector<std::string>& tokens, std::size_t& i,
                      cp3::cli::CliOptions& opt, std::string& err) {
    const std::string& t = tokens[i];
    if (cp3::cli::matchEditorValueOption(tokens, i, opt, err)) return true;
    if (t == "--max-memory" || cp3::cli::startsWith(t, "--max-memory=")) {
        std::string value;
        if (t == "--max-memory") {
            if (i + 1 >= tokens.size()) {
                err = "--max-memory requires a value (<MB> or <N%>)";
                return true;
            }
            value = tokens[++i];
        } else {
            value = t.substr(std::strlen("--max-memory="));
        }
        std::uint64_t mb = 0;
        if (!parseMaxMemory(value, mb)) {
            err = "invalid --max-memory value '" + value + "'";
            return true;
        }
        opt.compile.maxMemoryMb = mb;
        return true;
    }
    if (t == "--type") {
        if (i + 1 >= tokens.size()) {
            err = "--type requires a value (application|package)";
            return true;
        }
        const std::string value = tokens[++i];
        if (value != "application" && value != "package") {
            err = "invalid --type '" + value +
                  "' (expected application or package)";
            return true;
        }
        opt.initType = value;
        return true;
    }
    if (t == "--toolchain" || cp3::cli::startsWith(t, "--toolchain=")) {
        if (t == "--toolchain") {
            if (i + 1 >= tokens.size()) {
                err = "--toolchain requires an id or path";
                return true;
            }
            opt.toolchain = tokens[++i];
        } else {
            opt.toolchain = t.substr(std::strlen("--toolchain="));
        }
        if (opt.toolchain.empty()) err = "--toolchain requires an id or path";
        return true;
    }
    return false;
}

// Parses the tokens following the command word into `opt`. Returns an empty
// string on success or an error message on a usage problem.
std::string parseOptions(const std::string& command,
                         const std::vector<std::string>& tokens,
                         cp3::cli::CliOptions& opt) {
    bool sawRelease = false;
    bool sawDebug = false;
    bool afterSeparator = false;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];

        if (afterSeparator) {
            opt.positionals.push_back(t);
            continue;
        }
        if (t == "--") {
            afterSeparator = true;
            continue;
        }
        if (matchFlag(t, opt, sawRelease, sawDebug)) continue;

        std::string err;
        if (parseValueOption(tokens, i, opt, err)) {
            if (!err.empty()) return err;
            continue;
        }
        if (startsWithDash(t)) {
            return "unknown option '" + t + "'";
        }
        opt.positionals.push_back(t);
    }

    if (sawRelease && sawDebug) {
        return "--release and --debug are mutually exclusive";
    }
    if (opt.projectScope && opt.globalScope) {
        return "--project and --global are mutually exclusive";
    }
    if ((opt.projectScope || opt.globalScope) && command != "toolchain") {
        return "--project and --global only apply to 'cp3c toolchain'";
    }
    (void)command;
    return {};
}

// Recognizes `first` as a command word (or a bare `.cp3` path meaning `run`).
bool resolveCommand(const std::string& first, std::string& command,
                    bool& filePreseeded) {
    if (!startsWithDash(first) && cp3::cli::endsWith(first, ".cp3")) {
        command = "run";
        filePreseeded = true;
        return true;
    }
    if (first == "run" || first == "build" || first == "check" ||
        first == "dev" || first == "fmt" || first == "clean" ||
        first == "init" || first == "exec" || first == "install" ||
        first == "editor-plugin" || first == "editor-plugins" ||
        first == "lsp" || first == "dap" || first == "toolchain") {
        command = first;
        return true;
    }
    return false;
}

// Maps positionals to file / program args. Returns an empty string on success
// or a usage-error message (without the "cp3c: " prefix).
std::string resolveFile(const std::string& command, bool filePreseeded,
                        const std::string& first, cp3::cli::CliOptions& opt) {
    const bool takesFile = (command == "run" || command == "build" ||
                            command == "check" || command == "dev" ||
                            command == "fmt");
    if (filePreseeded) {
        opt.file = first;
        // Bare `cp3c foo.cp3 a b` passes the extras to the program.
        for (const std::string& extra : opt.positionals) {
            opt.programArgs.push_back(extra);
        }
        opt.positionals.clear();
    } else if (takesFile) {
        if (!opt.positionals.empty()) {
            opt.file = opt.positionals.front();
            opt.positionals.erase(opt.positionals.begin());
            if (command == "run") {
                for (const std::string& extra : opt.positionals) {
                    opt.programArgs.push_back(extra);
                }
                opt.positionals.clear();
            } else if (!opt.positionals.empty()) {
                return "unexpected extra argument '" + opt.positionals.front() +
                       "'";
            }
        }
    } else if (command == "clean" && !opt.positionals.empty()) {
        return "'clean' takes no file argument";
    } else if (command == "install" && opt.positionals.size() > 1) {
        return "'install' takes at most one path or URL";
    }
    return {};
}

// `lsp` owns a tiny argument surface (PKG-CONTRACTS sec.2): `--stdio` is the
// only accepted token (the transport is stdio with or without it) and anything
// else is a usage error. It bypasses the package.json hooks on purpose: an
// editor-spawned server must never run project scripts.
int runLsp(const std::vector<std::string>& args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--stdio") continue;
        std::fprintf(stderr, "cp3c: unknown option '%s'\n", args[i].c_str());
        std::fputs("usage: cp3c lsp [--stdio]\n", stderr);
        return kExitUsageError;
    }
    cp3::cli::CliOptions opt;
    return cp3::cli::cmdLsp(opt);
}

// `dap` owns the same tiny argument surface for the same reason: an
// editor-spawned debug adapter must never run project scripts or parse
// build options - the launch request carries everything it needs.
int runDap(const std::vector<std::string>& args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--stdio") continue;
        std::fprintf(stderr, "cp3c: unknown option '%s'\n", args[i].c_str());
        std::fputs("usage: cp3c dap [--stdio]\n", stderr);
        return kExitUsageError;
    }
    cp3::cli::CliOptions opt;
    return cp3::cli::cmdDap(opt);
}

int dispatchBuiltin(const std::string& command, cp3::cli::CliOptions& opt);

int reportHookFailure(const std::string& name, int rc) {
    if (rc == -2) {
        std::fprintf(stderr, "cp3c: could not start a shell for script '%s'\n",
                     name.c_str());
        return cp3::kExitCompileError;
    }
    std::fprintf(stderr, "cp3c: script '%s' failed (exit %d)\n", name.c_str(),
                 rc);
    return rc;
}

// Runs the package.json lifecycle hooks around the built-in command
// (CLI-README §3). A failing `pre` hook aborts the command; `post` runs only
// after a successful command, so a failed build never triggers a follow-up.
int dispatchCommand(const std::string& command, cp3::cli::CliOptions& opt) {
    const bool hooks =
        cp3::cli::hookableCommand(command) && !cp3::cli::hooksSuppressed();
    if (hooks) {
        const int rc = cp3::cli::runScriptHook("pre" + command);
        if (rc == -2 || rc > 0) return reportHookFailure("pre" + command, rc);
    }
    const int result = dispatchBuiltin(command, opt);
    if (hooks && result == cp3::kExitSuccess) {
        const int rc = cp3::cli::runScriptHook("post" + command);
        if (rc == -2 || rc > 0) return reportHookFailure("post" + command, rc);
    }
    return result;
}

int dispatchBuiltin(const std::string& command, cp3::cli::CliOptions& opt) {
    if (command == "check") {
        if (opt.file.empty()) {
            std::fprintf(stderr, "cp3c: check needs a .cp3 file\n");
            printUsage(stderr);
            return cp3::kExitUsageError;
        }
        return cp3::cli::cmdCheck(opt);
    }

    if (command == "run" || command == "build") {
        if (opt.file.empty()) {
            if (command == "build") {
                opt.file = cp3::cli::defaultEntryFile();
            }
            if (opt.file.empty()) {
                std::fprintf(stderr, "cp3c: %s needs a .cp3 file\n",
                             command.c_str());
                printUsage(stderr);
                return cp3::kExitUsageError;
            }
        }
        return cp3::cli::cmdBuildOrRun(opt, command == "run");
    }

    if (command == "dev") {
        if (opt.file.empty()) {
            opt.file = cp3::cli::defaultEntryFile();
        }
        return cp3::cli::cmdDev(opt);
    }
    if (command == "fmt") {
        return cp3::cli::cmdFmt(opt);
    }
    if (command == "clean") {
        return cp3::cli::cmdClean(opt);
    }
    if (command == "init") {
        return cp3::cli::cmdInit(opt);
    }
    if (command == "toolchain") {
        return cp3::cli::cmdToolchain(opt);
    }
    if (command == "install") {
        return cp3::cli::cmdInstall(opt);
    }
    if (command == "editor-plugin" || command == "editor-plugins") {
        return cp3::cli::cmdEditorPlugin(opt);
    }
    if (command == "exec") {
        return cp3::cli::cmdExec(opt);
    }

    // Unreachable: the command word was validated above.
    printUsage(stderr);
    return cp3::kExitUsageError;
}

}  // namespace

int runCli(int argc, char** argv) {
    const std::vector<std::string> args = collectArgs(argc, argv);

    // No arguments: friendly usage, success (ARCHITECTURE §4).
    if (args.empty()) {
        printUsage(stdout);
        return kExitSuccess;
    }

    const std::string first = args[0];

    if (first == "-v" || first == "--version" || first == "version") {
        std::fputs(versionString().c_str(), stdout);
        std::fputc('\n', stdout);
        return kExitSuccess;
    }

    if (first == "-h" || first == "--help" || first == "help") {
        printUsage(stdout);
        return kExitSuccess;
    }

    // Determine the command word (a bare `.cp3` path means `run`).
    std::string command;
    bool filePreseeded = false;
    if (!resolveCommand(first, command, filePreseeded)) {
        std::fprintf(stderr, "cp3c: unknown %s '%s'\n",
                     startsWithDash(first) ? "option" : "command", first.c_str());
        printUsage(stderr);
        return kExitUsageError;
    }

    // `lsp` and `dap` validate their own flags and never run package.json
    // hooks.
    if (command == "lsp") {
        return runLsp(args);
    }
    if (command == "dap") {
        return runDap(args);
    }

    std::vector<std::string> rest(args.begin() + 1, args.end());
    cp3::cli::CliOptions opt;
    const std::string parseError = parseOptions(command, rest, opt);
    if (!parseError.empty()) {
        std::fprintf(stderr, "cp3c: %s\n", parseError.c_str());
        printUsage(stderr);
        return kExitUsageError;
    }

    // Positional -> file / command-specific payload.
    const std::string fileError = resolveFile(command, filePreseeded, first, opt);
    if (!fileError.empty()) {
        std::fprintf(stderr, "cp3c: %s\n", fileError.c_str());
        printUsage(stderr);
        return kExitUsageError;
    }

    // Apply the toolchain override and the project-scope search start before
    // any command can probe the compiler. The project search starts at the
    // entry file's directory (the working directory when there is no entry),
    // so a project choice never leaks onto a file outside that project.
    cp3::cli::setToolchainOverride(opt.toolchain);
    {
        std::error_code ec;
        cp3::cli::fs::path start = cp3::cli::fs::current_path(ec);
        if (!opt.file.empty()) {
            const cp3::cli::fs::path entry(opt.file);
            if (entry.has_parent_path()) start = entry.parent_path();
        }
        cp3::cli::setToolchainProjectDirectory(start);
    }

    return dispatchCommand(command, opt);
}

}  // namespace cp3
