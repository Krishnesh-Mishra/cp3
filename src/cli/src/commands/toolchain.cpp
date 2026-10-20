// =============================================================================
// src/cli/src/commands/toolchain.cpp - the `cp3c toolchain` command surface.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdToolchain.
//
// Non-interactive forms for scripts:
//   cp3c toolchain list
//   cp3c toolchain detect
//   cp3c toolchain use <id|path> [--project|--global]
//   cp3c toolchain clear [--project|--global]
// Bare `cp3c toolchain` opens the single-select menu. A piped stdin gets the
// numbered fallback; when both scopes are possible a flag is required.
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

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

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

struct AssignRequest {
    std::string id;
    std::string name;
    std::string program;
    Scope scope = Scope::None;
    fs::path projectRoot;
    bool customPath = false;
};

bool saveChoice(const AssignRequest& request, std::string& err) {
    if (request.scope == Scope::Project) {
        if (request.projectRoot.empty()) {
            err = "no package.json found; --project needs a cp3 project";
            return false;
        }
        std::string value = request.id;
        if (request.customPath) {
            value = request.program;
            std::fprintf(stderr,
                         "cp3c: warning: storing the machine-specific path "
                         "'%s' in package.json\n",
                         request.program.c_str());
        }
        return writeProjectToolchain(request.projectRoot, value, err);
    }
    return writeGlobalToolchainConfig(request.id, request.program, err);
}

void printSaved(const AssignRequest& request) {
    std::printf("selected: %s (%s)\n", request.name.c_str(),
                request.program.c_str());
    if (request.scope == Scope::Project) {
        std::printf("scope: this project (%s)\n",
                    (request.projectRoot / "package.json").string().c_str());
    } else {
        std::printf("scope: all projects (%s)\n",
                    toolchainConfigPath().string().c_str());
    }
}

