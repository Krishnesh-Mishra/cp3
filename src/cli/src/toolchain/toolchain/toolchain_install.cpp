// =============================================================================
// src/cli/src/toolchain/toolchain_install.cpp - platform installer planning.
//
// OWNERSHIP: cli module (src/cli/**). Implements the install half of
// detail/toolchain_select.hpp.
//
// Planning is separate from running: planToolchainInstall only picks the exact
// system package manager command (or the manual instructions when none exists),
// and runInstallPlan is reached only after an explicit Yes in the selector.
// Sudo and winget prompts stay in the terminal; no shell is involved. More
// MSVC versions opens the Visual Studio Installer, never an in-cp3c download.
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

fs::path visualStudioInstaller() {
    const char* x86 = std::getenv("ProgramFiles(x86)");
    const char* native = std::getenv("ProgramFiles");
    const fs::path relative =
        fs::path("Microsoft") / "Visual Studio" / "Installer" /
        "vs_installer.exe";
    std::error_code ec;
    if (x86 != nullptr) {
        const fs::path candidate = fs::path(x86) / relative;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    if (native != nullptr) {
        const fs::path candidate = fs::path(native) / relative;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

InstallPlan planForWindows(const ToolchainCandidate& candidate,
                           const std::string& version) {
    if (candidate.id == "gcc") {
        InstallPlan plan = wingetPlan("BrechtSanders.WinLibs.POSIX.UCRT",
                                      wingetVersionArgs(version));
        if (plan.runnable) return plan;
        return manualPlan(
            "winget is not available. Download a WinLibs UCRT build from "
            "https://winlibs.com/ and add its bin directory to PATH.",
            "https://winlibs.com/");
    }
    if (candidate.id == "clang") {
        InstallPlan plan = wingetPlan("LLVM.LLVM", wingetVersionArgs(version));
        if (plan.runnable) return plan;
        return manualPlan(
            "winget is not available. Install LLVM from "
            "https://releases.llvm.org/ and add its bin directory to PATH.",
            "https://releases.llvm.org/");
    }
    if (candidate.id == "msvc") {
        InstallPlan plan = wingetPlan(
            "Microsoft.VisualStudio.2022.BuildTools",
            {"--override",
             "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools "
             "--includeRecommended"});
        if (plan.runnable) {
            plan.needsSecondConfirm = true;
            plan.warning =
                "Visual Studio Build Tools is a large download (several GB).";
            return plan;
        }
        return manualPlan(
            "winget is not available. Install Build Tools for Visual Studio "
            "from https://visualstudio.microsoft.com/downloads/ with the "
            "Desktop development with C++ workload.",
            "https://visualstudio.microsoft.com/downloads/");
    }
    return manualPlan("no installer is known for this compiler.", "");
}

InstallPlan planMoreVersionsWindows(const ToolchainCandidate& candidate) {
    if (candidate.id != "msvc") return {};
    const fs::path installer = visualStudioInstaller();
    if (!installer.empty()) {
        InstallPlan plan;
        plan.argv = {installer.string()};
        plan.command = joined(plan.argv);
        plan.label = "Visual Studio Installer";
        plan.runnable = true;
        return plan;
    }
    return manualPlan(
        "Visual Studio Installer not found. Open the Visual Studio Installer "
        "from the Start menu and add an MSVC v143 toolset.",
        "https://visualstudio.microsoft.com/downloads/");
}

#elif defined(__APPLE__)

std::string majorVersion(const std::string& version) {
    const std::size_t dot = version.find('.');
    return dot == std::string::npos ? version : version.substr(0, dot);
}

InstallPlan planForApple(const ToolchainCandidate& candidate,
                         const std::string& version) {
    if (candidate.id == "apple-clang") {
        std::string tool;
        if (onPath("xcode-select", tool)) {
            InstallPlan plan;
            plan.argv = {tool, "--install"};
            plan.command = joined(plan.argv);
            plan.label = plan.command;
            plan.runnable = true;
            return plan;
        }
        return manualPlan(
            "run 'xcode-select --install' from Terminal, or install Xcode "
            "from the App Store.",
            "https://developer.apple.com/xcode/");
    }
    std::string brew;
    if (!onPath("brew", brew)) {
        return manualPlan(
            "Homebrew is not available. Install it from https://brew.sh/ and "
            "run the matching 'brew install' command.",
            "https://brew.sh/");
    }
    std::string formula = candidate.id == "gcc" ? "gcc" : "llvm";
    if (!version.empty() && candidate.id == "gcc") {
        formula += "@" + majorVersion(version);
    }
    InstallPlan plan;
    plan.argv.push_back(brew);
    plan.argv.push_back("install");
    plan.argv.push_back(formula);
    plan.command = joined(plan.argv);
    plan.label = plan.command;
    plan.runnable = true;
    return plan;
}

InstallPlan planMoreVersionsApple(const ToolchainCandidate& candidate) {
    (void)candidate;
    return manualPlan(
        "Use Homebrew to install another formula version, or install a "
        "versioned LLVM/GCC package with your package manager.",
        "https://brew.sh/");
}

#else

// Package names for the three supported Linux package managers.
struct PackageChoice {
    std::string apt;
    std::string dnf;
    std::string pacman;
};

// Returns the package manager invocation, or an empty vector when none of
// apt-get, dnf or pacman is on PATH. A found sudo prefixes every command.
std::vector<std::string> packageManagerArgs(const PackageChoice& packages,
                                            const std::string& version) {
    std::string sudo;
    std::vector<std::string> prefix;
    if (onPath("sudo", sudo)) prefix.push_back(sudo);
    std::string tool;
    if (onPath("apt-get", tool)) {
        prefix.push_back(tool);
        prefix.push_back("install");
        prefix.push_back("-y");
        prefix.push_back(version.empty() ? packages.apt
                                         : packages.apt + "=" + version);
        return prefix;
    }
    if (onPath("dnf", tool)) {
        prefix.push_back(tool);
        prefix.push_back("install");
        prefix.push_back("-y");
        prefix.push_back(version.empty() ? packages.dnf
                                         : packages.dnf + "-" + version);
        return prefix;
    }
    if (onPath("pacman", tool)) {
        prefix.push_back(tool);
        prefix.push_back("-S");
        prefix.push_back("--needed");
        prefix.push_back(packages.pacman);
        return prefix;
    }
    return {};
}

InstallPlan planForLinux(const ToolchainCandidate& candidate,
                         const std::string& version) {
    const bool gcc = candidate.id == "gcc";
    const PackageChoice packages{
        gcc ? (version.empty() ? "build-essential" : "g++") : "clang",
        gcc ? "gcc-c++" : "clang", gcc ? "gcc" : "clang"};
    std::vector<std::string> argv = packageManagerArgs(packages, version);
    if (argv.empty()) {
        return manualPlan(
            "no supported package manager (apt-get, dnf, pacman) was found. "
            "Install g++ or clang with your system package manager.",
            "");
    }
    InstallPlan plan;
    plan.argv = std::move(argv);
    plan.command = joined(plan.argv);
    plan.label = plan.command;
    plan.runnable = true;
    return plan;
}

InstallPlan planMoreVersionsLinux(const ToolchainCandidate& candidate) {
    (void)candidate;
    return manualPlan(
        "Use your package manager to add another version repository, or "
        "switch to the exact package version shown in the version list.",
        "");
}

#endif

}  // namespace

InstallPlan planToolchainInstall(const ToolchainCandidate& candidate,
                                 const std::string& packageVersion) {
#if defined(_WIN32)
    return planForWindows(candidate, packageVersion);
#elif defined(__APPLE__)
    return planForApple(candidate, packageVersion);
#else
    return planForLinux(candidate, packageVersion);
#endif
}

InstallPlan planToolchainMoreVersions(const ToolchainCandidate& candidate) {
#if defined(_WIN32)
    return planMoreVersionsWindows(candidate);
#elif defined(__APPLE__)
    return planMoreVersionsApple(candidate);
#else
    return planMoreVersionsLinux(candidate);
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

}  // namespace cp3::cli
