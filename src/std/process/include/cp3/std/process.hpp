// =============================================================================
// cp3/std/process.hpp - `process` (std/process) backing: exit, argv, env,
// exec and spawn.
//
// OWNERSHIP: the std/process family (src/std/process/**). Additive to the
// generated-code ABI (D14).
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   process.exit(code)        -> `::cp3::runtime::process::exit(code)`
//   process.argv              -> `::cp3::runtime::process::argv()`
//   process.argv.length       -> `argv()->size()` (emitted directly by codegen)
//   process.env.get(name)     -> `::cp3::runtime::process::envGet(name)`
//   process.exec(cmd, args)   -> `::cp3::runtime::process::exec(cmd, args)`
//                                (Promise<ExecResult>; members map
//                                stdout->out, stderr->err, exitCode->exitCode)
//   process.spawn(cmd, args)  -> `::cp3::runtime::process::spawn(cmd, args)`
//                                (SpawnHandle; `.on("data"|"stderr"|"exit")`,
//                                `.write(..)`, `.kill()`)
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * exit(code): flushes stdout, then terminates the process with `code`
//     (Node `process.exit`, corpus-pinned by process_exit_zero/nonzero and
//     cli/run_exit_code_propagates).
//   * argv(): the program-argument vector initArgv stored in main (argv[0] is
//     the program name, so argv().size() >= 1, as in Node). A program that
//     never imported std/process has no argv store; argv() then reports a
//     single "cp3" entry.
//   * envGet/envHas read an in-process overlay first (env.load writes it) and
//     the OS environment second. An OS variable that is present but EMPTY is
//     `undefined` (process_env_get_unset pins unset === undefined); an
//     overlay entry may be an empty string (a parsed `KEY=` line is present).
//   * exec: buffered. Resolves once, after the child exits, with stdout /
//     stderr / exitCode. It does NOT reject on a non-zero exit code; a
//     rejection is reserved for a command that cannot be started at all
//     (STDLIB docs §04, process section).
//   * spawn: returns immediately with a live handle; the child's stdout /
//     stderr chunks and its exit are delivered as loop tasks (a listener
//     registered after the child already produced output may miss it, the
//     usual EventEmitter caveat). A child that cannot be started throws
//     `Thrown` from spawn synchronously.
//   * Neither exec nor spawn re-enters the event loop: they use worker
//     threads that post onto the calling thread's loop (D71: one loop per
//     thread; no second loop).
//
// The environment overlay lives here (process owns `process.env`); the
// std/env family (src/std/env/include/cp3/std/env.hpp) composes it: `env.get`
// is `envGet` plus conversion/default/throw, `env.has` is `envHas`, and
// `env.load` parses a .env file into `envSet`.
// =============================================================================
#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/value.hpp"

