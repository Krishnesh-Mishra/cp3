// =============================================================================
// src/cli/src/toolchain/toolchain_install.cpp - platform installer planning.
//
// OWNERSHIP: cli module (src/cli/**). Implements the install half of
// detail/toolchain_select.hpp.
//
// Planning is separate from running: planToolchainInstall only picks the exact
// system package manager command (or the manual instructions when none exists),
// and runInstallPlan is reached only after an explicit Yes in the selector.
// Sudo and winget prompts stay in the terminal; no shell is involved. Beta one
// installs Clang/LLVM only; cp3c never downloads a compiler itself.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string joined(const std::vector<std::string>& argv) {
    return describeCommand(argv);
}

InstallPlan manualPlan(const std::string& instructions,
                       const std::string& url) {
    InstallPlan plan;
    plan.label = instructions;
    plan.url = url;
    plan.runnable = false;
    return plan;
}

bool onPath(const char* name, std::string& found) {
    return findExecutable(name, found);
}

#if defined(_WIN32)

InstallPlan wingetPlan(const std::string& id,
                       const std::vector<std::string>& extra) {
    std::string winget;
    if (!onPath("winget", winget)) {
        return {};
    }
    InstallPlan plan;
    plan.argv.push_back(winget);
    plan.argv.push_back("install");
    plan.argv.push_back("--id");
    plan.argv.push_back(id);
    plan.argv.push_back("-e");
    plan.argv.push_back("--accept-source-agreements");
    plan.argv.push_back("--accept-package-agreements");
    for (const std::string& arg : extra) plan.argv.push_back(arg);
    plan.command = joined(plan.argv);
    plan.label = plan.command;
    plan.runnable = true;
    return plan;
}

std::vector<std::string> wingetVersionArgs(const std::string& version) {
    if (version.empty()) return {};
    return {"--version", version};
}

InstallPlan planForWindows(const std::string& version) {
    InstallPlan plan = wingetPlan("LLVM.LLVM", wingetVersionArgs(version));
    if (plan.runnable) return plan;
    return manualPlan(
        "winget is not available. Install LLVM from "
        "https://releases.llvm.org/ and add its bin directory to PATH.",
        "https://releases.llvm.org/");
}

InstallPlan planMoreVersionsWindows() {
    return manualPlan(
        "Use winget to install another LLVM version, for example: "
        "winget install --id LLVM.LLVM --version <version>",
        "https://github.com/llvm/llvm-project/releases");
}

#elif defined(__APPLE__)

InstallPlan planForApple(const std::string& version) {
    if (version.empty()) {
        std::string tool;
        if (onPath("xcode-select", tool)) {
            InstallPlan plan;
            plan.argv = {tool, "--install"};
            plan.command = joined(plan.argv);
            plan.label = plan.command;
            plan.runnable = true;
            return plan;
        }
    }
    std::string brew;
    if (!onPath("brew", brew)) {
        return manualPlan(
            "Install the Xcode command line tools with "
            "'xcode-select --install', or install Homebrew from https://brew.sh/ "
            "and run 'brew install llvm'.",
            "https://brew.sh/");
    }
    InstallPlan plan;
    plan.argv = {brew, "install", "llvm"};
    plan.command = joined(plan.argv);
    plan.label = plan.command;
    plan.runnable = true;
    return plan;
}

InstallPlan planMoreVersionsApple() {
    return manualPlan(
        "Use Homebrew to install another LLVM formula, or install a versioned "
        "LLVM package with your package manager.",
        "https://brew.sh/");
}

#else

// Package names for the supported Linux package managers. Clang is the only
// compiler cp3 installs in beta one.
std::vector<std::string> packageManagerArgs(const std::string& version) {
    std::string sudo;
    std::vector<std::string> prefix;
    if (onPath("sudo", sudo)) prefix.push_back(sudo);
    std::string tool;
    if (onPath("apt-get", tool)) {
        prefix.push_back(tool);
        prefix.push_back("install");
        prefix.push_back("-y");
        prefix.push_back(version.empty() ? "clang" : "clang=" + version);
        return prefix;
    }
    if (onPath("dnf", tool)) {
        prefix.push_back(tool);
        prefix.push_back("install");
        prefix.push_back("-y");
        prefix.push_back(version.empty() ? "clang" : "clang-" + version);
        return prefix;
    }
    if (onPath("pacman", tool)) {
        prefix.push_back(tool);
        prefix.push_back("-S");
        prefix.push_back("--needed");
        prefix.push_back("clang");
        return prefix;
    }
    return {};
}

