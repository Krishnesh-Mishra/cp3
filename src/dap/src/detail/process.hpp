// =============================================================================
// src/dap/src/detail/process.hpp - one child process with piped stdio.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**). PRIVATE header.
//
// Just enough for the one long-lived child the adapter has (gdb): spawn with
// stdin/stdout pipes, non-blocking reads of whatever the child has written, and
// a kill that also takes the child's own children down (the debuggee). No shell
// and no threads: the adapter polls, so it stays single-threaded.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cp3::dap {

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Spawns `exe` with `args` in `cwd`. False with `err` on failure.
    bool spawn(const std::string& exe, const std::vector<std::string>& args,
               const std::string& cwd, std::string& err);

    // Writes `line` + '\n' to the child's stdin; false when the pipe is gone.
    bool writeLine(const std::string& line);

    // Non-blocking: appends whatever the child already wrote to `out` (also
    // flushes what is still buffered when the pipe is broken). `closed` is set
    // once the read end reports end of stream.
    void readAvailable(std::string& out, bool& closed);

    // True while the child has not exited; reaps a finished child.
    bool alive();

    int pid() const noexcept { return pid_; }
    int exitCode() const noexcept { return exitCode_; }

    // Kills the child and everything it spawned, then reaps it (bounded wait).
    // Safe to call repeatedly and after the child already exited.
    void killTree(int waitMs);

private:
    void closeHandles();
#if defined(_WIN32)
    bool readChunk(std::string& out);
    void* process_ = nullptr;  // HANDLE
    void* job_ = nullptr;      // HANDLE: kill-on-close job holding the child
    void* in_ = nullptr;       // HANDLE: write end of the child's stdin
    void* out_ = nullptr;      // HANDLE: read end of the child's stdout
#else
    int in_ = -1;
    int out_ = -1;
#endif
    int pid_ = 0;
    int exitCode_ = -1;
    bool exited_ = false;
};

}  // namespace cp3::dap
