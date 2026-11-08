// =============================================================================
// src/dap/src/process.cpp - ChildProcess for the debug adapter (detail/process.hpp).
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// Windows runs gdb inside a job object with KILL_ON_JOB_CLOSE: the debuggee is
// gdb's own child, so terminating the job (or the adapter dying) reaps both -
// no orphaned program is ever left behind. POSIX uses a process group and
// SIGKILL. Reads never block: the adapter is a single-threaded loop.
// =============================================================================
#include "detail/process.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <cstdio>
#endif

namespace cp3::dap {

#if defined(_WIN32)
namespace {

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

// Appends one argument using the CommandLineToArgvW quoting rules.
void appendQuoted(std::wstring& out, const std::wstring& arg) {
    if (!out.empty()) out.push_back(L' ');
    out.push_back(L'"');
    int backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(static_cast<std::size_t>(backslashes) * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(static_cast<std::size_t>(backslashes), L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(static_cast<std::size_t>(backslashes) * 2, L'\\');
    out.push_back(L'"');
}

}  // namespace

ChildProcess::~ChildProcess() { killTree(2000); }

bool ChildProcess::spawn(const std::string& exe,
                         const std::vector<std::string>& args,
                         const std::string& cwd, std::string& err) {
    closeHandles();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE inRead = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    if (CreatePipe(&inRead, &inWrite, &sa, 0) == 0) {
        err = "could not create the stdin pipe";
        return false;
    }
    if (CreatePipe(&outRead, &outWrite, &sa, 0) == 0) {
        CloseHandle(inRead);
        CloseHandle(inWrite);
        err = "could not create the stdout pipe";
        return false;
    }
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;  // gdb keeps stderr quiet on the MI stream

    std::wstring command;
    appendQuoted(command, toWide(exe));
    for (const std::string& arg : args) appendQuoted(command, toWide(arg));
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const std::wstring cwdWide = toWide(cwd);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, 0, nullptr,
        cwdWide.empty() ? nullptr : cwdWide.c_str(), &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (ok == 0) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        err = "could not start " + exe;
        return false;
    }
    CloseHandle(pi.hThread);
    in_ = inWrite;
    out_ = outRead;
    process_ = pi.hProcess;
    pid_ = static_cast<int>(pi.dwProcessId);
    // Everything gdb spawns (the debuggee) dies with the job, including when
    // the adapter itself is killed without a chance to clean up.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits)) != 0 &&
            AssignProcessToJobObject(job, pi.hProcess) != 0) {
            job_ = job;
        } else {
            CloseHandle(job);
        }
    }
    return true;
}

bool ChildProcess::writeLine(const std::string& line) {
    if (in_ == nullptr) return false;
    std::string data = line;
    data.push_back('\n');
    std::size_t written = 0;
    while (written < data.size()) {
        DWORD chunk = 0;
        const DWORD want = static_cast<DWORD>(
            (data.size() - written) > 0x10000 ? 0x10000 : (data.size() - written));
        if (WriteFile(static_cast<HANDLE>(in_), data.data() + written, want,
                      &chunk, nullptr) == 0) {
            return false;
        }
        if (chunk == 0) return false;
        written += chunk;
    }
    return true;
}

bool ChildProcess::readChunk(std::string& out) {
    char buffer[16384];
    DWORD got = 0;
    if (ReadFile(static_cast<HANDLE>(out_), buffer, sizeof(buffer), &got,
                 nullptr) == 0) {
        return false;
    }
    if (got == 0) return false;
    out.append(buffer, got);
    return true;
}

void ChildProcess::readAvailable(std::string& out, bool& closed) {
    closed = false;
    if (out_ == nullptr) {
        closed = true;
        return;
    }
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(static_cast<HANDLE>(out_), nullptr, 0, nullptr, &avail,
                          nullptr) == 0) {
            // The write end is gone (or draining): take what is still buffered,
            // then report the close.
            if (!readChunk(out)) {
                closed = true;
                return;
            }
            continue;
        }
        if (avail == 0) return;
        if (!readChunk(out)) {
            closed = true;
            return;
        }
    }
}

bool ChildProcess::alive() {
    if (exited_) return false;
    if (process_ == nullptr) return false;
    if (WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_OBJECT_0) {
        return true;
    }
    DWORD code = 0;
    if (GetExitCodeProcess(static_cast<HANDLE>(process_), &code) != 0) {
        exitCode_ = static_cast<int>(code);
    }
    exited_ = true;
    return false;
}

