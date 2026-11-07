// =============================================================================
// cp3/parallel/worker.hpp - `new Worker(...)`: an isolated OS thread with a
// typed, message-passing channel (docs/language-spec/13-concurrency-...,
// DECISIONS D80 + addendum).
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   cp3 `new Worker("./x.cp3")` / `new Worker((thread: Thread<In, Out>) => ...)`
//        -> `::cp3::runtime::startWorker<In, Out>(&::<worker-unit>::cp3_default)`
//     which yields a `WorkerHandle<In, Out>`. `In` is main-to-worker, `Out` is
//     worker-to-main, exactly as the worker entry's `Thread<In, Out>` annotation
//     spells them.
//
//   Main side (`w`):
//     w.postMessage(v)   -> w.postMessage(v)         (deep copy in)
//     w.on("message", l) -> w.onMessage(l)           (l: (const Out&) -> void)
//     w.on("error", l)   -> w.onError(l)             (l: (const Thrown&) -> void)
//     await w.request(v) -> ::cp3::runtime::awaitValue(w.request(v))
//                           (`Promise<Out>`; resolves with the next Out message)
//     w.terminate()      -> w.terminate()
//
//   Worker side (`thread`):
//     thread.on("message", l) -> thread.onMessage(l) (l: (const In&) -> void)
//     thread.on("error", l)   -> thread.onError(l)
//     thread.postMessage(v)   -> thread.postMessage(v)  (deep copy out)
//
//   The worker unit's default export is compiled as
//     `void cp3_default(::cp3::runtime::ThreadHandle<In, Out>& thread)`.
//
//   Every payload crosses the boundary through `cp3::runtime::clone` (see
//   cp3/core/value.hpp), the same deep copy an assignment performs, so codegen
//   MUST specialize `Cloner<T>` for every object/class type that can be sent
//   or received - exactly as the old worker surface required.
//
// -----------------------------------------------------------------------------
// SEMANTICS (spec §13, D80)
// -----------------------------------------------------------------------------
//   * `new Worker` starts one real OS thread that runs a *synthesized worker
//     unit*: the entry function runs once, then that thread's own event loop
//     (`currentLoop()` is per-thread) serves its messages until the main side
//     terminates it.
//   * Nothing is shared: messages are genuine deep copies in both directions.
//     Delivery is FIFO per sender and a message queues until the receiving
//     side attaches a listener (attaching later still receives it).
//   * A thrown error inside a message handler is delivered to the other side as
//     a catchable `"error"` event; the throwing side's loop keeps serving. A
//     throw at worker start-up is an `"error"` event to the main side and the
//     thread ends.
//   * A live worker keeps the main loop alive (an outstanding worker IS pending
//     work, eventloop.hpp). `terminate()` stops the worker's loop, frees the
//     thread, drops queued messages and pending `request`s, and is idempotent.
//     The process then exits once the main loop is idle and no live handles
//     remain. A hard crash still takes the process down, per the D80 ruling.
//   * Nothing blocks the main loop: it keeps turning between messages, so
//     timers stay live while a worker answers.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/core/value.hpp"

#include "cp3/parallel/worker_channel.hpp"

namespace cp3::runtime {


// ---------------------------------------------------------------------------
// ThreadHandle<In, Out> - the worker side (`thread` in the entry's annotation).
//
// The worker-side surface is receive-only plus `postMessage`: termination is
// the main side's (D80).
// ---------------------------------------------------------------------------
template <class In, class Out>
class ThreadHandle {
public:
    using MessageListener = typename ChannelPort<In>::MessageListener;
    using ErrorListener = typename ChannelPort<In>::ErrorListener;

    explicit ThreadHandle(std::shared_ptr<WorkerChannel<In, Out>> channel)
        : channel_(std::move(channel)) {}

    // Copyable: the facade holds only the channel handle, so listener boxes
    // that capture `thread` share the channel and keep its data alive,
    // exactly as the channel's own semantics require (spec §13).
    ThreadHandle(const ThreadHandle&) = default;
    ThreadHandle& operator=(const ThreadHandle&) = default;

    void onMessage(MessageListener listener) {
        channel_->toWorker().onMessage(std::move(listener));
    }

    void onError(ErrorListener listener) {
        channel_->toWorker().onError(std::move(listener));
    }

