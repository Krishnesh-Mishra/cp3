// =============================================================================
// cp3/async/eventloop.hpp - the single-threaded event loop that drives async.
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// Model (docs/language-spec/14-async.md): one event loop per *thread*, not per
// process. The main thread has one; every `worker` (spec §13) runs on its own
// OS thread and therefore gets its own independent loop. `currentLoop()`
// returns the calling thread's loop, creating it on first use.
//
// Two kinds of work:
//   * microtasks - `post()`; drained completely, in FIFO order, between every
//     macrotask. Promise continuations are microtasks, so the JS ordering
//     ("all the `.then`s run before the next timer") holds.
//   * timers - `addTimer()`; run when due, ordered by (deadline, insertion).
//
// The loop is a drop-in stand-in for the "libuv single event loop" of the spec
// (§14): it keeps the same observable contract - a program exits when there is
// no pending work - without libuv. Timers are the only event source the M1
// runtime needs; file/process/network I/O would be added as more `addTimer`-like
// sources behind this same surface.
//
// EXIT RULE: `run()` returns when there is no pending work left. At that point
// a rejected, unhandled Promise terminates the process (DECISIONS D43); see
// cp3/async/promise.hpp.
// =============================================================================
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace cp3::runtime {

/// A move-only, small-buffer task: the loop-wide `std::function<void()>`.
///
/// `std::function` stores a closure inline only when it is trivially copyable
/// and at most 16 bytes, so every `shared_ptr` capture (the shape of almost
/// every async continuation and timer callback) costs a heap allocation. This
/// type stores callables up to 24 bytes inline - one `shared_ptr` plus a
/// `std::size_t` index, the largest capture the runtime's own callbacks use -
/// and falls back to a heap allocation only for larger closures, such as user
/// `.then` handlers that capture a lot.
///
/// Layout: the vtable pointer lives in the first word of the storage, so the
/// whole task is 32 bytes (the same role `std::function`'s pointer pair plays).
/// Semantics match `std::function<void()>` for the loop's uses: an empty task
/// is false, invoking an empty task is undefined (the loop never queues one).
class Task {
public:
    Task() noexcept = default;
    Task(std::nullptr_t) noexcept {}

    template <class F, std::enable_if_t<!std::is_same_v<std::decay_t<F>, Task>, int> = 0>
    Task(F&& fn) {
        emplace(std::forward<F>(fn));
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept { take(other); }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            reset();
            take(other);
        }
        return *this;
    }
    Task& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    ~Task() { reset(); }

    explicit operator bool() const noexcept { return ops() != nullptr; }
    bool empty() const noexcept { return ops() == nullptr; }

    void operator()() {
        const Ops* table = ops();
        table->invoke(object());
    }

private:
    // 8 bytes of vtable pointer + 24 bytes of object: a `shared_ptr` plus a
    // `std::size_t` index fit, which covers `Promise.all`'s per-element
    // continuation (the element state itself is kept by `AllState`).
    static constexpr std::size_t kObjectSize = 24;
    static constexpr std::size_t kStorageSize = 8 + kObjectSize;

    struct Ops {
        void (*invoke)(void*);
        void (*destroy)(void*);
        void (*move)(void* dst, void* src);
        bool heap;
    };

    template <class F>
    static constexpr bool kFitsInline =
        sizeof(F) <= kObjectSize && alignof(F) <= 8 &&
        std::is_nothrow_move_constructible_v<F>;

    template <class F>
    static const Ops* opsFor() {
        static const Ops table = {
            [](void* p) {
                if constexpr (kFitsInline<F>) {
                    (*static_cast<F*>(p))();
                } else {
                    (**static_cast<F**>(p))();
                }
            },
            [](void* p) {
                if constexpr (kFitsInline<F>) {
                    static_cast<F*>(p)->~F();
                } else {
                    delete *static_cast<F**>(p);
                }
            },
            [](void* dst, void* src) {
                if constexpr (kFitsInline<F>) {
                    new (dst) F(std::move(*static_cast<F*>(src)));
                } else {
                    *static_cast<F**>(dst) = *static_cast<F**>(src);
                }
            },
            !kFitsInline<F>,
        };
        return &table;
    }

    template <class F>
    void emplace(F&& fn) {
        using D = std::decay_t<F>;
        static_assert(std::is_invocable_v<D&>, "Task requires a nullary callable");
        static_assert(std::is_move_constructible_v<D>, "Task requires a movable callable");
        if constexpr (kFitsInline<D>) {
            new (object()) D(std::forward<F>(fn));
        } else {
            *static_cast<D**>(object()) = new D(std::forward<F>(fn));
        }
        const Ops* table = opsFor<D>();
        *static_cast<const Ops**>(static_cast<void*>(storage_)) = table;
    }

    void take(Task& other) noexcept {
        const Ops* table = other.ops();
        if (table == nullptr) {
            return;
        }
        if (table->heap) {
            // A heap closure lives behind one pointer; stealing the whole
            // (vtable, pointer) pair is a plain copy, no indirect call.
            std::memcpy(storage_, other.storage_, kStorageSize);
            other.clearWithoutDestroy();
            return;
        }
        table->move(object(), other.object());
        table->destroy(other.object());  // moved-from inline object
        *static_cast<const Ops**>(static_cast<void*>(storage_)) = table;
        other.clearWithoutDestroy();
    }

    void reset() noexcept {
        const Ops* table = ops();
        if (table != nullptr) {
            table->destroy(object());
            clearWithoutDestroy();
        }
    }

    void clearWithoutDestroy() noexcept {
        *static_cast<const Ops**>(static_cast<void*>(storage_)) = nullptr;
    }

    const Ops* ops() const noexcept {
        return *static_cast<const Ops* const*>(static_cast<const void*>(storage_));
    }
    void* object() noexcept { return storage_ + 8; }
    const void* object() const noexcept { return storage_ + 8; }

    alignas(8) unsigned char storage_[kStorageSize] = {};
};

