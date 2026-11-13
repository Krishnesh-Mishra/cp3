// =============================================================================
// src/cli/src/toolchain/toolchain_select.cpp - effective Clang resolution.
// OWNERSHIP: cli module. Implements detail/toolchain_select.hpp and
// detectToolchain: --toolchain > CP3_TOOLCHAIN > project > global > auto.
// `latest` records the saved version and uses the newest installed Clang;
// `exact` uses the recorded series/version and falls back with a warning or
// fails under --strict-toolchain. Beta one has one compiler family: Clang.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/toolchain.hpp"
#include "detail/toolchain/toolchain_select.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

struct SelectionState {
    bool resolved = false;
    bool strict = false;
    bool strictFailed = false;
    Toolchain toolchain;
    ToolchainSource source = ToolchainSource::None;
    std::string origin;
    std::string warning;
    std::string overrideValue;
    fs::path projectDir;
    bool projectDirSet = false;
};

SelectionState& state() {
    static SelectionState current;
    return current;
}

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
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

bool findProgram(const std::string& raw, std::string& program) {
    std::error_code ec;
    if (fs::is_regular_file(raw, ec)) {
        program = raw;
        return true;
    }
    if (raw.find('/') != std::string::npos ||
        raw.find('\\') != std::string::npos) {
        return false;
    }
    return findExecutable(raw, program);
}

Toolchain toolchainFromInstall(const ToolchainCandidate& candidate,
                               const ToolchainInstall& install,
                               const ToolchainChoice& choice,
                               const std::string& policy) {
    Toolchain tc;
    tc.program = install.program;
    tc.name = candidate.name;
    tc.id = candidate.id;
    tc.version = install.version;
    tc.recordedVersion = choice.version;
    tc.policy = policy;
    tc.targetFlags = toolchainTargetFlags(tc.program);
    return tc;
}

ChoiceResolution resolveCustom(const ToolchainChoice& choice, Toolchain& out,
                               std::string& err) {
    std::string program = choice.program.empty() ? choice.id : choice.program;
    if (!findProgram(program, program)) {
        err = "'" + program + "' is not an existing executable";
        return ChoiceResolution::Invalid;
    }
    const ToolchainCandidate* candidate = findToolchainCandidate("clang");
    if (candidate == nullptr) {
        err = "no Clang candidate is registered";
        return ChoiceResolution::Invalid;
    }
    const CandidateStatus status =
        probeToolchainProgram(*candidate, program, true);
    if (!status.usable) {
        err = "'" + program + "' is not a usable Clang compiler";
        if (!status.problem.empty()) err += ": " + status.problem;
        return ChoiceResolution::Invalid;
    }
    out.program = status.program;
    out.name = candidate->name;
    out.id = candidate->id;
    out.version = status.version;
    out.recordedVersion = choice.version;
    out.policy = "custom";
    out.targetFlags = toolchainTargetFlags(out.program);
    return ChoiceResolution::Ok;
}

std::string policyOf(const ToolchainChoice& choice) {
    if (!choice.policy.empty()) return choice.policy;
    return choice.version.empty() ? "latest" : "exact";
}

ChoiceResolution missingFamily(const ToolchainCandidate& candidate,
                               const ToolchainChoice& choice,
                               const std::string& sourceLabel, bool strict,
                               std::string& warning) {
    const std::string& label = candidate.name;
    if (strict) {
        warning = sourceLabel + " selects " + label +
                  ", but it is not installed (--strict-toolchain)";
        return ChoiceResolution::StrictFailure;
    }
    warning = sourceLabel + " selects " + label +
              ", but no usable " + label +
              " was found; using auto-detect. Install it with 'cp3c toolchain'.";
    if (!choice.version.empty()) {
        warning += " Requested version: " + choice.version + ".";
    }
    return ChoiceResolution::FamilyMissing;
}

ChoiceResolution resolveFamily(const ToolchainChoice& choice,
                               const std::string& sourceLabel, bool strict,
                               ChoiceResolutionResult& result) {
    const ToolchainCandidate* candidate = findToolchainCandidate(choice.id);
    if (candidate == nullptr) {
        result.error =
            "unknown toolchain '" + choice.id +
            "' (beta one supports Clang only: clang, clang@<version>, "
            "clang++-<major>, or a clang++ path)";
        return ChoiceResolution::Invalid;
    }
    const std::vector<ToolchainInstall> installs =
        discoverToolchainVersions(*candidate, false);
    if (installs.empty()) {
        return missingFamily(*candidate, choice, sourceLabel, strict,
                             result.warning);
    }
    const std::string policy = policyOf(choice);
    if (policy != "latest" && policy != "exact") {
        result.error = "toolchain policy must be \"latest\" or \"exact\"";
        return ChoiceResolution::Invalid;
    }
    const bool exact = policy == "exact" && !choice.version.empty();
    const ToolchainInstall* selected = &installs.front();
    if (exact) {
        selected = nullptr;
        for (const ToolchainInstall& install : installs) {
            if (versionSatisfies(install.version, choice.version)) {
                selected = &install;
                break;
            }
        }
        if (selected == nullptr) {
            if (strict) {
                result.warning = sourceLabel + " pins " + candidate->name + " " +
                                 choice.version +
                                 ", which is not installed (--strict-toolchain)";
                return ChoiceResolution::StrictFailure;
            }
            selected = &installs.front();
            result.warning = sourceLabel + " pins " + candidate->name + " " +
                             choice.version +
                             ", not installed; falling back to " +
                             candidate->name + " " + selected->version + " (" +
                             selected->program + "). Run 'cp3c toolchain' to install " +
                             choice.version + ".";
        }
    }
    result.toolchain = toolchainFromInstall(*candidate, *selected, choice,
                                            exact ? "exact" : "latest");
    return ChoiceResolution::Ok;
}