namespace cp3::runtime::process {

// -----------------------------------------------------------------------------
// exit (core, unchanged since the D72 split)
// -----------------------------------------------------------------------------
inline void exit(double code) {
    // std::exit runs static destructors (which flush std::cout), but be
    // explicit: a generated program may have written through the C streams.
    std::cout.flush();
    std::fflush(nullptr);
    std::exit(static_cast<int>(code));
}

// -----------------------------------------------------------------------------
// argv
// -----------------------------------------------------------------------------
// `main` hands the C argument vector over before any cp3 statement runs
// (codegen emits the call as the first line of the generated `main`).
void initArgv(int argc, char** argv);

// The program arguments as a cp3 `string[]` (`Array<string>`); a fresh handle
// over the stored vector, so `.length` lowers to `argv()->size()`.
Array<std::string> argv();

// -----------------------------------------------------------------------------
// process.env (the overlay `std/env` composes)
// -----------------------------------------------------------------------------
inline std::unordered_map<std::string, std::string>& envOverlay() {
    static std::unordered_map<std::string, std::string> overlay;
    return overlay;
}

inline std::optional<std::string> envGet(const std::string& name) {
    const auto& overlay = envOverlay();
    const auto it = overlay.find(name);
    if (it != overlay.end()) return it->second;
    if (const char* value = std::getenv(name.c_str())) {
        if (*value != '\0') return std::string(value);
    }
    return std::nullopt;
}

inline bool envHas(const std::string& name) {
    const auto& overlay = envOverlay();
    if (overlay.find(name) != overlay.end()) return true;
    if (const char* value = std::getenv(name.c_str())) return *value != '\0';
    return false;
}

inline void envSet(const std::string& name, const std::string& value) {
    envOverlay()[name] = value;
}

// -----------------------------------------------------------------------------
// exec: the buffered result (docs §04); `out`/`err` keep the C++ field names
// away from the `stdout`/`stderr` macros of <cstdio>, codegen maps back
// (stdout->out, stderr->err).
// -----------------------------------------------------------------------------
struct ExecResult {
    std::string out;
    std::string err;
    double exitCode = 0.0;
};

// -----------------------------------------------------------------------------
// spawn: the live handle (docs §04). Pimpl: the state (child handles, listeners)
// is defined in src/std/process/runtime.cpp.
// -----------------------------------------------------------------------------
struct SpawnState;

class SpawnHandle {
public:
    SpawnHandle() = default;
    ~SpawnHandle();
    SpawnHandle(const SpawnHandle&) = default;
    SpawnHandle& operator=(const SpawnHandle&) = default;

    /// `p.on("data" | "stderr", (chunk: string) => void)` /
    /// `p.on("exit", (code: number) => void)`. A listener may also take no
    /// parameters at all (the nullary wrapper below). With a listener of the
    /// wrong shape the emitted C++ fails to compile; sema types the documented
    /// one-parameter callbacks.
    template <class F>
    void on(const std::string& event, F&& listener) {
        if (event == "exit") {
            addExitListener(adaptExit(std::forward<F>(listener)));
        } else {
            addChunkListener(event, adaptChunk(std::forward<F>(listener)));
        }
    }

    /// Write to the child's stdin; a no-op once the child is gone.
    void write(const std::string& input);
    /// Terminate the child (the exit event still fires, with a non-zero code
    /// on Windows; a killed child reports the platform's termination code).
    void kill();

private:
    friend SpawnHandle spawn(const std::string& cmd,
                             const std::vector<std::string>& args);
    explicit SpawnHandle(std::shared_ptr<SpawnState> state)
        : state_(std::move(state)) {}

    void addChunkListener(const std::string& event,
                          std::function<void(std::string)> listener);
    void addExitListener(std::function<void(double)> listener);

    template <class F>
    static std::function<void(std::string)> adaptChunk(F&& listener) {
        if constexpr (std::is_invocable_v<F&, std::string&>) {
            return std::function<void(std::string)>(std::forward<F>(listener));
        } else {
            return [fn = std::forward<F>(listener)](std::string) mutable {
                fn();
            };
        }
    }

    template <class F>
    static std::function<void(double)> adaptExit(F&& listener) {
        if constexpr (std::is_invocable_v<F&, double>) {
            return std::function<void(double)>(std::forward<F>(listener));
        } else {
            return [fn = std::forward<F>(listener)](double) mutable { fn(); };
        }
    }

    std::shared_ptr<SpawnState> state_;
};

// -----------------------------------------------------------------------------
// exec / spawn
// -----------------------------------------------------------------------------

/// Buffered: a Promise the caller awaits. A launch failure rejects with the
/// platform's message; a command that runs and fails resolves normally (check
/// `result.exitCode`). Never blocks the loop: reading happens on worker
/// threads that post the result back to the calling thread's loop.
Promise<ExecResult> exec(const std::string& cmd,
                         const std::vector<std::string>& args);

/// Streaming: the child starts before this returns; events are delivered as
/// loop tasks (the caller needs the event loop to drain, which codegen emits
/// for any program that spawns). Throws `Thrown` when the child cannot start.
SpawnHandle spawn(const std::string& cmd, const std::vector<std::string>& args);

}  // namespace cp3::runtime::process