class EventLoop {
public:
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
    // Returns a handle for `cancelTimer`. `delayMs <= 0` runs on the next turn;
    // a negative delay keeps its past deadline, so it orders ahead of
    // later-deadlined timers (repeating timers re-arm this way to catch up).
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
    // `worker` (cp3/parallel/worker.hpp) computes on another thread and posts its result
    // back through `postFromThread`. `run()` must not return while such a
    // producer is still in flight, or the process would exit before the result
    // arrives ("a program exits once there is no pending work" - spec §14, and
    // an outstanding worker IS pending work). Each producer calls
    // `retainExternal()` before it starts and `releaseExternal()` once it has
    // posted; `run()` blocks while the count is non-zero. Both are thread-safe.
    void retainExternal() noexcept;
    void releaseExternal() noexcept;

    // --- external event sources ---------------------------------------------
    //
    // `std/network` (cp3/std/network.hpp) needs libuv driven from this loop. The
    // network module owns its libuv loop and installs three opaque callbacks
    // here; the loop itself never sees a libuv type (generated code must not
    // include uv.h), and a program that does not import std/network installs
    // nothing, so timers/promises keep their exact old behavior.
    //
    //   * `poll`     - one non-blocking turn (uv_run(UV_RUN_NOWAIT)); its result
    //                  is advisory (the loop's exit rule does not consult it -
    //                  libuv stays "alive" for its own unref'd uWS timers).
    //   * `waitTurn` - one blocking turn, bounded by `maxWaitMs` (-1 means no
    //                  runtime deadline: the source's own timeout applies). The
    //                  source arms its own wake timer so a runtime timer still
    //                  fires on time while the loop blocks in libuv.
    //   * `wake`     - nudges libuv out of a blocking turn (thread-safe). Called
    //                  by `postFromThread`, so a `worker` result is serviced
    //                  promptly even while the loop is inside libuv.
    //
    // `run()` stays alive while `external_` is non-zero (the network module
    // retains/releases per live server, socket or in-flight fetch), so the exit
    // rule ("no pending work") is unchanged: an open server IS pending work.
    using ExternalPoll = std::function<bool()>;
    using ExternalWait = std::function<void(std::int64_t maxWaitMs)>;
    using ExternalWake = std::function<void()>;
    void setExternalSource(ExternalPoll poll, ExternalWait waitTurn,
                           ExternalWake wake);

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

    // (deadline, insertion) order: earlier deadline first, insertion order for
    // equal deadlines. This is the deterministic order the old ordered map's
    // iteration produced, kept because it is observable.
    static bool timerBefore(const TimerEntry& a, const TimerEntry& b) noexcept {
        return a.deadline < b.deadline ||
               (a.deadline == b.deadline && a.id < b.id);
    }

    bool pumpReadyTasks();  // adopt + run ready cross-thread tasks
    bool pumpDueTimers();   // run every timer whose deadline has passed
    bool waitForNext();     // block for the next timer/event; false = loop done
    bool hasExternalWork() const noexcept;

