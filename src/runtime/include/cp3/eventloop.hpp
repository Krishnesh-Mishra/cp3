// =============================================================================
// cp3/eventloop.hpp — the single-threaded event loop that drives async.
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// Model (docs/language-spec/14-async.md): one event loop per *thread*, not per
// process. The main thread has one; every `worker` (spec §13) runs on its own
// OS thread and therefore gets its own independent loop. `currentLoop()`
// returns the calling thread's loop, creating it on first use.
//
// Two kinds of work:
//   * microtasks — `post()`; drained completely, in FIFO order, between every
//     macrotask. Promise continuations are microtasks, so the JS ordering
//     ("all the `.then`s run before the next timer") holds.
//   * timers — `addTimer()`; run when due, ordered by (deadline, insertion).
//
// The loop is a drop-in stand-in for the "libuv single event loop" of the spec
// (§14): it keeps the same observable contract — a program exits when there is
// no pending work — without libuv. Timers are the only event source the M1
// runtime needs; file/process/network I/O would be added as more `addTimer`-like
// sources behind this same surface.
//
// EXIT RULE: `run()` returns when there is no pending work left. At that point
// a rejected, unhandled Promise terminates the process (DECISIONS D43); see
// cp3/promise.hpp.
// =============================================================================
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <utility>

namespace cp3::runtime {

class EventLoop {
public:
    using Task = std::function<void()>;
    using TimerId = std::uint64_t;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // --- scheduling ---------------------------------------------------------

    // Queue a microtask on this loop. Safe from the loop's own thread only.
    // (Calling this from another thread is a data race; use `postFromThread`.)
    void post(Task task);

    // Queue a task from *any* thread. Thread-safe; wakes the loop if it is
    // blocked waiting for a timer. This is how a `worker` thread hands its
    // result back to the loop that is awaiting it.
    void postFromThread(Task task);

    // Run `task` once, when at least `delayMs` milliseconds have elapsed.
    // Returns a handle for `cancelTimer`. `delayMs <= 0` runs on the next turn.
    TimerId addTimer(std::int64_t delayMs, Task task);

    // Cancel a pending timer. Returns true if it was still pending.
    bool cancelTimer(TimerId id);

    // --- driving ------------------------------------------------------------

    // Drain microtasks, then run macrotasks/timers until nothing is pending.
    // Returns when the loop is empty or `stop()` was called.
    void run();

    // Drain microtasks, then run at most one batch of due timers/tasks.
    // Returns true if any work was performed. Intended for tests.
    bool runOnce();

    // Drain every queued microtask now, in order. Returns the number drained.
    std::size_t drainMicrotasks();

    // Stop `run()` after the current turn.
    void stop() noexcept;
    bool stopped() const noexcept { return stopped_; }

    // Install a callback invoked by `run()` exactly once, when the loop has
    // drained and is about to return because no work is left. Used by the
    // Promise layer to enforce DECISIONS D43 (an unhandled rejection terminates
    // the process). Not invoked by `runOnce()`.
    void setDrainHook(std::function<void()> hook);

    // --- external producers -------------------------------------------------
    //
    // `worker` (cp3/worker.hpp) computes on another thread and posts its result
    // back through `postFromThread`. `run()` must not return while such a
    // producer is still in flight, or the process would exit before the result
    // arrives ("a program exits once there is no pending work" — spec §14, and
    // an outstanding worker IS pending work). Each producer calls
    // `retainExternal()` before it starts and `releaseExternal()` once it has
    // posted; `run()` blocks while the count is non-zero. Both are thread-safe.
    void retainExternal() noexcept;
    void releaseExternal() noexcept;

    // Pending timers + tasks currently queued (not counting microtasks).
    std::size_t pending() const noexcept;
    // Queued microtasks (for tests / diagnostics).
    std::size_t microtaskCount() const noexcept;

    // Monotonic milliseconds since an arbitrary epoch, used for deadlines.
    static std::int64_t nowMs() noexcept;

private:
    struct TimerEntry {
        std::int64_t deadline = 0;
        TimerId id = 0;
        Task task;
    };

    bool pumpReadyTasks();  // adopt + run ready cross-thread tasks
    bool pumpDueTimers();   // run every timer whose deadline has passed
    bool waitForNext();     // block for the next timer/event; false = loop done
    bool hasExternalWork() const noexcept;

    std::deque<Task> microtasks_;
    // Cross-thread tasks that have been adopted by the loop thread.
    std::deque<Task> readyTasks_;
    // Timers ordered by deadline, then by id (deterministic tie-break).
    std::map<std::pair<std::int64_t, TimerId>, Task> timers_;
    TimerId nextTimerId_ = 1;

    mutable std::mutex mutex_;                // guards incoming_, external_, wakeCv_
    mutable std::condition_variable wakeCv_;  // signalled by postFromThread
    std::deque<Task> incoming_;               // cross-thread tasks
    std::size_t external_ = 0;                // in-flight external producers

    bool stopped_ = false;
    bool inRun_ = false;
    std::function<void()> drainHook_;
};

// The event loop of the calling thread (created on first use). Every `worker`
// thread gets its own, independent loop.
//
// LIFETIME: the loop lives until the process ends; it is deliberately never
// torn down (a `thread_local` destructor is unsafe for a detached `worker`
// thread — see the implementation note in src/eventloop.cpp). Callers must not
// hold a reference past process exit, which is the same rule as any static.
EventLoop& currentLoop();

// Run the calling thread's loop to completion. Generated `main()` ends with
// this call (the spec's "a program exits once the event loop has no pending
// work left", docs/language-spec/14-async.md §"Program exit").
void runEventLoop();

// --- timer builtins (core language, no import — STDLIB docs §03) ------------
//
// `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval` are core builtins
// because they map directly onto the loop (spec §14). They are backed here;
// `sleep` (Promise<void>) lives in cp3/promise.hpp because it needs Promise.

using TimerHandle = std::uint64_t;

// Run `fn` after `delayMs`. Returns a handle for `clearTimeout`.
TimerHandle setTimeout(EventLoop::Task fn, double delayMs);

// Run `fn` every `intervalMs` until cleared. Returns a handle for
// `clearInterval`.
TimerHandle setInterval(EventLoop::Task fn, double intervalMs);

void clearTimeout(TimerHandle handle);
void clearInterval(TimerHandle handle);

}  // namespace cp3::runtime
