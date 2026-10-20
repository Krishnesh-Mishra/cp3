// =============================================================================
// src/cli/src/commands/toolchain.cpp - the `cp3c toolchain` command surface.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdToolchain.
//
// Non-interactive forms for scripts:
//   cp3c toolchain list
//   cp3c toolchain detect
//   cp3c toolchain use <id[@version]|path> [--project|--global]
//   cp3c toolchain clear [--project|--global]
// Bare `cp3c toolchain` opens the family -> version -> scope wizard. A piped
// stdin gets the numbered fallback; when both scopes are possible a flag is
// required.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/commands.hpp"
#include "detail/package_json.hpp"
#include "detail/toolchain.hpp"
#include "detail/toolchain_select.hpp"
#include "detail/util.hpp"

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

bool projectRootHere(fs::path& root) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec) return false;
    return findPackageRoot(cwd, root);
}

std::string effectiveLine() {
    const Toolchain& toolchain = detectToolchain();
    if (!toolchain.found()) return "none (no C++ compiler found)";
    return describeToolchain(toolchain) + " from " + toolchainSourceDescription();
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
    if (scope == Scope::Project) {
        std::printf("scope: this project (%s)\n",
                    (root / "package.json").string().c_str());
    } else {
        std::printf("scope: all projects (%s)\n",
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

int toolchainUse(const CliOptions& opt, const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::fputs("usage: cp3c toolchain use <id[@version]|path> "
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
    const bool inProject = projectRootHere(root);
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
    Toolchain resolved;
    if (!resolveToolchainValue(value, resolved, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitCompileError;
    }
    ToolchainChoice choice;
    if (!parseToolchainSpec(value, choice, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitCompileError;
    }
    const ToolchainCandidate* candidate = findToolchainCandidate(resolved.id);
    ToolchainCandidate fallback{"", resolved.name, resolved.program, resolved.msvc,
                                resolved.clang};
    const ToolchainCandidate& family = candidate != nullptr ? *candidate : fallback;
    std::string link;
    if (family.msvc) findExecutable("link", link);
    const CandidateStatus status =
        probeToolchainProgram(family, resolved.program, link, true);
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

int toolchainClear(const CliOptions& opt) {
    Scope scope = Scope::None;
    std::string error;
    if (!parseScope(opt, scope, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitUsageError;
    }
    fs::path root;
    const bool inProject = projectRootHere(root);
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

int toolchainList() {
    std::printf("C++ toolchains\n");
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
    if (projectRootHere(root)) {
        ToolchainChoice choice;
        std::string error;
        if (!readProjectToolchain(root, choice, error)) {
            std::printf("project: %s (invalid: %s)\n",
                        (root / "package.json").string().c_str(),
                        error.c_str());
        } else if (!choice.present) {
            std::printf("project: %s (no toolchain field)\n",
                        (root / "package.json").string().c_str());
        } else {
            std::printf("project: %s (%s)\n",
                        (root / "package.json").string().c_str(),
                        describeChoice(choice).c_str());
        }
    } else {
        std::puts("project: none (no package.json found here)");
    }
    std::printf("current: %s\n", effectiveLine().c_str());
    const Toolchain& toolchain = detectToolchain();
    if (toolchain.found()) {
        std::printf("versions: %s\n", toolchainVersionDescription().c_str());
    }
    return kExitSuccess;
}

int toolchainDetect() {
    const Toolchain automatic = probeAutoToolchain();
    std::printf("auto-detect (D12): %s\n",
                automatic.found() ? describeToolchain(automatic).c_str()
                                  : "(none found)");
    std::printf("C++ toolchains\n");
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
        "usage: cp3c toolchain [list|detect|use <id[@version]|path>|clear]\n"
        "  cp3c toolchain                 choose family, version and scope\n"
        "  cp3c toolchain list            versions, paths and the effective choice\n"
        "  cp3c toolchain detect          show what auto-detection finds\n"
        "  cp3c toolchain use <id[@version]|path>\n"
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
        return toolchainList();
    }
    if (sub == "detect") {
        if (!args.empty()) {
            std::fputs("cp3c: 'toolchain detect' takes no arguments\n",
                       stderr);
            return kExitUsageError;
        }
        return toolchainDetect();
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