    // --- timer storage ------------------------------------------------------
    //
    // Two containers, one total order:
    //   * `readyTimers_` - timers added with delay 0 (`sleep(0)`,
    //     `setTimeout(_, 0)`), by far the common case. They are due on the next
    //     turn, and their deadlines are nondecreasing in insertion order (the
    //     clock only moves forward), so a plain FIFO queue is exactly the
    //     (deadline, id) order the heap below would produce. A contiguous
    //     vector with a head index avoids the per-block allocations a deque
    //     pays for a burst of timers.
    //   * `timers_` - a binary min-heap for every other timer (future delays
    //     and overdue repeating-timer catch-ups, whose past deadline must sort
    //     by true deadline). Contiguous storage: no per-timer tree node.
    // `popNextDue` merges the two heads in (deadline, id) order, so the
    // observable firing order is unchanged.
    std::vector<TimerEntry> readyTimers_;
    std::size_t readyHead_ = 0;
    std::vector<TimerEntry> timers_;

    bool readyEmpty() const noexcept { return readyHead_ >= readyTimers_.size(); }
    void readyPush(TimerEntry entry) { readyTimers_.push_back(std::move(entry)); }
    void readyPopFront() noexcept {
        ++readyHead_;
        if (readyHead_ >= readyTimers_.size()) {
            readyTimers_.clear();
            readyHead_ = 0;
        }
    }

    void heapPush(TimerEntry entry);
    void heapSiftUp(std::size_t index);
    void heapSiftDown(std::size_t index);
    void heapPopFront();
    bool cancelFromHeap(TimerId id);
    bool nextDueIsReady(std::int64_t now) const noexcept;
    bool popNextDue(std::int64_t now, Task& out);

    TimerId nextTimerId_ = 1;

    // A contiguous FIFO with a head index, for the same reason as the ready
    // timers: promise continuations arrive in bursts, and the queue is empty
    // again after every drain, so the vector stabilizes at its peak size and
    // stops allocating.
    std::vector<Task> microtasks_;
    std::size_t microtaskHead_ = 0;

    bool microtasksEmpty() const noexcept { return microtaskHead_ >= microtasks_.size(); }
    // Cross-thread tasks that have been adopted by the loop thread.
    std::deque<Task> readyTasks_;

    mutable std::mutex mutex_;                // guards incoming_, external_, wakeCv_
    mutable std::condition_variable wakeCv_;  // signalled by postFromThread
    std::deque<Task> incoming_;               // cross-thread tasks
    std::size_t external_ = 0;                // in-flight external producers

    // The installed external event source (see setExternalSource). Read under
    // `mutex_` so `postFromThread` may use `externalWake_` from any thread.
    ExternalPoll externalPoll_;
    ExternalWait externalWait_;
    ExternalWake externalWake_;

    bool stopped_ = false;
    bool inRun_ = false;
    std::function<void()> drainHook_;
};

// The event loop of the calling thread (created on first use). Every `worker`
// thread gets its own, independent loop.
//
// LIFETIME: the loop lives until the process ends; it is deliberately never
// torn down (a `thread_local` destructor is unsafe for a detached `worker`
// thread - see the implementation note in src/eventloop.cpp). Callers must not
// hold a reference past process exit, which is the same rule as any static.
EventLoop& currentLoop();

// Run the calling thread's loop to completion. Generated `main()` ends with
// this call (the spec's "a program exits once the event loop has no pending
// work left", docs/language-spec/14-async.md §"Program exit").
void runEventLoop();

// --- timer builtins (core language, no import - STDLIB docs §03) ------------
//
// `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval` are core builtins
// because they map directly onto the loop (spec §14). They are backed here;
// `sleep` (Promise<void>) lives in cp3/async/promise.hpp because it needs Promise.

using TimerHandle = std::uint64_t;

// Run `fn` after `delayMs`. Returns a handle for `clearTimeout`.
TimerHandle setTimeout(Task fn, double delayMs);

// Run `fn` every `intervalMs` until cleared. Returns a handle for
// `clearInterval`.
TimerHandle setInterval(Task fn, double intervalMs);

void clearTimeout(TimerHandle handle);
void clearInterval(TimerHandle handle);

// Scheduling primitives (core builtins, STDLIB §03): `queueMicrotask(cb)`
// and `process.nextTick(cb)` enqueue on the microtask queue (drained before
// timers); `setImmediate(cb)` runs on the next loop turn via a zero-delay
// timer. All keep the loop alive until the callback has run.
inline void queueMicrotask(Task fn) { currentLoop().post(std::move(fn)); }
inline void nextTick(Task fn) { currentLoop().post(std::move(fn)); }
inline void setImmediate(Task fn) { currentLoop().addTimer(0, std::move(fn)); }

}  // namespace cp3::runtime
