// =============================================================================
// src/cli/src/commands/toolchain.cpp - the `cp3c toolchain` command surface.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdToolchain.
//
// Non-interactive forms for scripts:
//   cp3c toolchain list
//   cp3c toolchain detect
//   cp3c toolchain current
//   cp3c toolchain use <clang[@version]|path> [--project|--global]
//   cp3c toolchain clear [--project|--global]
// Bare `cp3c toolchain` opens the Clang version -> scope wizard. A piped stdin
// gets the numbered fallback for every menu, including scope.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/core/commands.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/toolchain/toolchain.hpp"
#include "detail/toolchain/toolchain_select.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

enum class Scope { None, Project, Global };

std::string padRight(std::string text, std::size_t width) {
    if (text.size() < width) text.append(width - text.size(), ' ');
    return text;
}

std::string statusText(const CandidateStatus& status) {
    if (status.usable) return "ready";
    if (status.found) return "not usable";
    return "missing";
}

std::string statusDetail(const CandidateStatus& status) {
    if (!status.usable) return status.problem;
    std::string detail = status.program;
    if (!status.version.empty()) detail += " (" + status.version + ")";
    return detail;
}

std::string tableRow(const CandidateStatus& status) {
    return "  " + padRight(status.candidate.id, 12) +
           padRight(status.candidate.name, 14) +
           padRight(statusText(status), 11) + statusDetail(status);
}

void printCandidateTable(bool validate) {
    for (const ToolchainCandidate& candidate : toolchainCandidates()) {
        const CandidateStatus status = probeToolchainCandidate(candidate, validate);
        std::printf("%s\n", tableRow(status).c_str());
    }
}

void printInstalledVersions() {
    std::puts("installed versions");
    for (const ToolchainCandidate& candidate : toolchainCandidates()) {
        const std::vector<ToolchainInstall> installs =
            discoverToolchainVersions(candidate, false);
        if (installs.empty()) {
            std::printf("  %s: none\n", candidate.id.c_str());
            continue;
        }
        for (const ToolchainInstall& install : installs) {
            std::printf("  %s %s  %s\n", candidate.id.c_str(),
                        install.version.c_str(), install.program.c_str());
        }
    }
}

std::string effectiveLine() {
    const Toolchain& toolchain = detectToolchain();
    if (!toolchain.found()) return "none (no usable Clang found)";
    return describeToolchain(toolchain) + " from " + toolchainSourceDescription();
}

// Single line, no decoration: the packaging shims (`cp3 doctor`) consume this
// verbatim instead of re-implementing the resolution order.
int toolchainCurrent() {
    std::printf("%s\n", effectiveLine().c_str());
    return kExitSuccess;
}

bool parseScope(const CliOptions& opt, Scope& scope, std::string& err) {
    if (opt.projectScope && opt.globalScope) {
        err = "--project and --global are mutually exclusive";
        return false;
    }
    if (opt.projectScope) scope = Scope::Project;
    else if (opt.globalScope) scope = Scope::Global;
    else scope = Scope::None;
    return true;
}

void printSaved(const Toolchain& resolved, const ToolchainCandidate& candidate,
                const ToolchainChoice& choice, Scope scope,
                const fs::path& root) {
    std::printf("selected: %s %s (%s)\n", candidate.name.c_str(),
                resolved.version.c_str(), resolved.program.c_str());
    std::printf("policy: %s, recorded %s, effective %s\n",
                choice.policy.c_str(), choice.version.c_str(),
                resolved.version.c_str());
    const bool inProject = !root.empty();
    if (scope == Scope::Project) {
        std::printf("scope: this project (%s)\n",
                    (root / "package.json").string().c_str());
    } else if (!inProject) {
        std::printf("scope: global (all projects; no package.json found here) "
                    "(%s)\n", toolchainConfigPath().string().c_str());
    } else {
        std::printf("scope: global (all projects) (%s)\n",
                    toolchainConfigPath().string().c_str());
    }
}

std::string describeChoice(const ToolchainChoice& choice) {
    if (!choice.present) return "not set";
    std::string out = "id=" + choice.id;
    if (!choice.version.empty()) out += ", recorded=" + choice.version;
    if (!choice.policy.empty()) out += ", policy=" + choice.policy;
    if (!choice.program.empty()) out += ", program=" + choice.program;
    return out;
}

