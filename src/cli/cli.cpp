// =============================================================================
// cp3/cli.cpp — `cp3c` command surface.
//
// This is the SCAFFOLD build: argument parsing, `--version`, `--help` and the
// exit-code contract are real; the compile commands (run/build/check/dev/fmt/
// clean/init/exec) are not implemented yet and report kExitNotImplemented with
// a marker line. The frontend modules are wired in behind this surface as they
// come online, one stage at a time.
// =============================================================================
#include "cp3/cli.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/version.hpp"

namespace cp3 {
namespace {

void printUsage(std::FILE* out) {
    std::fputs(
        "cp3c - the C+++ (.cp3) reference compiler\n"
        "\n"
        "Usage:\n"
        "  cp3c <command> [options] [file]\n"
        "\n"
        "Commands:\n"
        "  run <file.cp3>     Build then execute a program\n"
        "  build [file.cp3]   Transpile + link without executing\n"
        "  check <file.cp3>   Frontend only: parse + type-check (no C++ generation)\n"
        "  dev                Watch mode: re-check/rebuild on save\n"
        "  fmt <file.cp3>     Zero-config formatter\n"
        "  clean              Remove the .cp3/build cache (--all also clears .cp3/packages)\n"
        "  init               Scaffold a new project (package.json + src)\n"
        "  exec <pkg> [args]  Run a package's bundled tool\n"
        "  version            Print the compiler version\n"
        "  help               Print this help\n"
        "\n"
        "Options:\n"
        "  -h, --help            Print this help\n"
        "  -v, --version         Print the compiler version\n"
        "  --release             (build/run) whole-program build with LTO\n"
        "  --debug               (build/run) -O0 plus debug info\n"
        "  --max-memory <MB|N%>  Cap codegen-phase memory (default: 70% of system RAM)\n"
        "\n"
        "Exit codes:\n"
        "  0 success   1 compile error   2 usage error   3 not implemented (scaffold build)\n",
        out);
}

// Reports that a recognized-but-unimplemented command was invoked and returns
// the stub exit code. Keeping this in one place keeps the marker consistent.
int notImplemented(const std::string& command) {
    std::fprintf(stderr,
                 "cp3c: '%s' is %s in this scaffold build (CP3002)\n",
                 command.c_str(),
                 kNotImplementedMarker);
    return kExitNotImplemented;
}

bool startsWithDashes(std::string_view s) {
    return s.size() >= 1 && s[0] == '-';
}

}  // namespace

int runCli(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc) : 0);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }

    // No arguments: print usage and succeed (friendly, not an error).
    if (args.empty()) {
        printUsage(stdout);
        return kExitSuccess;
    }

    const std::string& first = args[0];

    if (first == "-v" || first == "--version" || first == "version") {
        std::fputs(versionString().c_str(), stdout);
        std::fputc('\n', stdout);
        return kExitSuccess;
    }

    if (first == "-h" || first == "--help" || first == "help") {
        printUsage(stdout);
        return kExitSuccess;
    }

    // Bare source path: treat as `run` (the everyday case).
    if (!startsWithDashes(first) && first.size() > 4 &&
        first.compare(first.size() - 4, 4, ".cp3") == 0) {
        return notImplemented("run");
    }

    if (first == "run" || first == "build" || first == "check" || first == "dev" ||
        first == "fmt" || first == "clean" || first == "init" || first == "exec") {
        return notImplemented(first);
    }

    // Unknown command or flag.
    std::fprintf(stderr, "cp3c: unknown %s '%s'\n",
                 startsWithDashes(first) ? "option" : "command", first.c_str());
    printUsage(stderr);
    return kExitUsageError;
}

}  // namespace cp3
