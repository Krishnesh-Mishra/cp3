// =============================================================================
// src/cli/src/commands/arguments.cpp - command-line option parsing DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/arguments.hpp.
//
// Kept apart from the dispatcher (src/cli.cpp) so each unit stays inside the
// STYLE.md file-size budget: this file owns flags and value options, cli.cpp
// owns command routing and usage.
// =============================================================================
#include "detail/core/arguments.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "detail/core/util.hpp"
#include "detail/editor/editor_plugin.hpp"

namespace cp3::cli {

namespace {

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
    if (t == "--show-runtime" || t == "--show") {
        opt.showRuntime = true;
        return true;
    }
    if (t == "--all") { opt.all = true; return true; }
    if (t == "--yes" || t == "-y") { opt.yes = true; return true; }
    if (t == "--once") { opt.once = true; return true; }
    if (t == "--auto") { opt.autoSelect = true; return true; }
    if (cp3::cli::matchEditorFlag(t, opt)) return true;
    if (t == "--strict-toolchain") { opt.strictToolchain = true; return true; }
    if (t == "--force") { opt.force = true; return true; }
    if (t == "--dry-run") { opt.dryRun = true; return true; }
    if (t == "--json") { opt.json = true; return true; }
    if (t == "--list") { opt.listLinks = true; return true; }
    if (t == "--project") { opt.projectScope = true; return true; }
    if (t == "--global") { opt.globalScope = true; return true; }
    return false;
}

// Handles the package/init value-taking options. Split out of
// parseValueOption to keep both functions inside the STYLE.md budget.
bool parsePackageValueOption(const std::vector<std::string>& tokens,
                             std::size_t& i, cp3::cli::CliOptions& opt,
                             std::string& err) {
    const std::string& t = tokens[i];
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
    if (t == "--type-system" || cp3::cli::startsWith(t, "--type-system=")) {
        std::string value;
        if (t == "--type-system") {
            if (i + 1 >= tokens.size()) {
                err = "--type-system requires a value (typescript|cpp|rust)";
                return true;
            }
            value = tokens[++i];
        } else {
            value = t.substr(std::strlen("--type-system="));
        }
        if (value != "typescript" && value != "cpp" && value != "rust") {
            err = "invalid --type-system '" + value +
                  "' (expected typescript, cpp or rust)";
            return true;
        }
        opt.initTypeSystem = value;
        return true;
    }
    if (t == "--out" || cp3::cli::startsWith(t, "--out=")) {
        if (t == "--out") {
            if (i + 1 >= tokens.size()) {
                err = "--out requires a directory or file path";
                return true;
            }
            opt.outDir = tokens[++i];
        } else {
            opt.outDir = t.substr(std::strlen("--out="));
        }
        if (opt.outDir.empty()) err = "--out requires a directory or file path";
        return true;
    }
    if (t == "--name" || cp3::cli::startsWith(t, "--name=")) {
        if (t == "--name") {
            if (i + 1 >= tokens.size()) {
                err = "--name requires a value";
                return true;
            }
            opt.name = tokens[++i];
        } else {
            opt.name = t.substr(std::strlen("--name="));
        }
        if (opt.name.empty()) err = "--name requires a value";
        return true;
    }
    return false;
}

// Handles the value-taking options (--max-memory, init/package options,
// --toolchain). Returns true when `t` was recognized; a malformed value is
// reported through `err` (the caller aborts). `i` is advanced past a separate
// value token.
bool parseValueOption(const std::vector<std::string>& tokens, std::size_t& i,
                      cp3::cli::CliOptions& opt, const std::string& command,
                      std::string& err) {
    const std::string& t = tokens[i];
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
    if (command == "editor-plugin" || command == "editor-plugins") {
        if (cp3::cli::matchEditorValueOption(tokens, i, opt, err)) return true;
    } else if (parsePackageValueOption(tokens, i, opt, err)) {
        return true;
    }
    if (t == "--toolchain" || cp3::cli::startsWith(t, "--toolchain=")) {
        if (t == "--toolchain") {
            if (i + 1 >= tokens.size()) {
                err = "--toolchain requires a Clang version or path";
                return true;
            }
            opt.toolchain = tokens[++i];
        } else {
            opt.toolchain = t.substr(std::strlen("--toolchain="));
        }
        if (opt.toolchain.empty()) err = "--toolchain requires a Clang version or path";
        return true;
    }
    return false;
}

}  // namespace

std::string parseOptions(const std::string& command,
                         const std::vector<std::string>& tokens,
                         CliOptions& opt) {
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
        if (parseValueOption(tokens, i, opt, command, err)) {
            if (!err.empty()) return err;
            continue;
        }
        if (!t.empty() && t[0] == '-') {
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
    return {};
}

}  // namespace cp3::cli
