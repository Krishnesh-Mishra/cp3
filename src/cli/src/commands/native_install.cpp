// =============================================================================
// src/cli/src/commands/native_install.cpp - `cp3c native-install` entry point.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdNativeInstall.
//
// This file owns the command-line surface. The discovery, libclang bind, and
// metadata emission live in native_install_support.cpp.
// =============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "detail/commands.hpp"
#include "detail/native_install.hpp"

namespace cp3::cli {
namespace {

bool takeValue(const std::vector<std::string>& args, std::size_t& i,
               std::string& value, std::string& error) {
    const std::string& token = args[i];
    const std::size_t eq = token.find('=');
    if (eq != std::string::npos) {
        value = token.substr(eq + 1);
        return true;
    }
    if (i + 1 >= args.size()) {
        error = "option '" + token + "' requires a value";
        return false;
    }
    value = args[++i];
    return true;
}

bool matches(const std::string& token, const char* name) {
    const std::string prefix = std::string(name) + "=";
    return token == name || token.rfind(prefix, 0) == 0;
}

bool parseOptions(const std::vector<std::string>& args, NativeOptions& out,
                  std::string& error) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& token = args[i];
        std::string value;
        if (matches(token, "--include")) {
            if (!takeValue(args, i, value, error)) return false;
            out.includes.emplace_back(value);
        } else if (matches(token, "--define")) {
            if (!takeValue(args, i, value, error)) return false;
            out.defines.push_back(value);
        } else if (matches(token, "--lib-dir")) {
            if (!takeValue(args, i, value, error)) return false;
            out.libDirs.emplace_back(value);
        } else if (matches(token, "--link")) {
            if (!takeValue(args, i, value, error)) return false;
            out.links.push_back(value);
        } else if (matches(token, "--name")) {
            if (!takeValue(args, i, value, error)) return false;
            out.name = value;
        } else if (matches(token, "--out")) {
            if (!takeValue(args, i, value, error)) return false;
            out.out = value;
        } else if (token == "--force") {
            out.force = true;
        } else if (token == "--dry-run") {
            out.dryRun = true;
        } else if (token == "--json") {
            out.json = true;
        } else if (token == "--verbose") {
            out.verbose = true;
        } else if (token == "--help" || token == "-h") {
            out.help = true;
        } else if (token == "--") {
            for (++i; i < args.size(); ++i) {
                if (!out.input.empty()) {
                    error = "unexpected extra argument '" + args[i] + "'";
                    return false;
                }
                out.input = args[i];
            }
        } else if (!token.empty() && token[0] == '-') {
            error = "unknown option '" + token + "'";
            return false;
        } else if (out.input.empty()) {
            out.input = token;
        } else {
            error = "unexpected extra argument '" + token + "'";
            return false;
        }
    }
    if (out.help) return true;
    if (out.input.empty()) {
        error = "native-install needs an SDK path or package name";
        return false;
    }
    return true;
}

}  // namespace

void printNativeInstallUsage(std::FILE* out) {
    std::fputs(
        "usage: cp3c native-install <sdk-path-or-name> [options]\n"
        "  --include <dir>        extra include directory (repeatable)\n"
        "  --define <name[=val]>  preprocessor definition (repeatable)\n"
        "  --lib-dir <dir>        library search directory (repeatable)\n"
        "  --link <name>          link library (repeatable)\n"
        "  --name <id>            binding id (default: package.json name)\n"
        "  --out <dir>            binding root (default: .cp3/native)\n"
        "  --force                overwrite an existing binding\n"
        "  --dry-run              print the plan without writing files\n"
        "  --json                 stable machine-readable output\n"
        "  --verbose              show discovery details\n"
        "  --help, -h             show this help\n"
        "\n"
        "exit codes:\n"
        "  0 success   1 bind or write failure   2 usage error\n",
        out);
}

int cmdNativeInstall(const std::vector<std::string>& args) {
    NativeOptions options;
    std::string parseError;
    if (!parseOptions(args, options, parseError)) {
        std::fprintf(stderr, "cp3c: %s\n", parseError.c_str());
        printNativeInstallUsage(stderr);
        return kExitUsageError;
    }
    if (options.help) {
        printNativeInstallUsage(stdout);
        return kExitSuccess;
    }
    return runNativeInstall(options);
}

}  // namespace cp3::cli
