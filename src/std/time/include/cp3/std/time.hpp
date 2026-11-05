// =============================================================================
// cp3/std/time.hpp - `Stopwatch` (std/time) backing: monotonic elapsed timing.
//
// OWNERSHIP: the std/time family (src/std/time/**). Additive to the
// generated-code ABI (D14): header-only, everything `inline`.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `time.stopwatch()`    -> `::cp3::runtime::time::stopwatch()`
//   `sw.start()`          -> `::cp3::runtime::time::start(sw)`
//   `sw.stop()`           -> `::cp3::runtime::time::stop(sw)`
//   `sw.elapsed()`        -> `::cp3::runtime::time::elapsed(sw)` (milliseconds)
//   `sw.reset()`          -> `::cp3::runtime::time::reset(sw)`
//
// -----------------------------------------------------------------------------
// SEMANTICS (ROADMAP v0.2 ledger section C: monotonic only, never wall clock)
// -----------------------------------------------------------------------------
//   * The clock is `std::chrono::steady_clock`, which is monotonic on every
//     supported platform (QueryPerformanceCounter on Windows,
//     CLOCK_MONOTONIC on the POSIX side). A wall-clock change never moves a
//     Stopwatch, and elapsed time can never go negative.
//   * A new Stopwatch is stopped with an elapsed time of 0.
//   * `start()` (re)starts the interval from now: it discards any previously
//     accumulated time, so `start` after `stop` measures only the new
//     interval. `start()` on an already-running Stopwatch restarts it.
//   * `stop()` freezes the interval: afterwards `elapsed()` returns the same
//     value every time. `stop()` on a stopped Stopwatch is a no-op.
//   * `elapsed()` on a running Stopwatch reads the live interval; on a
//     stopped one it returns the frozen value. The unit is milliseconds as a
//     fractional `double` (steady_clock nanosecond resolution).
//   * `reset()` stops the Stopwatch and zeroes the elapsed time.
//   * Zero allocation: the value is two steady_clock time points plus a flag
//     (24 bytes, trivially copyable); no method allocates or throws.
// =============================================================================
#pragma once

#include <chrono>

namespace cp3::runtime::time {

// A monotonic stopwatch. The member functions are the whole semantics (see
// the file header); the free functions below are the codegen spelling, with
// the receiver first, exactly like std/date's instance lowering.
struct Stopwatch {
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point frozen{};
    bool running = false;

    void start() {
        started = std::chrono::steady_clock::now();
        running = true;
    }

    void stop() {
        if (!running) return;
        frozen = std::chrono::steady_clock::now();
        running = false;
    }

    double elapsed() const {
        const std::chrono::steady_clock::time_point end =
            running ? std::chrono::steady_clock::now() : frozen;
        // A never-started (or reset) watch has started == frozen == epoch,
        // so the difference is exactly 0.
        return std::chrono::duration<double, std::milli>(end - started)
            .count();
    }

    void reset() {
        running = false;
        started = {};
        frozen = {};
    }
};

// `time.stopwatch()`: a fresh, stopped Stopwatch.
inline Stopwatch stopwatch() { return Stopwatch{}; }

// The instance surface, receiver first (the codegen contract above).
inline void start(Stopwatch& sw) { sw.start(); }
inline void stop(Stopwatch& sw) { sw.stop(); }
inline double elapsed(const Stopwatch& sw) { return sw.elapsed(); }
inline void reset(Stopwatch& sw) { sw.reset(); }

}  // namespace cp3::runtime::time
