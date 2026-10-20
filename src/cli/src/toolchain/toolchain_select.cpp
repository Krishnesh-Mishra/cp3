// =============================================================================
// src/cli/src/toolchain/toolchain_select.cpp - effective toolchain resolution.
//
// OWNERSHIP: cli module (src/cli/**). Implements the resolution half of
// detail/toolchain_select.hpp and cp3::cli::detectToolchain.
//
// Precedence (first source that resolves to an existing driver wins):
//   --toolchain <id|path> > CP3_TOOLCHAIN > project package.json "toolchain"
//   > global toolchain.conf > D12 auto-detect.
// A source that cannot resolve is reported and skipped, never fatal: a build
// whose saved compiler was uninstalled warns once and falls back rather than
// refusing to run. The result is cached until one of the setters resets it.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "detail/package_json.hpp"
#include "detail/toolchain.hpp"
#include "detail/toolchain_select.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

struct SelectionState {
    bool resolved = false;
    Toolchain toolchain;
    ToolchainSource source = ToolchainSource::None;
    std::string origin;        // flag/env value, or the file that supplied it
    std::string warning;
    std::string overrideValue; // --toolchain
    fs::path projectDir;       // search start for package.json
    bool projectDirSet = false;
};

SelectionState& state() {
    static SelectionState current;
    return current;
}

struct Attempt {
    bool ok = false;
    Toolchain toolchain;
    std::string error;
};

Attempt attemptValue(const std::string& value) {
    Attempt attempt;
    attempt.ok = resolveToolchainValue(value, attempt.toolchain, attempt.error);
    return attempt;
}

std::string joinProblems(const std::vector<std::string>& problems) {
    std::string out;
    for (const std::string& problem : problems) {
        if (!out.empty()) out += "; ";
        out += problem;
    }
    return out;
}

fs::path projectSearchStart() {
    std::error_code ec;
    if (state().projectDirSet) return state().projectDir;
    return fs::current_path(ec);
}

Attempt attemptProject(std::string& origin,
                       std::vector<std::string>& problems) {
    fs::path root;
    if (!findPackageRoot(projectSearchStart(), root)) return {};
    std::string value;
    std::string error;
    if (!readProjectToolchain(root, value, error)) {
        problems.push_back(error);
        return {};
    }
    if (value.empty()) return {};
    origin = (root / "package.json").string();
    Attempt attempt = attemptValue(value);
    if (!attempt.ok) {
        problems.push_back("project package.json toolchain '" + value +
                           "': " + attempt.error);
    }
    return attempt;
}

Attempt attemptGlobal(std::string& origin,
                      std::vector<std::string>& problems) {
    const GlobalToolchainConfig config = readGlobalToolchainConfig();
    if (!config.present) return {};
    origin = toolchainConfigPath().string();
    std::error_code ec;
    if (!config.program.empty() &&
        fs::is_regular_file(config.program, ec)) {
        Attempt attempt = attemptValue(config.program);
        if (attempt.ok) return attempt;
        problems.push_back("saved compiler '" + config.program +
                           "' is not usable: " + attempt.error);
    } else if (!config.program.empty()) {
        problems.push_back("saved compiler '" + config.program +
                           "' no longer exists");
    }
    if (!config.id.empty()) {
        Attempt attempt = attemptValue(config.id);
        if (attempt.ok) {
            if (!config.program.empty()) {
                problems.push_back("re-probed '" + config.id + "' at " +
                                   attempt.toolchain.program);
            }
            return attempt;
        }
        problems.push_back("saved toolchain '" + config.id +
                           "' is not available: " + attempt.error);
    }
    return {};
}

void adopt(const Toolchain& toolchain, ToolchainSource source,
           const std::string& origin) {
    SelectionState& current = state();
    current.toolchain = toolchain;
    current.source = source;
    current.origin = origin;
}

void resolveNow() {
    SelectionState& current = state();
    std::vector<std::string> problems;
    bool done = false;

    if (!current.overrideValue.empty()) {
        const Attempt attempt = attemptValue(current.overrideValue);
        if (attempt.ok) {
            adopt(attempt.toolchain, ToolchainSource::Flag,
                  current.overrideValue);
            done = true;
        } else {
            problems.push_back("--toolchain " + current.overrideValue + ": " +
                               attempt.error);
        }
    }

    if (!done) {
        if (const char* fromEnv = std::getenv("CP3_TOOLCHAIN")) {
            if (*fromEnv != '\0') {
                const Attempt attempt = attemptValue(fromEnv);
                if (attempt.ok) {
                    adopt(attempt.toolchain, ToolchainSource::Environment,
                          fromEnv);
                    done = true;
                } else {
                    problems.push_back(std::string("CP3_TOOLCHAIN=") + fromEnv +
                                       ": " + attempt.error);
                }
            }
        }
    }

    if (!done) {
        std::string origin;
        const Attempt attempt = attemptProject(origin, problems);
        if (attempt.ok) {
            adopt(attempt.toolchain, ToolchainSource::Project, origin);
            done = true;
        }
    }

    if (!done) {
        std::string origin;
        const Attempt attempt = attemptGlobal(origin, problems);
        if (attempt.ok) {
            adopt(attempt.toolchain, ToolchainSource::Global, origin);
            done = true;
        }
    }

    if (!done) {
        const Toolchain automatic = probeAutoToolchain();
        if (automatic.found()) {
            adopt(automatic, ToolchainSource::Auto, "");
        } else {
            current.toolchain = Toolchain{};
            current.source = ToolchainSource::None;
            current.origin.clear();
        }
    }
    current.warning = joinProblems(problems);
}

}  // namespace

const Toolchain& detectToolchain() {
    SelectionState& current = state();
    if (!current.resolved) {
        resolveNow();
        current.resolved = true;
        if (!current.warning.empty()) {
            std::fprintf(stderr, "cp3c: toolchain: %s\n",
                         current.warning.c_str());
        }
    }
    return current.toolchain;
}

void setToolchainOverride(std::string value) {
    state().overrideValue = std::move(value);
    state().resolved = false;
}

void setToolchainProjectDirectory(fs::path dir) {
    state().projectDir = std::move(dir);
    state().projectDirSet = !state().projectDir.empty();
    state().resolved = false;
}

void resetToolchainResolution() { state() = SelectionState{}; }

ToolchainSource toolchainSource() noexcept { return state().source; }

std::string toolchainSourceDescription() {
    const SelectionState& current = state();
    switch (current.source) {
        case ToolchainSource::Flag:
            return "--toolchain " + current.origin;
        case ToolchainSource::Environment:
            return "CP3_TOOLCHAIN=" + current.origin;
        case ToolchainSource::Project:
            return "project package.json (" + current.origin + ")";
        case ToolchainSource::Global:
            return "global config (" + current.origin + ")";
        case ToolchainSource::Auto:
            return "auto-detect (D12)";
        case ToolchainSource::None:
            break;
    }
    return "none";
}

std::string toolchainWarning() { return state().warning; }

}  // namespace cp3::cli
