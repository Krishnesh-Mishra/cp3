// =============================================================================
// src/std/process/child.hpp - PRIVATE platform child-process primitives.
//
// The small OS surface behind `process.exec`/`process.spawn`
// (src/std/process/runtime.cpp): launch a child with piped stdio, block on
// reads/waits, write stdin, kill and clean up. One implementation per
// platform in child.cpp; the caller owns the threading (worker threads that
// post onto the event loop, src/std/process/runtime.cpp).
//
// This header is NOT part of the generated-code ABI tree
// (src/std/process/include); runtime.cpp includes it by relative path and the
// native row compiles child.cpp as a runtime TU.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cp3::runtime::process::detail {

/// The platform handles of one launched child.
struct ChildProcess {
#if defined(_WIN32)
    void* process = nullptr;  // HANDLE: the child process
    void* readOut = nullptr;  // HANDLE: our read end of the child's stdout
    void* readErr = nullptr;  // HANDLE: our read end of the child's stderr
    void* writeIn = nullptr;  // HANDLE: our write end of the child's stdin
#else
    int pid = -1;
    int readOut = -1;
    int readErr = -1;
    int writeIn = -1;
    int exitStatus = 0;
#endif
};

/// Launch `cmd` with `args` and piped stdin/stdout/stderr. On failure returns
/// false and sets `error` to the platform's message; `child` then holds no
/// open handles.
bool launchChild(const std::string& cmd, const std::vector<std::string>& args,
                 ChildProcess& child, std::string& error);

/// Blocking read of one chunk (`false` on EOF or error; `got` may then be 0).
bool readChunk(ChildProcess& child, bool fromStderr, char* buffer,
               std::size_t capacity, std::size_t& got);

/// Block until the child exits.
void waitChild(ChildProcess& child);

/// The child's exit code (call after waitChild, before closeChild).
double childExitCode(ChildProcess& child);

/// Write to the child's stdin (best effort: partial writes are not retried).
void writeChild(ChildProcess& child, const std::string& data);

/// Terminate the child (`TerminateProcess` / `SIGTERM`).
void killChild(ChildProcess& child);

/// Close the parent's stdin end early, so a child that reads stdin sees EOF
/// (`process.exec` never feeds stdin; `process.spawn` keeps it open for
/// `write`).
void closeChildStdin(ChildProcess& child);

/// Release every handle this struct owns. Idempotent.
void closeChild(ChildProcess& child);

}  // namespace cp3::runtime::process::detail
