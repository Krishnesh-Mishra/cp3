// =============================================================================
// src/cli/src/toolchain/toolchain_probe.cpp - compiler candidate probing.
//
// OWNERSHIP: cli module (src/cli/**). Implements the candidate half of
// detail/toolchain_select.hpp.
//
// A candidate is only "usable" after a tiny C++20 source compiles, links and
// runs in a temporary directory. That is deliberately stricter than the D12
// build-time lookup: `cp3c toolchain list` must not call a compiler installed
// when it cannot produce a binary, and MSVC additionally needs link.exe on
// PATH because linkArgs invokes `link` directly.
// =============================================================================
#include "detail/toolchain_select.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

#include "detail/util.hpp"

namespace cp3::cli {

namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
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

// Runs `args` with stdout and stderr captured into `out`. Returns the exit
// code, or -1 when the process could not be launched. No shell is involved.
int captureProcess(const std::vector<std::string>& args, std::string& out) {
    std::error_code ec;
    const fs::path log =
        fs::temp_directory_path(ec) / (uniqueToken("cp3-capture-") + ".log");
    std::fflush(stdout);
    std::fflush(stderr);
    std::FILE* file = std::fopen(log.string().c_str(), "wb");
    if (file == nullptr) return -1;
#if defined(_WIN32)
    const int outFd = _fileno(stdout);
    const int errFd = _fileno(stderr);
    const int savedOut = _dup(outFd);
    const int savedErr = _dup(errFd);
    const int logFd = _fileno(file);
    _dup2(logFd, outFd);
    _dup2(logFd, errFd);
#else
    const int outFd = fileno(stdout);
    const int errFd = fileno(stderr);
    const int savedOut = dup(outFd);
    const int savedErr = dup(errFd);
    const int logFd = fileno(file);
    dup2(logFd, outFd);
    dup2(logFd, errFd);
#endif
    const int rc = runProcess(args);
    std::fflush(stdout);
    std::fflush(stderr);
#if defined(_WIN32)
    _dup2(savedOut, outFd);
    _dup2(savedErr, errFd);
    _close(savedOut);
    _close(savedErr);
#else
    dup2(savedOut, outFd);
    dup2(savedErr, errFd);
    close(savedOut);
    close(savedErr);
#endif
    std::fclose(file);
    readTextFile(log, out);
    fs::remove(log, ec);
    return rc;
}

void inferFamily(const std::string& program, Toolchain& out) {
    const std::string base = lowerCopy(fs::path(program).filename().string());
    if (base == "cl" || base.rfind("cl.", 0) == 0 ||
        base.find("cl.exe") != std::string::npos) {
        out.name = "MSVC cl";
        out.msvc = true;
        return;
    }
    if (base.find("clang") != std::string::npos) {
        out.name = "Clang";
        out.clang = true;
        return;
    }
    if (base.find("g++") != std::string::npos ||
        base.find("gcc") != std::string::npos ||
        base.find("mingw") != std::string::npos) {
        out.name = "GCC";
        return;
    }
    out.name = "C++ compiler";
}

bool locateProgram(const ToolchainCandidate& candidate, std::string& program) {
    std::error_code ec;
    // A candidate may carry a full driver path (toolchain use <path>).
    if (candidate.exe.find('/') != std::string::npos ||
        candidate.exe.find('\\') != std::string::npos) {
        if (fs::is_regular_file(candidate.exe, ec)) {
            program = candidate.exe;
            return true;
        }
        return false;
    }
#if defined(__APPLE__)
    if (candidate.id == "apple-clang") {
        const fs::path apple = "/usr/bin/clang++";
        if (fs::is_regular_file(apple, ec)) {
            program = apple.string();
            return true;
        }
    }
#endif
    return findExecutable(candidate.exe, program);
}

std::string versionOf(const ToolchainCandidate& candidate,
                      const std::string& program) {
    std::vector<std::string> args;
    args.push_back(program);
    if (!candidate.msvc) args.push_back("--version");
    std::string out;
    if (captureProcess(args, out) < 0) return {};
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
        args.push_back("-std=c++20");
        args.push_back(cpp.string());
        args.push_back("-o");
        args.push_back(exe.string());
    }
    std::string log;
    const int buildRc = captureProcess(args, log);
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
    const int runRc = captureProcess({exe.string()}, runLog);
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

CandidateStatus probeToolchainCandidate(const ToolchainCandidate& candidate,
                                        bool validate) {
    CandidateStatus status;
    status.candidate = candidate;
    if (!locateProgram(candidate, status.program)) {
        status.problem = "not found on PATH (looked for " + candidate.exe + ")";
        return status;
    }
    status.found = true;
    if (candidate.msvc) {
        std::string link;
        if (!findExecutable("link", link)) {
            status.problem =
                "cl.exe found but link.exe is not on PATH; open a Developer "
                "Command Prompt or install Build Tools with the C++ workload";
            return status;
        }
        status.linkProgram = link;
    }
    status.version = versionOf(candidate, status.program);
    if (!validate) {
        status.usable = true;
        return status;
    }
    std::string reason;
    status.usable = validateProgram(candidate, status.program, reason);
    if (!status.usable) status.problem = reason;
    return status;
}

bool resolveToolchainValue(const std::string& raw, Toolchain& out,
                           std::string& err) {
    const std::string value = trimCopy(raw);
    if (value.empty()) {
        err = "no toolchain value";
        return false;
    }
    if (const ToolchainCandidate* candidate =
            findToolchainCandidate(lowerCopy(value))) {
        const CandidateStatus status = probeToolchainCandidate(*candidate, false);
        if (!status.found) {
            err = "'" + value + "' is not installed (" + status.problem + ")";
            return false;
        }
        if (candidate->msvc && status.linkProgram.empty()) {
            err = status.problem;
            return false;
        }
        out.program = status.program;
        out.name = candidate->name;
        out.msvc = candidate->msvc;
        out.clang = candidate->clang;
        return true;
    }

    std::string program = value;
    std::error_code ec;
    bool exists = fs::is_regular_file(program, ec);
    if (!exists) {
        std::string found;
        if (findExecutable(value, found)) {
            program = found;
            exists = true;
        }
    }
    if (!exists) {
        err = "'" + value +
              "' is neither a known toolchain id (gcc, clang, msvc, "
              "apple-clang) nor an existing executable";
        return false;
    }
    out.program = program;
    inferFamily(program, out);
    return true;
}

}  // namespace cp3::cli