ChoiceResolution resolveChoice(const ToolchainChoice& choice,
                               const std::string& sourceLabel, bool strict,
                               ChoiceResolutionResult& result) {
    if (!choice.present || (choice.id.empty() && choice.program.empty())) {
        result.error = "empty toolchain choice";
        return ChoiceResolution::Invalid;
    }
    if (choice.custom) return resolveCustom(choice, result.toolchain, result.error);
    return resolveFamily(choice, sourceLabel, strict, result);
}

enum class SourceResult { Resolved, Skip, Auto, Fatal };

struct ResolutionRun {
    std::vector<std::string> problems;
    bool strictFailed = false;
    bool useAuto = false;
};

SourceResult attemptSource(const ToolchainChoice& choice,
                           const std::string& sourceLabel,
                           ToolchainSource source, const std::string& origin,
                           ResolutionRun& run) {
    ChoiceResolutionResult result;
    result.status =
        resolveChoice(choice, sourceLabel, state().strict, result);
    if (result.status == ChoiceResolution::Ok) {
        SelectionState& current = state();
        current.toolchain = std::move(result.toolchain);
        current.source = source;
        current.origin = origin;
        if (!result.warning.empty()) run.problems.push_back(result.warning);
        return SourceResult::Resolved;
    }
    if (result.status == ChoiceResolution::StrictFailure) {
        run.problems.push_back(result.warning.empty() ? result.error
                                                       : result.warning);
        run.strictFailed = true;
        return SourceResult::Fatal;
    }
    if (result.status == ChoiceResolution::FamilyMissing) {
        run.problems.push_back(result.warning.empty() ? result.error
                                                       : result.warning);
        run.useAuto = true;
        return SourceResult::Auto;
    }
    if (!result.error.empty()) {
        run.problems.push_back(sourceLabel + ": " + result.error);
    }
    return SourceResult::Skip;
}

bool loadChoice(const std::string& raw, const std::string& sourceLabel,
                ToolchainChoice& choice, ResolutionRun& run) {
    std::string error;
    if (!parseToolchainSpec(raw, choice, error)) {
        run.problems.push_back(sourceLabel + ": " + error);
        return false;
    }
    return true;
}

void resolveNow() {
    SelectionState& current = state();
    ResolutionRun run;
    bool done = false;

    if (!current.overrideValue.empty()) {
        ToolchainChoice choice;
        if (loadChoice(current.overrideValue, "--toolchain", choice, run)) {
            done = attemptSource(choice, "--toolchain", ToolchainSource::Flag,
                                 current.overrideValue, run) ==
                   SourceResult::Resolved;
        }
    }
    if (!done && !run.strictFailed && !run.useAuto) {
        if (const char* fromEnv = std::getenv("CP3_TOOLCHAIN")) {
            if (*fromEnv != '\0') {
                ToolchainChoice choice;
                if (loadChoice(fromEnv, "CP3_TOOLCHAIN", choice, run)) {
                    done = attemptSource(
                               choice, "CP3_TOOLCHAIN",
                               ToolchainSource::Environment, fromEnv, run) ==
                           SourceResult::Resolved;
                }
            }
        }
    }
    if (!done && !run.strictFailed && !run.useAuto) {
        fs::path root;
        if (findPackageRoot(projectSearchStart(), root)) {
            ToolchainChoice choice;
            std::string error;
            if (!readProjectToolchain(root, choice, error)) {
                run.problems.push_back("project package.json: " + error);
            } else if (choice.present) {
                done = attemptSource(choice, "project package.json",
                                     ToolchainSource::Project,
                                     (root / "package.json").string(), run) ==
                       SourceResult::Resolved;
            }
        }
    }
    if (!done && !run.strictFailed && !run.useAuto) {
        const GlobalToolchainConfig config = readGlobalToolchainConfig();
        if (config.present) {
            done = attemptSource(
                       config.choice(), "global config", ToolchainSource::Global,
                       toolchainConfigPath().string(), run) ==
                   SourceResult::Resolved;
        }
    }
    if (!done && !run.strictFailed) {
        const Toolchain automatic = probeAutoToolchain();
        if (automatic.found()) {
            current.toolchain = automatic;
            current.source = ToolchainSource::Auto;
            current.origin.clear();
            done = true;
        }
    }
    if (!done && !run.strictFailed) {
        current.toolchain = Toolchain{};
        current.source = ToolchainSource::None;
        current.origin.clear();
    }
    current.strictFailed = run.strictFailed;
    current.warning = joinProblems(run.problems);
}

}  // namespace