    template <class U = Out, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    void postMessage(const U& value) {
        channel_->toMain().send(value);
    }

private:
    std::shared_ptr<WorkerChannel<In, Out>> channel_;
};

// ---------------------------------------------------------------------------
// WorkerHandle<In, Out> - the main side (`w`).
//
// Owning handle: destructing it terminates the worker (an unreferenced active
// worker would otherwise keep the process alive forever). Movable, not
// copyable.
// ---------------------------------------------------------------------------
template <class In, class Out>
class WorkerHandle {
public:
    using MessageListener = typename ChannelPort<Out>::MessageListener;
    using ErrorListener = typename ChannelPort<Out>::ErrorListener;
    using RequestPromise = typename ChannelPort<Out>::RequestPromise;

    WorkerHandle(std::shared_ptr<WorkerChannel<In, Out>> channel, std::thread thread)
        : channel_(std::move(channel)), thread_(std::move(thread)) {}

    ~WorkerHandle() { terminate(); }

    WorkerHandle(WorkerHandle&& other) noexcept
        : channel_(std::move(other.channel_)), thread_(std::move(other.thread_)) {}

    WorkerHandle& operator=(WorkerHandle&& other) noexcept {
        if (this != &other) {
            terminate();
            channel_ = std::move(other.channel_);
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    WorkerHandle(const WorkerHandle&) = delete;
    WorkerHandle& operator=(const WorkerHandle&) = delete;

    // Fire-and-forget structured clone to the worker.
    template <class U = In, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    void postMessage(const U& value) {
        channel_->toWorker().send(value);
    }

    void onMessage(MessageListener listener) {
        channel_->toMain().onMessage(std::move(listener));
    }

    void onError(ErrorListener listener) {
        channel_->toMain().onError(std::move(listener));
    }

    // One-shot ask/answer: resolves with the next Out message.
    template <class U = In, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    RequestPromise request(const U& value) {
        RequestPromise promise = RequestPromise::pending();
        channel_->toMain().addRequest(promise);
        channel_->toWorker().send(value);
        return promise;
    }

    // Stop the worker loop, free the thread. Idempotent.
    void terminate() noexcept {
        if (channel_) {
            channel_->requestStop();
            channel_.reset();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool terminated() const noexcept { return !channel_; }

private:
    std::shared_ptr<WorkerChannel<In, Out>> channel_;
    std::thread thread_;
};

// ---------------------------------------------------------------------------
// startWorker - spawn the worker thread and return its main-side handle.
//
// `entry` is the worker unit's default export (a function pointer). It runs
// once on the new thread; the thread then serves the channel's loop until the
// main side terminates it. The main loop is held open (retainExternal) while
// the thread lives, and released exactly once when the thread exits - including
// the start-up-throw path, which reports the error to the main side first.
// ---------------------------------------------------------------------------
namespace detail {

template <class In, class Out>
void workerThreadMain(const std::shared_ptr<WorkerChannel<In, Out>>& channel,
                      void (*entry)(ThreadHandle<In, Out>&), EventLoop& mainLoop) {
    struct MainLoopRelease {
        EventLoop* loop;
        ~MainLoopRelease() { loop->releaseExternal(); }
    } release{&mainLoop};

    EventLoop& workerLoop = currentLoop();
    if (!channel->bindWorker(workerLoop)) {
        return;  // terminate() won the race: never started
    }
    struct WorkerLoopRelease {
        WorkerChannel<In, Out>* channel;
        ~WorkerLoopRelease() { channel->workerExiting(); }
    } workerRelease{channel.get()};

    ThreadHandle<In, Out> thread(channel);
    try {
        entry(thread);
    } catch (...) {
        // Start-up throw: an "error" event to the main side, then the thread
        // ends (D80). A rejected/observed promise is not involved here.
        channel->toMain().sendError(Thrown::fromCurrentException());
        return;
    }
    workerLoop.run();
}

}  // namespace detail

template <class In, class Out>
WorkerHandle<In, Out> startWorker(void (*entry)(ThreadHandle<In, Out>&)) {
    EventLoop& mainLoop = currentLoop();
    auto channel = std::make_shared<WorkerChannel<In, Out>>();
    channel->bindMain(mainLoop);

    // A live worker keeps the main loop alive until the thread exits.
    mainLoop.retainExternal();

    std::thread thread;
    try {
        thread = std::thread([channel, entry, &mainLoop]() {
            detail::workerThreadMain<In, Out>(channel, entry, mainLoop);
        });
    } catch (...) {
        mainLoop.releaseExternal();
        throw;
    }
    return WorkerHandle<In, Out>(std::move(channel), std::move(thread));
}

}  // namespace cp3::runtime
