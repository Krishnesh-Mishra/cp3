// =============================================================================
// src/cli/src/toolchain/toolchain_wizard.cpp - the interactive toolchain flow.
// OWNERSHIP: cli module. Family -> version -> scope; the version list shows the
// newest five package-manager versions plus `Show more (+N)`. Installing always
// delegates to the system package manager, never an in-cp3 download.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "detail/commands.hpp"
#include "detail/toolchain_select.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

enum class Scope { None, Project, Global };
enum class VersionOutcome { Chosen, Back, Cancelled, Invalid };

struct VersionPick {
    ToolchainCandidate candidate;
    ToolchainInstall install;
    std::string policy;
};

struct Menu {
    std::vector<std::string> rows;
    std::vector<std::string> keys;
};

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

bool installedVersion(const std::vector<ToolchainInstall>& installs,
                      const std::string& version,
                      ToolchainInstall& out) {
    for (const ToolchainInstall& install : installs) {
        if (compareCompilerVersions(install.version, version) == 0) {
            out = install;
            return true;
        }
    }
    return false;
}

bool alreadyInstalled(const std::vector<ToolchainInstall>& installs,
                      const std::string& version) {
    ToolchainInstall ignored;
    return installedVersion(installs, version, ignored);
}

void reportStillMissing(const ToolchainCandidate& candidate,
                        const std::string& version) {
    const CandidateStatus status = probeToolchainCandidate(candidate, true);
    std::fprintf(stderr, "cp3c: %s is still not usable: %s\n",
                 candidate.name.c_str(), status.problem.c_str());
    const InstallPlan plan = planToolchainInstall(candidate, version);
    if (!plan.command.empty()) {
        std::fprintf(stderr, "cp3c: remaining command: %s\n",
                     plan.command.c_str());
    }
    std::fputs("cp3c: installer PATH changes often need a new terminal; open "
               "one and run 'cp3c toolchain' again.\n", stderr);
}

bool executePlan(const InstallPlan& plan) {
    if (!plan.runnable) {
        std::printf("%s\n", plan.label.c_str());
        if (!plan.url.empty()) {
            std::printf("download: %s\n", plan.url.c_str());
        }
        return false;
    }
    std::printf("command: %s\n", plan.command.c_str());
    std::fflush(stdout);
    if (!plan.warning.empty()) std::printf("%s\n", plan.warning.c_str());
    if (plan.needsSecondConfirm &&
        !runYesNo("Download and install Build Tools now?")) {
        return false;
    }
    return runInstallPlan(plan);
}

bool askAndInstall(const ToolchainCandidate& candidate,
                   const std::string& packageVersion,
                   const std::string& displayVersion,
                   const std::vector<ToolchainInstall>& installed) {
    if (!installed.empty() && candidate.id != "msvc") {
        if (!displayVersion.empty() &&
            compareCompilerVersions(displayVersion, installed.front().version) <
                0) {
            std::fprintf(stderr,
                         "cp3c: warning: %s %s is older than the installed %s; "
                         "winget installs it in place.\n",
                         candidate.name.c_str(), displayVersion.c_str(),
                         installed.front().version.c_str());
        } else if (!displayVersion.empty()) {
            std::fprintf(stderr,
                         "cp3c: note: installing %s %s replaces the installed "
                         "%s in place.\n",
                         candidate.name.c_str(), displayVersion.c_str(),
                         installed.front().version.c_str());
        }
    }
    std::string question = "Install " + candidate.name;
    if (!displayVersion.empty()) question += " " + displayVersion;
    question += "?";
    if (!runYesNo(question)) return false;
    return executePlan(planToolchainInstall(candidate, packageVersion));
}

bool askMoreMsvc(const ToolchainCandidate& candidate) {
    if (!runYesNo("Open the Visual Studio Installer to add more MSVC toolsets?")) {
        return false;
    }
    return executePlan(planToolchainMoreVersions(candidate));
}