int finishInteractive(const CandidateStatus& status, Scope forced,
                      bool inProject, const fs::path& root) {
    Scope scope = forced;
    if (scope == Scope::None) {
        if (!inProject) {
            std::fputs("No package.json found here, so only the global choice "
                       "is available.\n", stdout);
            scope = Scope::Global;
        } else if (!isTty(stdin)) {
            std::fputs("cp3c: this directory is a cp3 project; re-run with "
                       "--project or --global\n", stderr);
            return kExitUsageError;
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
    AssignRequest request;
    request.id = status.candidate.id;
    request.name = status.candidate.name;
    request.program = status.program;
    request.scope = scope;
    request.projectRoot = root;
    std::string error;
    if (!saveChoice(request, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitCompileError;
    }
    printSaved(request);
    return kExitSuccess;
}

// Asks before doing anything, prints the exact command, then runs it. Returns
// true only when an installer actually ran.
bool installMissing(const CandidateStatus& status) {
    const std::string question = "Install " + status.candidate.name + "?";
    if (!runYesNo(question)) return false;
    const InstallPlan plan = planToolchainInstall(status.candidate);
    if (!plan.runnable) {
        std::printf("%s\n", plan.label.c_str());
        if (!plan.url.empty()) std::printf("download: %s\n", plan.url.c_str());
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

void reportStillMissing(const CandidateStatus& status) {
    const InstallPlan plan = planToolchainInstall(status.candidate);
    std::fprintf(stderr, "cp3c: %s is still not usable: %s\n",
                 status.candidate.name.c_str(), status.problem.c_str());
    if (!plan.command.empty()) {
        std::fprintf(stderr, "cp3c: remaining command: %s\n",
                     plan.command.c_str());
    }
    std::fputs("cp3c: installer PATH changes often need a new terminal; open "
               "one and run 'cp3c toolchain' again.\n", stderr);
}

int toolchainInteractive(const CliOptions& opt) {
    Scope forced = Scope::None;
    std::string error;
    if (!parseScope(opt, forced, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitUsageError;
    }
    fs::path root;
    const bool inProject = projectRootHere(root);
    std::vector<CandidateStatus> statuses;
    for (const ToolchainCandidate& candidate : toolchainCandidates()) {
        statuses.push_back(probeToolchainCandidate(candidate, true));
    }
    const Toolchain& current = detectToolchain();
    std::printf("current: %s\n", effectiveLine().c_str());
    int initial = 0;
    for (std::size_t i = 0; i < statuses.size(); ++i) {
        if (!current.program.empty() &&
            statuses[i].program == current.program) {
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
        const int selected =
            runMenu("Select a C++ toolchain:", rows, keys, initial, cancelled);
        if (cancelled) return kExitSuccess;
        if (selected < 0) return kExitUsageError;
        CandidateStatus& status = statuses[static_cast<std::size_t>(selected)];
        if (!status.usable) {
            std::printf("\n%s is not usable: %s\n",
                        status.candidate.name.c_str(),
                        status.problem.c_str());
            if (!installMissing(status)) continue;
            status = probeToolchainCandidate(status.candidate, true);
            if (!status.usable) {
                reportStillMissing(status);
                continue;
            }
        }
        return finishInteractive(status, forced, inProject, root);
    }
}

int toolchainUse(const CliOptions& opt, const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::fputs("usage: cp3c toolchain use <id|path> "
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
    const bool customPath = findToolchainCandidate(lowerCopy(value)) == nullptr;
    ToolchainCandidate candidate;
    if (!customPath) {
        candidate = *findToolchainCandidate(lowerCopy(value));
    } else {
        candidate.id = resolved.msvc ? "msvc"
                                     : (resolved.clang ? "clang" : "gcc");
        candidate.name = resolved.name;
        candidate.exe = resolved.program;
        candidate.msvc = resolved.msvc;
        candidate.clang = resolved.clang;
    }
    const CandidateStatus status = probeToolchainCandidate(candidate, true);
    if (!status.usable) {
        std::fprintf(stderr, "cp3c: %s is not usable: %s\n", value.c_str(),
                     status.problem.c_str());
        return kExitCompileError;
    }
    AssignRequest request;
    request.id = status.candidate.id;
    request.name = status.candidate.name;
    request.program = status.program;
    request.scope = scope;
    request.projectRoot = root;
    request.customPath = customPath;
    if (!saveChoice(request, error)) {
        std::fprintf(stderr, "cp3c: %s\n", error.c_str());
        return kExitCompileError;
    }
    printSaved(request);
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
    const GlobalToolchainConfig config = readGlobalToolchainConfig();
    if (config.present) {
        std::printf("global config: %s (id=%s, program=%s)\n",
                    toolchainConfigPath().string().c_str(),
                    config.id.c_str(), config.program.c_str());
    } else {
        std::printf("global config: %s (not set)\n",
                    toolchainConfigPath().string().c_str());
    }
    fs::path root;
    if (projectRootHere(root)) {
        std::string value;
        std::string error;
        if (!readProjectToolchain(root, value, error)) {
            std::printf("project: %s (invalid: %s)\n",
                        (root / "package.json").string().c_str(),
                        error.c_str());
        } else if (value.empty()) {
            std::printf("project: %s (no toolchain field)\n",
                        (root / "package.json").string().c_str());
        } else {
            std::printf("project: %s (toolchain: %s)\n",
                        (root / "package.json").string().c_str(),
                        value.c_str());
        }
    } else {
        std::puts("project: none (no package.json found here)");
    }
    std::printf("current: %s\n", effectiveLine().c_str());
    return kExitSuccess;
}

int toolchainDetect() {
    const Toolchain automatic = probeAutoToolchain();
    std::printf("auto-detect (D12): %s\n",
                automatic.found() ? describeToolchain(automatic).c_str()
                                  : "(none found)");
    std::printf("C++ toolchains\n");
    printCandidateTable(true);
    std::printf("current: %s\n", effectiveLine().c_str());
    return kExitSuccess;
}

void printToolchainUsage(std::FILE* out) {
    std::fputs(
        "usage: cp3c toolchain [list|detect|use <id|path>|clear]\n"
        "  cp3c toolchain                 choose a compiler interactively\n"
        "  cp3c toolchain list            status, paths and the effective choice\n"
        "  cp3c toolchain detect          show what auto-detection finds\n"
        "  cp3c toolchain use <id|path>   save a choice (--project|--global)\n"
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
        return toolchainInteractive(opt);
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
