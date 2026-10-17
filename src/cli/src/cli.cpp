// =============================================================================
// cli/cli.cpp — the `cp3c` command surface: entry, dispatch, top-level usage.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::runCli (cp3/cli.hpp).
//
// Responsibilities: argument parsing, the global commands, and dispatch to the
// per-concern command implementations (src/commands_*.cpp). The frontend stages
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

#include "private/commands.hpp"
#include "private/util.hpp"

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
        "  build [file.cp3]   Transpile + link without executing\n"
        "  check <file.cp3>   Frontend only: parse + type-check (no C++ generation)\n"
        "  dev [file.cp3]     Watch mode: re-check on save (--once for a single pass)\n"
        "  fmt <file.cp3>     Zero-config formatter\n"
        "  clean              Remove .cp3/build and .cp3/out (--all clears .cp3/packages)\n"
        "  init [dir]         Scaffold a new project (package.json + src/main.cp3)\n"
        "  exec <pkg> [args]  Run a package's bundled tool\n"
        "  version            Print the compiler version\n"
        "  help               Print this help\n"
        "\n"
        "Options:\n"
        "  -h, --help            Print this help\n"
        "  -v, --version         Print the compiler version\n"
        "      --release         (build/run) whole-program build with LTO\n"
        "      --debug           (build/run) -O0 plus debug info\n"
        "      --verbose         Show toolchain choice and pipeline steps\n"
        "      --max-memory <MB|N%>  Cap codegen-phase memory (default: 70% of system RAM)\n"
        "      --all             (clean) also remove .cp3/packages\n"
        "      --yes, -y         (init) accept defaults, never prompt\n"
        "      --once            (dev) run one watch pass and exit\n"
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

// Minimal `"main": "path"` extraction from package.json (default entry lookup).
std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t kpos = json.find(needle);
    if (kpos == std::string::npos) return {};
    std::size_t i = kpos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                               json[i] == '\r' || json[i] == '\n')) {
        ++i;
    }
    if (i >= json.size() || json[i] != ':') return {};
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                               json[i] == '\r' || json[i] == '\n')) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') return {};
    ++i;
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            ++i;
        }
        out.push_back(json[i]);
        ++i;
    }
    return out;
}

// For `build` with no explicit file: package.json "main", then common defaults.
std::string defaultEntryFile() {
    namespace fs = cp3::cli::fs;
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    std::string pkg;
    if (cp3::cli::readTextFile(cwd / "package.json", pkg)) {
        const std::string main = extractJsonString(pkg, "main");
        if (!main.empty()) {
            const fs::path candidate = cwd / main;
            if (fs::exists(candidate, ec)) return candidate.string();
        }
    }
    for (const char* rel : {"src/main.cp3", "src/index.cp3", "index.cp3",
                            "main.cp3"}) {
        const fs::path candidate = cwd / rel;
        if (fs::exists(candidate, ec)) return candidate.string();
    }
    return {};
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
        if (t == "--release") {
            sawRelease = true;
            opt.compile.profile = cp3::BuildProfile::Release;
            continue;
        }
        if (t == "--debug") {
            sawDebug = true;
            opt.compile.profile = cp3::BuildProfile::Debug;
            continue;
        }
        if (t == "--verbose" || t == "-v") {
            opt.verbose = true;
            continue;
        }
        if (t == "--all") {
            opt.all = true;
            continue;
        }
        if (t == "--yes" || t == "-y") {
            opt.yes = true;
            continue;
        }
        if (t == "--once") {
            opt.once = true;
            continue;
        }
        if (t == "--max-memory") {
            if (i + 1 >= tokens.size()) {
                return "--max-memory requires a value (<MB> or <N%>)";
            }
            std::uint64_t mb = 0;
            if (!parseMaxMemory(tokens[++i], mb)) {
                return "invalid --max-memory value '" + tokens[i] + "'";
            }
            opt.compile.maxMemoryMb = mb;
            continue;
        }
        if (cp3::cli::startsWith(t, "--max-memory=")) {
            std::uint64_t mb = 0;
            const std::string value = t.substr(std::strlen("--max-memory="));
            if (!parseMaxMemory(value, mb)) {
                return "invalid --max-memory value '" + value + "'";
            }
            opt.compile.maxMemoryMb = mb;
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
        first == "init" || first == "exec") {
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
    }
    return {};
}

int dispatchCommand(const std::string& command, cp3::cli::CliOptions& opt) {
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
                opt.file = defaultEntryFile();
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
            opt.file = defaultEntryFile();
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

    return dispatchCommand(command, opt);
}

}  // namespace cp3