InstallPlan planForLinux(const std::string& version) {
    std::vector<std::string> argv = packageManagerArgs(version);
    if (argv.empty()) {
        return manualPlan(
            "no supported package manager (apt-get, dnf, pacman) was found. "
            "Install Clang/LLVM with your system package manager.",
            "https://releases.llvm.org/");
    }
    InstallPlan plan;
    plan.argv = std::move(argv);
    plan.command = joined(plan.argv);
    plan.label = plan.command;
    plan.runnable = true;
    return plan;
}

InstallPlan planMoreVersionsLinux() {
    return manualPlan(
        "Use your package manager to add another Clang version repository, or "
        "install the exact package version shown in the version list.",
        "https://releases.llvm.org/");
}

#endif

}  // namespace

InstallPlan planToolchainInstall(const ToolchainCandidate& candidate,
                                 const std::string& packageVersion) {
    if (candidate.id != "clang") {
        return manualPlan("no installer is known for this compiler.", "");
    }
#if defined(_WIN32)
    return planForWindows(packageVersion);
#elif defined(__APPLE__)
    return planForApple(packageVersion);
#else
    return planForLinux(packageVersion);
#endif
}

InstallPlan planToolchainMoreVersions(const ToolchainCandidate& candidate) {
    if (candidate.id != "clang") {
        return manualPlan("no version manager is known for this compiler.", "");
    }
#if defined(_WIN32)
    return planMoreVersionsWindows();
#elif defined(__APPLE__)
    return planMoreVersionsApple();
#else
    return planMoreVersionsLinux();
#endif
}

bool runInstallPlan(const InstallPlan& plan) {
    if (!plan.runnable || plan.argv.empty()) return false;
    std::printf("running: %s\n", plan.command.c_str());
    std::fflush(stdout);
    const int rc = runProcess(plan.argv);
    if (rc != 0) {
        std::fprintf(stderr, "cp3c: installer exited with code %d\n", rc);
        return false;
    }
    return true;
}

bool offerToInstallClang() {
    const ToolchainCandidate* candidate = findToolchainCandidate("clang");
    if (candidate == nullptr) return false;

    const CandidateStatus status = probeToolchainCandidate(*candidate, false);
    std::fprintf(stderr, "\ncp3c: no usable Clang compiler was found.\n");
    std::fprintf(stderr,
                 "cp3c: beta one requires Clang/LLVM %d or newer.\n",
                 minimumClangMajor());
    if (!status.problem.empty()) {
        std::fprintf(stderr, "cp3c: %s\n", status.problem.c_str());
    }
#if defined(_WIN32)
    std::fputs(
        "cp3c: Windows builds also need a MinGW-w64 C++ runtime. cp3c looks "
        "for one in PATH, the WinLibs/MSYS2 locations, or CP3_MINGW_ROOT. "
        "Clang remains the only compiler driver cp3c invokes.\n",
        stderr);
#endif

    const bool tooOld =
        !status.version.empty() &&
        compareCompilerVersions(
            status.version,
            std::to_string(minimumClangMajor()) + ".0.0") < 0;
    if (status.found && !tooOld) {
        // A Clang exists but cannot build. Installing another LLVM would not
        // fix a missing target sysroot or a broken installation, so report the
        // probe problem and let the user repair it.
        return false;
    }

    const InstallPlan plan = planToolchainInstall(*candidate);
    if (!plan.runnable) {
        std::fprintf(stderr, "cp3c: %s\n", plan.label.c_str());
        if (!plan.url.empty()) {
            std::fprintf(stderr, "cp3c: download: %s\n", plan.url.c_str());
        }
        return false;
    }
    std::fprintf(stderr, "cp3c: install command: %s\n", plan.command.c_str());
    if (!runYesNo("Install Clang/LLVM now?")) return false;
    if (!runInstallPlan(plan)) return false;
    std::fputs(
        "cp3c: installer finished. If clang++ is not on PATH yet, open a new "
        "terminal and run the command again.\n",
        stderr);
    resetToolchainTargetFlagsCache();
    return true;
}

}  // namespace cp3::cli
