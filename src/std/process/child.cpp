// =============================================================================
// src/std/process/child.cpp - platform child-process primitives (see child.hpp).
//
// Windows: CreateProcessA with anonymous pipes; the argument vector is joined
// with the MSVC-runtime quoting rules, so a generated `process.exec("git",
// ["status", "--porcelain"])` reaches the child exactly as passed. POSIX:
// fork + execvp with pipe()d stdio. The POSIX branch is written but not
// exercised on the Windows dev machine (recorded in the wave report).
// =============================================================================
#include "child.hpp"

#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cp3::runtime::process::detail {

#if defined(_WIN32)

namespace {

/// Quote one argument for the MSVC C runtime's command-line parser: the
/// documented backslash/quote doubling rules.
std::string quoteWinArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    for (auto it = arg.begin();;) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (*it == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}

std::string buildWinCommandLine(const std::string& cmd,
                                const std::vector<std::string>& args) {
    std::string line = quoteWinArg(cmd);
    for (const std::string& arg : args) {
        line.push_back(' ');
        line += quoteWinArg(arg);
    }
    return line;
}

std::string winErrorMessage(const std::string& cmd) {
    return "cannot start '" + cmd + "' (Windows error " +
           std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
}

bool makePipe(HANDLE& readEnd, HANDLE& writeEnd) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;  // the CHILD end must be inheritable
    return CreatePipe(&readEnd, &writeEnd, &sa, 0) != 0;
}

}  // namespace

bool launchChild(const std::string& cmd, const std::vector<std::string>& args,
                 ChildProcess& child, std::string& error) {
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr,
           errWrite = nullptr, inRead = nullptr, inWrite = nullptr;
    if (!makePipe(outRead, outWrite) || !makePipe(errRead, errWrite) ||
        !makePipe(inRead, inWrite)) {
        error = "cannot create the child's pipes (Windows error " +
                std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return false;
    }
    // The parent's ends must NOT leak into the child, or EOF never arrives.
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    si.hStdInput = inRead;
    PROCESS_INFORMATION pi{};
    std::string line = buildWinCommandLine(cmd, args);
    const BOOL ok =
        CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                       nullptr, &si, &pi);
    // The child owns its ends from here on (inherited through bInheritHandles).
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    CloseHandle(inRead);
    if (!ok) {
        error = winErrorMessage(cmd);
        CloseHandle(outRead);
        CloseHandle(errRead);
        CloseHandle(inWrite);
        return false;
    }
    CloseHandle(pi.hThread);
    child.process = pi.hProcess;
    child.readOut = outRead;
    child.readErr = errRead;
    child.writeIn = inWrite;
    return true;
}

bool readChunk(ChildProcess& child, bool fromStderr, char* buffer,
               std::size_t capacity, std::size_t& got) {
    const HANDLE handle =
        static_cast<HANDLE>(fromStderr ? child.readErr : child.readOut);
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, buffer, static_cast<DWORD>(capacity), &read,
                             nullptr);
    got = read;
    return ok && read > 0;
}

void waitChild(ChildProcess& child) {
    WaitForSingleObject(static_cast<HANDLE>(child.process), INFINITE);
}

double childExitCode(ChildProcess& child) {
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(child.process), &code);
    return static_cast<double>(code);
}

void writeChild(ChildProcess& child, const std::string& data) {
    if (child.writeIn == nullptr) return;
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(child.writeIn), data.data(),
              static_cast<DWORD>(data.size()), &written, nullptr);
}

void killChild(ChildProcess& child) {
    if (child.process != nullptr) {
        TerminateProcess(static_cast<HANDLE>(child.process), 1);
    }
}

void closeChildStdin(ChildProcess& child) {
    if (child.writeIn != nullptr) {
        CloseHandle(static_cast<HANDLE>(child.writeIn));
        child.writeIn = nullptr;
    }
}

void closeChild(ChildProcess& child) {
    auto close = [](void*& handle) {
        if (handle != nullptr) {
            CloseHandle(static_cast<HANDLE>(handle));
            handle = nullptr;
        }
    };
    close(child.writeIn);
    close(child.readOut);
    close(child.readErr);
    close(child.process);
}

#else  // POSIX

bool launchChild(const std::string& cmd, const std::vector<std::string>& args,
                 ChildProcess& child, std::string& error) {
    int outPipe[2] = {-1, -1}, errPipe[2] = {-1, -1}, inPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0 || pipe(inPipe) != 0) {
        error = "cannot create the child's pipes (" + std::string(strerror(errno)) + ")";
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        error = "cannot fork (" + std::string(strerror(errno)) + ")";
        return false;
    }
    if (pid == 0) {
        // Child: wire the pipes onto stdio, then exec.
        dup2(inPipe[0], 0);
        dup2(outPipe[1], 1);
        dup2(errPipe[1], 2);
        close(inPipe[0]); close(inPipe[1]);
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(cmd.c_str()));
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(cmd.c_str(), argv.data());
        _exit(127);  // exec failed: the parent reports 127 (shell convention)
    }
    close(inPipe[0]);
    close(outPipe[1]);
    close(errPipe[1]);
    child.pid = static_cast<int>(pid);
    child.readOut = outPipe[0];
    child.readErr = errPipe[0];
    child.writeIn = inPipe[1];
    return true;
}

bool readChunk(ChildProcess& child, bool fromStderr, char* buffer,
               std::size_t capacity, std::size_t& got) {
    const int fd = fromStderr ? child.readErr : child.readOut;
    const ssize_t read = ::read(fd, buffer, capacity);
    got = read > 0 ? static_cast<std::size_t>(read) : 0;
    return read > 0;
}

void waitChild(ChildProcess& child) {
    int status = 0;
    while (waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {
    }
    child.exitStatus = status;
}

double childExitCode(ChildProcess& child) {
    if (WIFEXITED(child.exitStatus)) return static_cast<double>(WEXITSTATUS(child.exitStatus));
    if (WIFSIGNALED(child.exitStatus)) {
        return 128.0 + static_cast<double>(WTERMSIG(child.exitStatus));
    }
    return -1.0;
}

void writeChild(ChildProcess& child, const std::string& data) {
    if (child.writeIn < 0) return;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written =
            ::write(child.writeIn, data.data() + offset, data.size() - offset);
        if (written <= 0) return;
        offset += static_cast<std::size_t>(written);
    }
}

void killChild(ChildProcess& child) {
    if (child.pid > 0) {
        ::kill(child.pid, SIGTERM);
    }
}

void closeChildStdin(ChildProcess& child) {
    if (child.writeIn >= 0) {
        close(child.writeIn);
        child.writeIn = -1;
    }
}

void closeChild(ChildProcess& child) {
    for (int* fd : {&child.writeIn, &child.readOut, &child.readErr}) {
        if (*fd >= 0) {
            close(*fd);
            *fd = -1;
        }
    }
    child.pid = -1;
}

#endif  // _WIN32

}  // namespace cp3::runtime::process::detail