void appendInstalledRows(const std::vector<ToolchainInstall>& installed,
                         Menu& menu) {
    for (const ToolchainInstall& install : installed) {
        std::string row = install.version + " (installed)  " + install.program;
        if (!install.usable) {
            row += "  not usable";
            if (!install.problem.empty()) row += ": " + install.problem;
        } else {
            row += "  ready";
        }
        menu.rows.push_back(std::move(row));
        menu.keys.push_back("version:" + install.version);
    }
}

void appendAvailableRows(const std::vector<AvailableToolchainVersion>& available,
                         const std::vector<ToolchainInstall>& installed,
                         int page, Menu& menu) {
    const std::size_t start = static_cast<std::size_t>(page) * 5;
    const std::size_t end = std::min(start + 5, available.size());
    for (std::size_t i = start; i < end; ++i) {
        std::string row = available[i].version + " (available)";
        if (!installed.empty()) {
            row += "  replaces " + installed.front().version + " in place";
        } else {
            row += "  installs with the system package manager";
        }
        menu.rows.push_back(std::move(row));
        menu.keys.push_back("available:" + available[i].version);
    }
    if (end < available.size()) {
        menu.rows.push_back("Show more (+" +
                            std::to_string(available.size() - end) + ")");
        menu.keys.push_back("more");
    }
}

void buildVersionMenu(const ToolchainCandidate& candidate,
                      const std::vector<ToolchainInstall>& installed,
                      const std::vector<AvailableToolchainVersion>& available,
                      int page, Menu& menu) {
    if (!installed.empty()) {
        menu.rows.push_back("Latest installed (" + installed.front().version +
                            ") - recommended");
    } else if (!available.empty()) {
        menu.rows.push_back("Install latest (" + available.front().version + ")");
    } else if (candidate.msvc) {
        menu.rows.push_back("Install latest (Build Tools)");
    } else {
        menu.rows.push_back("Install latest");
    }
    menu.keys.push_back("latest");
    appendInstalledRows(installed, menu);
    if (candidate.msvc) {
        menu.rows.push_back("More versions (Visual Studio Installer)");
        menu.keys.push_back("more-msvc");
    }
    appendAvailableRows(available, installed, page, menu);
    menu.rows.push_back("Back");
    menu.keys.push_back("back");
    menu.rows.push_back("Cancel");
    menu.keys.push_back("cancel");
}

const AvailableToolchainVersion* findAvailable(
    const std::vector<AvailableToolchainVersion>& available,
    const std::string& version) {
    for (const AvailableToolchainVersion& entry : available) {
        if (compareCompilerVersions(entry.version, version) == 0) return &entry;
    }
    return nullptr;
}

bool chooseLatest(const ToolchainCandidate& candidate,
                  const std::vector<AvailableToolchainVersion>& available,
                  std::vector<ToolchainInstall>& installed, VersionPick& pick) {
    if (!installed.empty()) {
        if (!installed.front().usable) {
            std::printf("\n%s is not usable: %s\n",
                        installed.front().version.c_str(),
                        installed.front().problem.c_str());
            return false;
        }
        pick = {candidate, installed.front(), "latest"};
        return true;
    }
    const bool installedNow =
        available.empty()
            ? askAndInstall(candidate, {}, {}, installed)
            : askAndInstall(candidate, available.front().packageVersion,
                            available.front().version, installed);
    if (!installedNow) return false;
    installed = discoverToolchainVersions(candidate, true);
    if (installed.empty()) reportStillMissing(candidate, {});
    return false;
}

bool chooseAvailable(const ToolchainCandidate& candidate,
                     const AvailableToolchainVersion& offered,
                     std::vector<ToolchainInstall>& installed, VersionPick& pick) {
    if (!askAndInstall(candidate, offered.packageVersion, offered.version,
                       installed)) {
        return false;
    }
    installed = discoverToolchainVersions(candidate, true);
    ToolchainInstall chosen;
    if (!installedVersion(installed, offered.version, chosen)) {
        reportStillMissing(candidate, offered.packageVersion);
        return false;
    }
    if (!chosen.usable) {
        std::printf("\n%s is not usable: %s\n", offered.version.c_str(),
                    chosen.problem.c_str());
        return false;
    }
    pick = {candidate, chosen, "exact"};
    return true;
}

