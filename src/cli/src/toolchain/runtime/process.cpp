// =============================================================================
// src/cli/src/toolchain/process.cpp - process execution DEFINITIONS for cp3c.
//
// OWNERSHIP: cli module (src/cli/**). Implements the process helpers declared in
// detail/util.hpp. Kept separate from util.cpp so the file-size budget covers
// independent concerns instead of one oversized platform file.
// =============================================================================
#include "detail/core/util.hpp"

#include <chrono>
#include <cstdio>
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
#  include <process.h>
#else
#  include <cerrno>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace cp3::cli {

int runProcess(const std::vector<std::string>& args) {
    if (args.empty()) return -1;

#if defined(_WIN32)
    const std::string& exe = args.front();
    const bool fullPath = exe.find('/') != std::string::npos ||
                          exe.find('\\') != std::string::npos;
    // _spawnvp splits "C:\Program Files\..." at the space while searching PATH.
    // A full driver path is already resolved, so _spawnv executes it directly.
    // Its argv[0] is reduced to the file name so the child's command line does
    // not shift at the space either. Bare names still use the PATH search.
    std::vector<std::string> stored = args;
    if (fullPath) stored.front() = fs::path(exe).filename().string();
    std::vector<const char*> argv;
    argv.reserve(stored.size() + 1);
    for (const std::string& a : stored) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const intptr_t rc =
        fullPath ? _spawnv(_P_WAIT, exe.c_str(), argv.data())
                 : _spawnvp(_P_WAIT, exe.c_str(), argv.data());
    return rc < 0 ? -1 : static_cast<int>(rc);
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

int runProcessCapture(const std::vector<std::string>& args, std::string& out) {
    if (args.empty()) return -1;
    static unsigned counter = 0;
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    ++counter;
#if defined(_WIN32)
    const long pid = _getpid();
#else
    const long pid = static_cast<long>(getpid());
#endif
    std::error_code ec;
    const fs::path log =
        fs::temp_directory_path(ec) /
        ("cp3-capture-" + std::to_string(pid) + "_" + std::to_string(stamp) +
         "_" + std::to_string(counter) + ".log");
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

std::string describeCommand(const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out.push_back(' ');
        const bool needsQuote = args[i].find(' ') != std::string::npos;
        if (needsQuote) out.push_back('"');
        out += args[i];
        if (needsQuote) out.push_back('"');
    }
    return out;
}

}  // namespace cp3::cli
