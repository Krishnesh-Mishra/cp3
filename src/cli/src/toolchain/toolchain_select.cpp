// =============================================================================
// src/cli/src/toolchain/toolchain_select.cpp - effective toolchain resolution.
// OWNERSHIP: cli module. Implements detail/toolchain_select.hpp and
// detectToolchain: --toolchain > CP3_TOOLCHAIN > project > global > D12.
// `latest` records the saved version and uses the newest installed family
// version; `exact` uses the recorded version and falls back with a warning or
// fails under --strict-toolchain. A missing family uses auto-detect.
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

bool looksLikeVersion(const std::string& text) {
    return !text.empty() && text[0] >= '0' && text[0] <= '9';
}

const ToolchainCandidate* candidateForProgram(const std::string& program) {
    const std::string base = lowerCopy(fs::path(program).filename().string());
    if (base.find("clang") != std::string::npos) return findToolchainCandidate("clang");
    if (base == "cl" || base.rfind("cl.", 0) == 0 ||
        base.find("cl.exe") != std::string::npos) {
        return findToolchainCandidate("msvc");
    }
    if (base.find("g++") != std::string::npos ||
        base.find("gcc") != std::string::npos ||
        base.find("mingw") != std::string::npos) {
        return findToolchainCandidate("gcc");
    }
    return nullptr;
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
    tc.msvc = candidate.msvc;
    tc.clang = candidate.clang;
    return tc;
}

ChoiceResolution resolveCustom(const ToolchainChoice& choice, Toolchain& out,
                               std::string& err) {
    std::string program = choice.program.empty() ? choice.id : choice.program;
    if (!findProgram(program, program)) {
        err = "'" + program + "' is not an existing executable";
        return ChoiceResolution::Invalid;
    }
    const ToolchainCandidate* candidate = candidateForProgram(program);
    ToolchainCandidate fallback{"", "C++ compiler", "", false, false};
    const ToolchainCandidate& family = candidate != nullptr ? *candidate : fallback;
    std::string link;
    if (family.msvc) findExecutable("link", link);
    const CandidateStatus status =
        probeToolchainProgram(family, program, link, false);
    out.program = program;
    out.name = family.name.empty() ? "C++ compiler" : family.name;
    out.id = family.id;
    out.version = status.version;
    out.recordedVersion = choice.version;
    out.policy = "custom";
    out.msvc = family.msvc;
    out.clang = family.clang;
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
              ", but no installed " + label +
              " was found; using auto-detect. Install it with cp3c toolchain.";
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
        result.error = "unknown toolchain id '" + choice.id +
                       "' (expected gcc, clang, msvc or apple-clang)";
        return ChoiceResolution::Invalid;
    }
    std::vector<ToolchainInstall> installs =
        discoverToolchainVersions(*candidate, false);
    if (candidate->msvc) {
        std::vector<ToolchainInstall> usable;
        for (ToolchainInstall& install : installs) {
            if (!install.linkProgram.empty()) usable.push_back(std::move(install));
        }
        installs = std::move(usable);
    }
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
            if (compareCompilerVersions(install.version, choice.version) == 0) {
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
                             selected->program + "). Run cp3c toolchain to install " +
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
    if (const ToolchainCandidate* candidate =
            findToolchainCandidate(lowerCopy(value))) {
        out.id = candidate->id;
        out.policy = "latest";
        out.present = true;
        return true;
    }
    const std::size_t at = value.rfind('@');
    if (at != std::string::npos && at > 0) {
        const std::string id = trimCopy(value.substr(0, at));
        if (const ToolchainCandidate* candidate =
                findToolchainCandidate(lowerCopy(id))) {
            const std::string rawVersion = trimCopy(value.substr(at + 1));
            if (!looksLikeVersion(rawVersion)) {
                err = "invalid version in '" + value + "'";
                return false;
            }
            const std::string version =
                normalizeCompilerVersion(rawVersion);
            if (version.empty()) {
                err = "invalid version in '" + value + "'";
                return false;
            }
            out.id = candidate->id;
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
                  "' (expected gcc, clang, msvc, apple-clang, id@version, or "
                  "an existing compiler path)";
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

void resetToolchainResolution() { state() = SelectionState{}; }

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
            return "auto-detect (D12)";
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