VersionOutcome chooseVersion(const ToolchainCandidate& candidate,
                             VersionPick& pick) {
    std::vector<ToolchainInstall> installed =
        discoverToolchainVersions(candidate, true);
    const std::vector<AvailableToolchainVersion> offered =
        availableToolchainVersions(candidate);
    int page = 0;
    for (;;) {
        std::vector<AvailableToolchainVersion> available;
        for (const AvailableToolchainVersion& version : offered) {
            if (!alreadyInstalled(installed, version.version)) {
                available.push_back(version);
            }
        }
        Menu menu;
        buildVersionMenu(candidate, installed, available, page, menu);
        bool cancelled = false;
        const int selected = runMenu("Select a " + candidate.name + " version:",
                                     menu.rows, menu.keys, 0, cancelled);
        if (cancelled) return VersionOutcome::Cancelled;
        if (selected < 0) return VersionOutcome::Invalid;
        const std::string& key = menu.keys[static_cast<std::size_t>(selected)];
        if (key == "back") return VersionOutcome::Back;
        if (key == "cancel") return VersionOutcome::Cancelled;
        if (key == "more") {
            ++page;
            continue;
        }
        if (key == "more-msvc") {
            if (askMoreMsvc(candidate)) {
                installed = discoverToolchainVersions(candidate, true);
                page = 0;
            }
            continue;
        }
        if (key == "latest") {
            if (chooseLatest(candidate, available, installed, pick)) {
                return VersionOutcome::Chosen;
            }
            continue;
        }
        if (key.rfind("version:", 0) == 0) {
            ToolchainInstall chosen;
            const std::string version = key.substr(std::string("version:").size());
            if (!installedVersion(installed, version, chosen)) continue;
            if (!chosen.usable) {
                std::printf("\n%s is not usable: %s\n", version.c_str(),
                            chosen.problem.c_str());
                continue;
            }
            pick = {candidate, chosen, "exact"};
            return VersionOutcome::Chosen;
        }
        if (key.rfind("available:", 0) == 0) {
            const std::string version =
                key.substr(std::string("available:").size());
            const AvailableToolchainVersion* offeredVersion =
                findAvailable(available, version);
            if (offeredVersion == nullptr) continue;
            if (chooseAvailable(candidate, *offeredVersion, installed, pick)) {
                return VersionOutcome::Chosen;
            }
        }
    }
}

int finishWizard(const VersionPick& pick, Scope forced, bool inProject,
                 const fs::path& root) {
    Scope scope = forced;
    if (scope == Scope::Project && !inProject) {
        std::fputs("cp3c: no package.json found here; --project needs a cp3 "
                   "project\n", stderr);
        return kExitUsageError;
    }
    if (scope == Scope::None) {
        if (!inProject) {
            std::fputs("No package.json found here, so only the global choice "
                       "is available.\n", stdout);
            scope = Scope::Global;
        } else {
            bool cancelled = false;
            const int choice = runMenu(
                "Where should this toolchain apply?",
                {"This project (package.json)", "All projects (global)"},
                {"project", "global"}, 0, cancelled);
            if (cancelled) return kExitSuccess;
            if (choice < 0) return kExitUsageError;
            scope = choice == 0 ? Scope::Project : Scope::Global;
        }
    }
    ToolchainChoice choice;
    choice.id = pick.candidate.id;
    choice.version = pick.install.version;
    choice.policy = pick.policy;
    choice.program = pick.install.program;
    choice.present = true;
    std::string error;
    if (!saveToolchainChoice(choice, scope == Scope::Project, root, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitCompileError;
    }
    std::printf("selected: %s %s (%s)\n", pick.candidate.name.c_str(),
                pick.install.version.c_str(), pick.install.program.c_str());
    std::printf("policy: %s, recorded %s, effective %s\n",
                pick.policy.c_str(), pick.install.version.c_str(),
                pick.install.version.c_str());
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
    resetToolchainResolution();
    return kExitSuccess;
}

}  // namespace