int finishToolchainUse(ToolchainChoice choice, const Toolchain& resolved,
                       Scope scope, const fs::path& root,
                       const std::string& value) {
    std::string error;
    // A missing exact pin falls back to the newest installed version. Record
    // the effective version as `latest` so later builds do not keep warning
    // about a version the user cannot install through this flow.
    if (choice.policy == "exact" && !choice.version.empty() &&
        !versionSatisfies(resolved.version, choice.version)) {
        choice.policy = "latest";
    }
    const ToolchainCandidate* candidate = findToolchainCandidate(resolved.id);
    const ToolchainCandidate family =
        candidate != nullptr ? *candidate : toolchainCandidates().front();
    const CandidateStatus status =
        probeToolchainProgram(family, resolved.program, true);
    if (!status.usable) {
        std::fprintf(stderr, "cp3c: %s is not usable: %s\n", value.c_str(),
                     status.problem.c_str());
        return kExitCompileError;
    }
    choice.version = resolved.version;
    if (choice.custom) {
        choice.id = resolved.program;
        choice.program = resolved.program;
        choice.policy = "latest";
    }
    choice.present = true;
    if (!saveToolchainChoice(choice, scope == Scope::Project, root, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitCompileError;
    }
    printSaved(resolved, family, choice, scope, root);
    resetToolchainResolution();
    return kExitSuccess;
}

int toolchainUse(const CliOptions& opt, const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::fputs("usage: cp3c toolchain use <clang[@version]|path> "
                   "[--project|--global]\n", stderr);
        return kExitUsageError;
    }
    Scope scope = Scope::None;
    std::string error;
    if (!parseScope(opt, scope, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitUsageError;
    }
    fs::path root;
    const bool inProject = toolchainProjectRoot(root);
    if (scope == Scope::None) {
        if (!inProject) {
            scope = Scope::Global;
        } else {
            std::fputs("cp3c: this directory is a cp3 project; pass --project "
                       "or --global\n", stderr);
            return kExitUsageError;
        }
    }
    if (scope == Scope::Project && !inProject) {
        std::fputs("cp3c: no package.json found here; --project needs a cp3 "
                   "project\n", stderr);
        return kExitUsageError;
    }
    const std::string value = args.front();
    ToolchainChoice choice;
    if (!parseToolchainSpec(value, choice, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitUsageError;
    }
    ChoiceResolutionResult result;
    resolveToolchainChoice(choice, "toolchain use", opt.strictToolchain, result);
    if (result.status == ChoiceResolution::Invalid) {
        std::fprintf(stderr, "cp3c: %s\n",
                     result.error.empty() ? "invalid toolchain choice"
                                          : result.error.c_str());
        return kExitUsageError;
    }
    if (result.status == ChoiceResolution::FamilyMissing ||
        result.status == ChoiceResolution::StrictFailure) {
        const std::string& message =
            result.warning.empty() ? result.error : result.warning;
        std::fprintf(stderr, "cp3c: %s\n", message.c_str());
        if (result.status == ChoiceResolution::FamilyMissing) {
            std::fputs("cp3c: cannot save a toolchain that is not installed; "
                       "run 'cp3c toolchain' to install one\n", stderr);
        }
        return kExitCompileError;
    }
    if (!result.warning.empty()) {
        std::fprintf(stderr, "cp3c: warning: %s\n", result.warning.c_str());
    }
    return finishToolchainUse(choice, result.toolchain, scope, root, value);
}

int toolchainClear(const CliOptions& opt) {
    Scope scope = Scope::None;
    std::string error;
    if (!parseScope(opt, scope, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitUsageError;
    }
    fs::path root;
    const bool inProject = toolchainProjectRoot(root);
    if (scope == Scope::Project && !inProject) {
        std::fputs("cp3c: no package.json found here; --project needs a cp3 "
                   "project\n", stderr);
        return kExitUsageError;
    }
    bool any = false;
    if ((scope == Scope::Project || scope == Scope::None) && inProject) {
        bool removed = false;
        if (!clearProjectToolchain(root, removed, error)) {
            std::fprintf(stderr, "cp3c: %s\n", error.c_str());
            return kExitCompileError;
        }
        if (removed) {
            std::printf("cleared: project %s\n",
                        (root / "package.json").string().c_str());
            any = true;
        }
    } else if (scope == Scope::Project) {
        std::fputs("cp3c: no package.json found here; nothing to clear\n",
                   stderr);
        return kExitUsageError;
    }
    if (scope == Scope::Global || scope == Scope::None) {
        bool removed = false;
        if (!clearGlobalToolchainConfig(removed, error)) {
            std::fprintf(stderr, "cp3c: %s\n", error.c_str());
            return kExitCompileError;
        }
        if (removed) {
            std::printf("cleared: global %s\n",
                        toolchainConfigPath().string().c_str());
            any = true;
        }
    }
    if (!any) std::fputs("no saved toolchain choice\n", stdout);
    resetToolchainResolution();
    return kExitSuccess;
}

int toolchainList(const CliOptions& opt) {
    if (opt.json) {
        return toolchainListJson();
    }
    std::printf("Clang toolchain\n");
    printCandidateTable(true);
    printInstalledVersions();
    const GlobalToolchainConfig config = readGlobalToolchainConfig();
    if (config.present) {
        std::printf("global config: %s (%s)\n",
                    toolchainConfigPath().string().c_str(),
                    describeChoice(config.choice()).c_str());
    } else {
        std::printf("global config: %s (not set)\n",
                    toolchainConfigPath().string().c_str());
    }
    fs::path root;
    const bool inProject = toolchainProjectRoot(root);
    ToolchainChoice projectChoice;
    bool projectValid = false;
    std::string projectError;
    if (inProject) {
        projectValid = readProjectToolchain(root, projectChoice, projectError);
        if (!projectValid) {
            std::printf("project: %s (invalid: %s)\n",
                        (root / "package.json").string().c_str(),
                        projectError.c_str());
        } else if (!projectChoice.present) {
            std::printf("project: %s (no toolchain field)\n",
                        (root / "package.json").string().c_str());
        } else {
            std::printf("project: %s (%s)\n",
                        (root / "package.json").string().c_str(),
                        describeChoice(projectChoice).c_str());
        }
    } else {
        std::puts("project: none (no package.json found here)");
    }
    std::printf("current: %s\n", effectiveLine().c_str());
    const Toolchain& toolchain = detectToolchain();
    if (toolchain.found()) {
        std::printf("versions: %s\n", toolchainVersionDescription().c_str());
    }
    if (config.present) {
        warnSavedToolchainChoice(config.choice(), "global config");
    }
    if (inProject && projectValid && projectChoice.present) {
        warnSavedToolchainChoice(projectChoice, "project package.json");
    }
    return kExitSuccess;
}

int toolchainDetect() {
    const Toolchain automatic = probeAutoToolchain();
    std::printf("auto-detect (newest Clang): %s\n",
                automatic.found() ? describeToolchain(automatic).c_str()
                                  : "(none found)");
    std::printf("Clang toolchain\n");
    printCandidateTable(true);
    printInstalledVersions();
    std::printf("current: %s\n", effectiveLine().c_str());
    const Toolchain& toolchain = detectToolchain();
    if (toolchain.found()) {
        std::printf("versions: %s\n", toolchainVersionDescription().c_str());
    }
    return kExitSuccess;
}

void printToolchainUsage(std::FILE* out) {
    std::fputs(
        "usage: cp3c toolchain [list|detect|current|use <clang[@version]|path>|"
        "clear]\n"
        "  cp3c toolchain                 choose a Clang version and scope\n"
        "  cp3c toolchain list            versions, paths and the effective choice\n"
        "  cp3c toolchain detect          show what auto-detection finds\n"
        "  cp3c toolchain current         print the effective toolchain, one line\n"
        "  cp3c toolchain use <clang[@version]|path>\n"
        "                                 save a choice (--project|--global)\n"
        "  cp3c toolchain clear           remove saved choices "
        "(--project|--global)\n",
        out);
}

}  // namespace

int cmdToolchain(const CliOptions& opt) {
    const std::string sub =
        opt.positionals.empty() ? "select" : opt.positionals.front();
    std::vector<std::string> args;
    if (opt.positionals.size() > 1) {
        args.assign(opt.positionals.begin() + 1, opt.positionals.end());
    }
    if (sub == "select") {
        if (!args.empty()) {
            std::fputs("cp3c: 'toolchain' takes no arguments\n", stderr);
            return kExitUsageError;
        }
        return runToolchainWizard(opt);
    }
    if (sub == "list") {
        if (!args.empty()) {
            std::fputs("cp3c: 'toolchain list' takes no arguments\n", stderr);
            return kExitUsageError;
        }
        return toolchainList(opt);
    }
    if (sub == "detect") {
        if (!args.empty()) {
            std::fputs("cp3c: 'toolchain detect' takes no arguments\n",
                       stderr);
            return kExitUsageError;
        }
        return toolchainDetect();
    }
    if (sub == "current") {
        if (!args.empty()) {
            std::fputs("cp3c: 'toolchain current' takes no arguments\n",
                       stderr);
            return kExitUsageError;
        }
        return toolchainCurrent();
    }
    if (sub == "use") return toolchainUse(opt, args);
    if (sub == "clear") {
        if (!args.empty()) {
            std::fputs("cp3c: 'toolchain clear' takes no arguments\n", stderr);
            return kExitUsageError;
        }
        return toolchainClear(opt);
    }
    if (sub == "help" || sub == "-h" || sub == "--help") {
        printToolchainUsage(stdout);
        return kExitSuccess;
    }
    std::fprintf(stderr, "cp3c: unknown toolchain command '%s'\n", sub.c_str());
    printToolchainUsage(stderr);
    return kExitUsageError;
}

}  // namespace cp3::cli