void ChildProcess::killTree(int waitMs) {
    if (process_ != nullptr) {
        if (job_ != nullptr) {
            TerminateJobObject(static_cast<HANDLE>(job_), 1);
        } else {
            TerminateProcess(static_cast<HANDLE>(process_), 1);
        }
        const DWORD wait = waitMs > 0 ? static_cast<DWORD>(waitMs) : 0;
        if (WaitForSingleObject(static_cast<HANDLE>(process_), wait) !=
            WAIT_OBJECT_0) {
            TerminateProcess(static_cast<HANDLE>(process_), 1);
            WaitForSingleObject(static_cast<HANDLE>(process_), 2000);
        }
        DWORD code = 0;
        if (GetExitCodeProcess(static_cast<HANDLE>(process_), &code) != 0) {
            exitCode_ = static_cast<int>(code);
        }
        exited_ = true;
    }
    closeHandles();
}

void ChildProcess::closeHandles() {
    if (in_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(in_));
        in_ = nullptr;
    }
    if (out_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(out_));
        out_ = nullptr;
    }
    if (process_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
    if (job_ != nullptr) {
        // Closing the last job handle kills whatever survived inside it.
        CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
    }
}

#else  // !_WIN32

ChildProcess::~ChildProcess() { killTree(2000); }

bool ChildProcess::spawn(const std::string& exe,
                         const std::vector<std::string>& args,
                         const std::string& cwd, std::string& err) {
    closeHandles();
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
        err = "could not create pipes";
        return false;
    }
    const pid_t child = fork();
    if (child < 0) {
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        err = "could not fork";
        return false;
    }
    if (child == 0) {
        dup2(inPipe[0], 0);
        dup2(outPipe[1], 1);
        dup2(outPipe[1], 2);
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        setpgid(0, 0);  // gdb and the debuggee share one killable group
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            fprintf(stderr, "cp3-dap: cannot chdir to '%s'\n", cwd.c_str());
            _exit(127);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(exe.c_str(), argv.data());
        _exit(127);
    }
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    in_ = inPipe[1];
    out_ = outPipe[0];
    pid_ = static_cast<int>(child);
    return true;
}

bool ChildProcess::writeLine(const std::string& line) {
    if (in_ < 0) return false;
    std::string data = line;
    data.push_back('\n');
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(in_, data.data() + written, data.size() - written);
        if (n <= 0) return false;
        written += static_cast<std::size_t>(n);
    }
    return true;
}

void ChildProcess::readAvailable(std::string& out, bool& closed) {
    closed = false;
    if (out_ < 0) {
        closed = true;
        return;
    }
    for (;;) {
        pollfd fd{};
        fd.fd = out_;
        fd.events = POLLIN;
        const int ready = poll(&fd, 1, 0);
        if (ready < 0) {
            closed = true;
            return;
        }
        if (ready == 0) return;
        char buffer[16384];
        const ssize_t got = ::read(out_, buffer, sizeof(buffer));
        if (got > 0) {
            out.append(buffer, static_cast<std::size_t>(got));
            if (static_cast<std::size_t>(got) == sizeof(buffer)) continue;
            return;
        }
        closed = true;
        return;
    }
}

bool ChildProcess::alive() {
    if (exited_) return false;
    if (pid_ <= 0) return false;
    int status = 0;
    const pid_t reaped = waitpid(pid_, &status, WNOHANG);
    if (reaped == 0) return true;
    if (reaped > 0 && WIFEXITED(status)) exitCode_ = WEXITSTATUS(status);
    exited_ = true;
    return false;
}

void ChildProcess::killTree(int waitMs) {
    if (pid_ > 0 && !exited_) {
        kill(-pid_, SIGKILL);  // the whole process group: gdb plus the debuggee
        const int steps = waitMs > 0 ? waitMs / 10 + 1 : 1;
        for (int i = 0; i < steps; ++i) {
            if (!alive()) break;
            usleep(10 * 1000);
        }
        if (!exited_) {
            int status = 0;
            waitpid(pid_, &status, 0);
            exited_ = true;
        }
    }
    closeHandles();
}

void ChildProcess::closeHandles() {
    if (in_ >= 0) {
        ::close(in_);
        in_ = -1;
    }
    if (out_ >= 0) {
        ::close(out_);
        out_ = -1;
    }
}

#endif

}  // namespace cp3::dap