int runWizardDefaults(const std::vector<CandidateStatus>& statuses,
                      const Toolchain& current, Scope forced, bool inProject,
                      const fs::path& root) {
    const CandidateStatus* chosen = nullptr;
    for (const CandidateStatus& status : statuses) {
        if (status.usable && !current.program.empty() &&
            status.program == current.program) {
            chosen = &status;
            break;
        }
    }
    if (chosen == nullptr && !current.id.empty()) {
        for (const CandidateStatus& status : statuses) {
            if (status.usable && status.candidate.id == current.id) {
                chosen = &status;
                break;
            }
        }
    }
    if (chosen == nullptr) {
        for (const CandidateStatus& status : statuses) {
            if (status.usable) {
                chosen = &status;
                break;
            }
        }
    }
    if (chosen == nullptr || chosen->version.empty()) {
        std::fputs("cp3c: no usable C++ compiler found; run 'cp3c "
                   "toolchain' to install one\n", stderr);
        return kExitCompileError;
    }
    ToolchainInstall install;
    install.candidate = chosen->candidate;
    install.program = chosen->program;
    install.linkProgram = chosen->linkProgram;
    install.version = chosen->version;
    install.banner = chosen->banner;
    install.problem = chosen->problem;
    install.usable = chosen->usable;
    VersionPick pick{chosen->candidate, install, "latest"};
    Scope scope = forced;
    if (scope == Scope::None) {
        scope = inProject ? Scope::Project : Scope::Global;
    }
    return finishWizard(pick, scope, inProject, root);
}

int runToolchainWizard(const CliOptions& opt) {
    Scope forced = Scope::None;
    std::string error;
    if (!parseScope(opt, forced, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitUsageError;
    }
    fs::path root;
    const bool inProject = toolchainProjectRoot(root);
    std::vector<CandidateStatus> statuses;
    for (const ToolchainCandidate& candidate : toolchainCandidates()) {
        statuses.push_back(probeToolchainCandidate(candidate, true));
    }
    const Toolchain& current = detectToolchain();
    std::printf("current: ");
    if (current.found()) {
        std::printf("%s from %s\n", describeToolchain(current).c_str(),
                    toolchainSourceDescription().c_str());
        std::printf("versions: %s\n", toolchainVersionDescription().c_str());
    } else {
        std::puts("none (no C++ compiler found)");
    }
    if (opt.yes) {
        return runWizardDefaults(statuses, current, forced, inProject, root);
    }
    int initial = 0;
    for (std::size_t i = 0; i < statuses.size(); ++i) {
        if (!current.program.empty() &&
            statuses[i].program == current.program) {
            initial = static_cast<int>(i);
        } else if (!current.id.empty() &&
                   statuses[i].candidate.id == current.id) {
            initial = static_cast<int>(i);
        }
    }
    for (;;) {
        std::vector<std::string> rows;
        std::vector<std::string> keys;
        for (const CandidateStatus& status : statuses) {
            rows.push_back(tableRow(status));
            keys.push_back(status.candidate.id);
        }
        bool cancelled = false;
        const int selected = runMenu("Select a C++ compiler family:", rows,
                                     keys, initial, cancelled);
        if (cancelled) return kExitSuccess;
        if (selected < 0) return kExitUsageError;
        const CandidateStatus& status = statuses[static_cast<std::size_t>(selected)];
        VersionPick pick;
        const VersionOutcome outcome = chooseVersion(status.candidate, pick);
        if (outcome == VersionOutcome::Cancelled) return kExitSuccess;
        if (outcome == VersionOutcome::Back) continue;
        if (outcome == VersionOutcome::Invalid) return kExitUsageError;
        return finishWizard(pick, forced, inProject, root);
    }
}

}  // namespace cp3::cli
