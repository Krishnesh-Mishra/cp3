// =============================================================================
// cp3/worker.hpp — `worker(...)`: an isolated, message-passing task on its own
// OS thread, exposing a `Promise` (docs/language-spec/13-concurrency-...).
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   cp3 `worker(data)` where `import worker from "./x.cp3"`
//        -> `::cp3::runtime::worker(data, xDefaultExport)`
//     `xDefaultExport` may be a function pointer or a captureless lambda; it is
//     the module's default export. If the default export is `async`, it returns
//     a `Promise<T>` and `worker` unwraps it, so the cp3 result type is `T`
//     either way.
//
//   The argument is deep-copied across the boundary by `cp3::runtime::clone`
//   (see cp3/value.hpp), so codegen MUST specialize `Cloner<T>` for every
//   object/class type that can be passed to (or returned from) a worker.
//
// -----------------------------------------------------------------------------
// SEMANTICS (spec §13)
// -----------------------------------------------------------------------------
//   * one real OS thread per call — never a process;
//   * nothing is implicitly reachable inside it: the argument is a genuine deep
//     copy in, and the result is deep-copied out;
//   * the worker thread runs its *own* independent event loop (cp3/eventloop.hpp
//     `currentLoop()` is per-thread), so an async worker body works;
//   * the returned `Promise` resolves with the body's value, or rejects with the
//     value the body threw (a plain `try`/`catch` around `await worker(...)`
//     catches it — DECISIONS D36);
//   * a crash inside the worker thread still takes the process down, exactly as
//     the spec allows for beta one.
//
// The thread is detached once it has handed its result back; the caller's loop
// is woken through `EventLoop::postFromThread`.
// =============================================================================
#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "cp3/eventloop.hpp"
#include "cp3/promise.hpp"
#include "cp3/runtime.hpp"
#include "cp3/value.hpp"

namespace cp3::runtime {

// The exit code used when a worker failure cannot cross the boundary as a
// rejection. DECISIONS D36 defers the exact value; single place to change.
inline constexpr int kWorkerFailureExitCode = kFailureExitCode;

// Named spelling of the structured-clone primitive (same as `clone`).
template <class T>
T structuredClone(const T& value) {
    return clone(value);
}

namespace detail {

// Run the worker thread's own loop until `promise` settles, then return the
// value (or `void`), throwing the rejection reason on failure.
template <class R>
R settleWorkerBody(Promise<R> promise) {
    EventLoop& loop = currentLoop();  // this worker thread's own loop
    if (!promise.settled()) {
        // Observe the promise so a rejection doesn't count as unhandled on this
        // thread, then drain this thread's loop.
        addFulfilledContinuation<R>(promise.sharedState(), []() {});
        addRejectedContinuation<R>(promise.sharedState(), []() {});
        loop.run();
    }
    if (promise.rejected()) {
        throw promise.reason();
    }
    if (!promise.settled()) {
        throw Thrown::make(std::string("worker: async body never settled"));
    }
    if constexpr (std::is_void_v<R>) {
        return;
    } else {
        return promise.value();
    }
}

// Call the worker body on the worker thread, then drain that thread's own loop.
// Used when the body is synchronous.
template <class R, class Fn, class Arg>
R runSyncWorkerBody(Fn& fn, Arg&& argument) {
    if constexpr (std::is_void_v<R>) {
        fn(std::forward<Arg>(argument));
        currentLoop().run();
        return;
    } else {
        R out = fn(std::forward<Arg>(argument));
        currentLoop().run();
        return out;
    }
}

// Run the worker body and produce its result, async or not.
template <class R, class Fn, class Arg>
R runWorkerBody(Fn& fn, Arg&& argument) {
    using Raw = std::invoke_result_t<Fn, Arg>;
    if constexpr (is_promise_v<Raw>) {
        Promise<R> inner = fn(std::forward<Arg>(argument));
        return settleWorkerBody<R>(inner);
    } else {
        return runSyncWorkerBody<R>(fn, std::forward<Arg>(argument));
    }
}

// Hand a fulfilled result back to the caller's loop (deep-copied out).
template <class R>
void postWorkerResult(EventLoop& callerLoop, Promise<R> result, R value) {
    R copied = clone<R>(value);
    callerLoop.postFromThread([result, copied = std::move(copied)]() mutable {
        result.fulfill(copied);
    });
}

inline void postWorkerResult(EventLoop& callerLoop, Promise<void> result) {
    callerLoop.postFromThread([result]() mutable { result.fulfill(); });
}

// Hand a rejection back to the caller's loop (deep-copied out).
template <class R>
void postWorkerRejection(EventLoop& callerLoop, Promise<R> result, const Thrown& reason) {
    Thrown copied = reason.deepCopy();
    callerLoop.postFromThread([result, copied]() mutable { result.rejectWith(copied); });
}

// The whole worker-thread body: run, then post the outcome to the caller loop.
template <class R, class Fn, class Arg>
void workerThreadMain(EventLoop& callerLoop, Promise<R> result, Fn& fn, Arg& argument) {
    try {
        if constexpr (std::is_void_v<R>) {
            runWorkerBody<void>(fn, std::move(argument));
            postWorkerResult(callerLoop, result);
        } else {
            postWorkerResult(callerLoop, result, runWorkerBody<R>(fn, std::move(argument)));
        }
    } catch (const Thrown& thrown) {
        postWorkerRejection(callerLoop, result, thrown);
    } catch (const std::exception& ex) {
        postWorkerRejection(callerLoop, result, Thrown::make(std::string(ex.what())));
    } catch (...) {
        postWorkerRejection(callerLoop, result, Thrown::fromCurrentException());
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// worker(arg, body)
// ---------------------------------------------------------------------------
template <class A, class F>
auto worker(A&& arg, F&& body)
    -> Promise<detail::unwrap_t<std::invoke_result_t<std::remove_cvref_t<F>,
                                                    std::remove_cvref_t<A>>>> {
    using Arg = std::remove_cvref_t<A>;
    using Fn = std::remove_cvref_t<F>;
    using Raw = std::invoke_result_t<Fn, Arg>;
    using R = detail::unwrap_t<Raw>;

    EventLoop& callerLoop = currentLoop();
    Promise<R> result = Promise<R>::pending();

    // Deep copy IN on the caller thread: the worker owns its own graph.
    Arg argument = clone<Arg>(arg);
    Fn fn = std::forward<F>(body);

    // Hold the caller's loop open until the worker has posted its result; the
    // loop would otherwise drain and the process exit before the result arrives.
    callerLoop.retainExternal();
    try {
        std::thread thread([argument = std::move(argument), fn = std::move(fn), &callerLoop,
                            result]() mutable {
            detail::workerThreadMain<R>(callerLoop, result, fn, argument);
            callerLoop.releaseExternal();
        });
        thread.detach();
    } catch (...) {
        callerLoop.releaseExternal();
        throw;
    }
    return result;
}

}  // namespace cp3::runtime