bool parseToolchainSpec(const std::string& raw, ToolchainChoice& out,
                        std::string& err) {
    out = ToolchainChoice{};
    const std::string value = trimCopy(raw);
    if (value.empty()) {
        err = "no toolchain value";
        return false;
    }
    const std::string lower = lowerCopy(value);
    if (isClangId(lower)) {
        out.id = "clang";
        out.policy = "latest";
        out.present = true;
        return true;
    }
    // `clang++-18` and `clang++-18.exe` select a Clang series. They are not
    // paths: the versioned driver is discovered like any other install.
    if (lower.rfind("clang++-", 0) == 0) {
        std::string version = value.substr(std::string("clang++-").size());
        if (version.size() > 4 &&
            lowerCopy(version.substr(version.size() - 4)) == ".exe") {
            version.erase(version.size() - 4);
        }
        if (validVersionSpec(version)) {
            out.id = "clang";
            out.version = version;
            out.policy = "exact";
            out.present = true;
            return true;
        }
    }
    const std::size_t at = value.rfind('@');
    if (at != std::string::npos && at > 0) {
        const std::string id = trimCopy(value.substr(0, at));
        if (isClangId(id)) {
            const std::string version = trimCopy(value.substr(at + 1));
            if (!validVersionSpec(version)) {
                err = "invalid version in '" + value + "'";
                return false;
            }
            out.id = "clang";
            out.version = version;
            out.policy = "exact";
            out.present = true;
            return true;
        }
    }
    std::string program;
    if (!findProgram(value, program)) {
        const bool pathShaped =
            value.find('/') != std::string::npos ||
            value.find('\\') != std::string::npos ||
            (value.size() > 1 && value[1] == ':');
        if (pathShaped) {
            err = "compiler path '" + value + "' does not exist";
        } else {
            err = "unknown toolchain '" + value +
                  "' (beta one supports Clang only: clang, clang@<version>, "
                  "clang++-<major>, or a clang++ path)";
        }
        return false;
    }
    out.id = program;
    out.program = program;
    out.custom = true;
    out.policy = "custom";
    out.present = true;
    return true;
}

ChoiceResolution resolveToolchainChoice(const ToolchainChoice& choice,
                                        const std::string& sourceLabel,
                                        bool strict,
                                        ChoiceResolutionResult& result) {
    result = ChoiceResolutionResult{};
    result.status = resolveChoice(choice, sourceLabel, strict, result);
    return result.status;
}

const Toolchain& detectToolchain() {
    SelectionState& current = state();
    if (!current.resolved) {
        resolveNow();
        current.resolved = true;
        if (!current.warning.empty()) {
            std::fprintf(stderr, "cp3c: warning: toolchain: %s\n",
                         current.warning.c_str());
        }
    }
    return current.toolchain;
}

bool ensureClangAvailable(bool interactive) {
    if (detectToolchain().found()) return true;
    if (toolchainStrictFailed() || !interactive) return false;
    if (!offerToInstallClang()) return false;
    resetToolchainResolution();
    return detectToolchain().found();
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

void setToolchainStrict(bool strict) {
    state().strict = strict;
    state().resolved = false;
}

void resetToolchainResolution() {
    state() = SelectionState{};
    resetToolchainTargetFlagsCache();
}

ToolchainSource toolchainSource() noexcept { return state().source; }

std::string toolchainSourceDescription() {
    const SelectionState& current = state();
    switch (current.source) {
        case ToolchainSource::Flag:
            return "command line (--toolchain " + current.origin + ")";
        case ToolchainSource::Environment:
            return "environment (CP3_TOOLCHAIN=" + current.origin + ")";
        case ToolchainSource::Project:
            return "project package.json (" + current.origin + ")";
        case ToolchainSource::Global:
            return "global config (" + current.origin + ")";
        case ToolchainSource::Auto:
            return "auto-detect (newest Clang)";
        case ToolchainSource::None:
            break;
    }
    return "none";
}

std::string toolchainWarning() { return state().warning; }

bool toolchainStrictFailed() noexcept { return state().strictFailed; }

std::string toolchainVersionDescription() {
    const Toolchain& tc = state().toolchain;
    if (!tc.found()) return "none";
    std::string out = "policy " + (tc.policy.empty() ? "auto" : tc.policy);
    if (!tc.recordedVersion.empty()) {
        out += ", recorded " + tc.recordedVersion;
    }
    if (!tc.version.empty()) out += ", effective " + tc.version;
    return out;
}

}  // namespace cp3::cli
