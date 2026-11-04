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
//   process.env               -> `::cp3::runtime::process::envMap()`
//                                (the program-wide `Map<string, string>`;
//                                `.get`/`.set`/... lower through the
//                                std/collections member tables)
//   process.exec(cmd, args)   -> `::cp3::runtime::process::exec(cmd, args)`
//                                (Promise<ExecResult>; members map
//                                stdout->out, stderr->err, exitCode->exitCode)
//   process.spawn(cmd, args)  -> `::cp3::runtime::process::spawn(cmd, args)`
//                                (SpawnHandle; `.on("data"|"stderr"|"exit")`,
//                                `.write(..)`, `.kill()`)
//   process.cwd()             -> `::cp3::runtime::process::cwd()`
//   process.chdir(path)       -> `::cp3::runtime::process::chdir(path)`
//   process.pid / .ppid       -> `pid()` / `ppid()` (scalar readers)
//   process.uptime()          -> `uptime()` (process lifetime in seconds)
//   process.memoryUsage()     -> `MemoryUsage` (rss only; rest 0)
//   process.hrtime()          -> `std::tuple<double, double>` ([sec, nsec])
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
//   * envMap is the program-wide `process.env` `Map<string, string>`: created
//     on first use and seeded from the OS environment (an OS variable that is
//     present but EMPTY is absent, so `get(missing) === undefined` holds for
//     it as well). `env.load` writes into it through envSet. The OS
//     environment itself is never modified, so children of exec/spawn inherit
//     the OS environment only.
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
// The environment map lives here (process owns `process.env`); the std/env
// family (src/std/env/include/cp3/std/env.hpp) composes it: `env.get` is
// `envGet` plus conversion/default/throw, `env.has` is `envHas`, and
// `env.load` parses a .env file into `envSet`.
// =============================================================================
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/value.hpp"
#include "cp3/std/collections.hpp"

#if !defined(_WIN32)
// The POSIX environment block (the seeding source of process.env); declared at
// global scope so the C symbol name is not namespace-mangled.
extern char** environ;
#endif

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
// process information and high-resolution timing (docs §04)
// -----------------------------------------------------------------------------
/// `rss` is the real resident set size in bytes. The other fields keep the
/// Node result shape but are always 0 because cp3 has no managed heap.
struct MemoryUsage {
    double rss = 0.0;
    double heapTotal = 0.0;
    double heapUsed = 0.0;
    double external = 0.0;
    double arrayBuffers = 0.0;
};

std::string cwd();
void chdir(const std::string& path);
double pid();
double ppid();
double uptime();
MemoryUsage memoryUsage();
// `process.hrtime()` — Node's [seconds, nanoseconds] form; `uv_hrtime()`
// carries an arbitrary origin, so the pair is monotonic deltas, not absolute
// time.
std::tuple<double, double> hrtime();

// -----------------------------------------------------------------------------
// process.env: the program-wide `Map<string, string>` (docs §04)
// -----------------------------------------------------------------------------
// One `KEY=VALUE` entry of the platform's environment block; a missing `=`,
// an empty key or an empty value is skipped (a present-but-EMPTY OS variable
// counts as unset, the process_env_get_unset semantics).
inline void seedEnvEntry(const collections::Map<std::string, std::string>& map,
                         const char* entry) {
    if (entry == nullptr) return;
    const char* equals = std::strchr(entry, '=');
    if (equals == nullptr || equals == entry || equals[1] == '\0') return;
    map->set(std::string(entry, equals), std::string(equals + 1));
}

// Created on first use and seeded from the OS environment; `env.load` and
// `process.env.set` write here. The OS environment itself is never modified:
// children of exec/spawn inherit the OS environment only.
inline collections::Map<std::string, std::string> envMap() {
    static collections::Map<std::string, std::string> store = [] {
        collections::Map<std::string, std::string> map =
            collections::makeMap<std::string, std::string>();
#if defined(_WIN32)
        for (char** entry = _environ; entry != nullptr && *entry != nullptr;
             ++entry) {
            seedEnvEntry(map, *entry);
        }
#else
        for (char** entry = environ; entry != nullptr && *entry != nullptr;
             ++entry) {
            seedEnvEntry(map, *entry);
        }
#endif
        return map;
    }();
    return store;
}

inline std::optional<std::string> envGet(const std::string& name) {
    return envMap()->get(name);
}

inline bool envHas(const std::string& name) { return envMap()->has(name); }

inline void envSet(const std::string& name, const std::string& value) {
    envMap()->set(name, value);
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
    /// parameters at all (the nullary wrappers below). The event dispatch is
    /// a runtime string, so BOTH adapters instantiate for every listener; the
    /// one that does not fit the listener's shape compiles to an inert
    /// wrapper that is never attached, so a listener of the wrong shape for
    /// its event simply never fires (sema types the documented callbacks).
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
        } else if constexpr (std::is_invocable_v<F&>) {
            return [fn = std::forward<F>(listener)](std::string) mutable {
                fn();
            };
        } else {
            // The dead branch of `on`'s runtime dispatch (an exit-shaped
            // listener): never attached, only required to compile.
            return [](std::string) {};
        }
    }

    template <class F>
    static std::function<void(double)> adaptExit(F&& listener) {
        if constexpr (std::is_invocable_v<F&, double>) {
            return std::function<void(double)>(std::forward<F>(listener));
        } else if constexpr (std::is_invocable_v<F&>) {
            return [fn = std::forward<F>(listener)](double) mutable { fn(); };
        } else {
            // The dead branch of `on`'s runtime dispatch (a chunk-shaped
            // listener): never attached, only required to compile.
            return [](double) {};
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
