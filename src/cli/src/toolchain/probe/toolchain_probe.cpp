// =============================================================================
// src/cli/src/toolchain/toolchain_probe.cpp - compiler candidate probing.
//
// OWNERSHIP: cli module (src/cli/**). Implements the candidate half of
// detail/toolchain_select.hpp.
//
// A candidate is only "usable" after a tiny C++20 source compiles, links and
// runs in a temporary directory. That is deliberately stricter than the D12
// build-time lookup: `cp3c toolchain list` must not call a compiler installed
// when it cannot produce a binary, and MSVC additionally needs link.exe from
// the same toolset because linkArgs invokes it directly.
// =============================================================================
#include "detail/toolchain/toolchain_select.hpp"

#include <chrono>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string firstLine(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        std::string line = text.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) return line;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

std::string uniqueToken(const char* prefix) {
    static unsigned counter = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    ++counter;
#if defined(_WIN32)
    const long pid = _getpid();
#else
    const long pid = static_cast<long>(getpid());
#endif
    return std::string(prefix) + std::to_string(pid) + "_" +
           std::to_string(stamp) + "_" + std::to_string(counter);
}

// Removes its directory on scope exit, including early returns.
struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / uniqueToken("cp3-toolchain-");
        fs::create_directories(path, ec);
        if (ec) path.clear();
    }
    ~TempDir() {
        if (!path.empty()) removeTree(path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::string versionOf(const ToolchainCandidate& candidate,
                      const std::string& program) {
    std::vector<std::string> args{program};
    if (!candidate.msvc) args.push_back("--version");
    std::string out;
    if (runProcessCapture(args, out) < 0) return {};
    return firstLine(out);
}

// Compiles, links and runs a one-line C++20 program.
bool validateProgram(const ToolchainCandidate& candidate,
                     const std::string& program, std::string& reason) {
    TempDir temp;
    if (temp.path.empty()) {
        reason = "could not create a temporary directory for the probe";
        return false;
    }
    const fs::path cpp = temp.path / "cp3_toolchain_probe.cpp";
    const fs::path exe = temp.path / "cp3_toolchain_probe.exe";
    const fs::path obj = temp.path / "cp3_toolchain_probe.obj";
    const std::string source =
        "#include <cstdio>\n"
        "int main() { std::printf(\"cp3-toolchain-probe\\n\"); return 0; }\n";
    if (!writeTextFile(cpp, source, nullptr)) {
        reason = "could not write the probe source in " + temp.path.string();
        return false;
    }
    std::vector<std::string> args;
    args.push_back(program);
    if (candidate.msvc) {
        args.push_back("/nologo");
        args.push_back("/std:c++20");
        args.push_back("/EHsc");
        args.push_back(cpp.string());
        args.push_back("/Fe:" + exe.string());
        args.push_back("/Fo:" + obj.string());
    } else {
        // The same target flags the real build uses, so a family that probes
        // clean is a family that builds (detail/toolchain_target.cpp).
        for (const std::string& flag :
             toolchainTargetFlags(candidate.clang, candidate.msvc, program)) {
            args.push_back(flag);
        }
        args.push_back("-std=c++20");
        args.push_back(cpp.string());
        args.push_back("-o");
        args.push_back(exe.string());
    }
    std::string log;
    const int buildRc = runProcessCapture(args, log);
    if (buildRc != 0) {
        const std::string detail = firstLine(log);
        reason = "test compile failed";
        if (!detail.empty()) reason += ": " + detail;
        return false;
    }
    std::error_code exeEc;
    if (!fs::is_regular_file(exe, exeEc)) {
        reason = "test compile produced no executable";
        return false;
    }
    std::string runLog;
    const int runRc = runProcessCapture({exe.string()}, runLog);
    if (runRc != 0) {
        const std::string detail = firstLine(runLog);
        reason = "test program did not run";
        if (!detail.empty()) reason += ": " + detail;
        return false;
    }
    return true;
}

}  // namespace

std::vector<ToolchainCandidate> toolchainCandidates() {
#if defined(_WIN32)
    return {
        {"gcc", "GCC", "g++", false, false},
        {"clang", "Clang", "clang++", false, true},
        {"msvc", "MSVC cl", "cl", true, false},
    };
#elif defined(__APPLE__)
    return {
        {"apple-clang", "Apple Clang", "/usr/bin/clang++", false, true},
        {"clang", "Clang", "clang++", false, true},
        {"gcc", "GCC", "g++", false, false},
    };
#else
    return {
        {"gcc", "GCC", "g++", false, false},
        {"clang", "Clang", "clang++", false, true},
    };
#endif
}

const ToolchainCandidate* findToolchainCandidate(const std::string& id) {
    static const std::vector<ToolchainCandidate> candidates =
        toolchainCandidates();
    const std::string wanted = lowerCopy(id);
    for (const ToolchainCandidate& candidate : candidates) {
        if (candidate.id == wanted) return &candidate;
    }
    return nullptr;
}

CandidateStatus probeToolchainProgram(const ToolchainCandidate& candidate,
                                      const std::string& program,
                                      const std::string& linkProgram,
                                      bool validate) {
    CandidateStatus status;
    status.candidate = candidate;
    status.program = program;
    status.linkProgram = linkProgram;
    std::error_code ec;
    if (program.empty() || !fs::is_regular_file(program, ec)) {
        status.problem = "the compiler program does not exist";
        return status;
    }
    status.found = true;
    if (candidate.msvc && linkProgram.empty()) {
        status.problem =
            "cl.exe found but link.exe from the same toolset is missing; "
            "install Build Tools with the C++ workload";
        return status;
    }
    status.banner = versionOf(candidate, program);
    status.version = normalizeCompilerVersion(status.banner);
    if (status.version.empty()) {
        status.problem = "could not parse a version from " +
                         (status.banner.empty() ? program : status.banner);
        return status;
    }
    if (!validate) {
        status.usable = true;
        return status;
    }
    std::string reason;
    status.usable = validateProgram(candidate, status.program, reason);
    if (!status.usable) status.problem = reason;
    return status;
}

CandidateStatus probeToolchainCandidate(const ToolchainCandidate& candidate,
                                        bool validate) {
    CandidateStatus status;
    status.candidate = candidate;
    const std::vector<ToolchainInstall> installs =
        discoverToolchainVersions(candidate, false);
    if (installs.empty()) {
        status.problem = "not installed (looked for " + candidate.exe +
                         " in PATH and standard system locations)";
        return status;
    }
    return probeToolchainProgram(candidate, installs.front().program,
                                 installs.front().linkProgram, validate);
}

}  // namespace cp3::cli
