// =============================================================================
// cp3/async/awaiters.hpp - the `await` bridge used by generated code.
//
// The `Promise<T>` class and the coroutine machinery live in promise.hpp; this
// header holds the awaiter types so promise.hpp stays within its size budget.
// promise.hpp includes this file at the bottom, so `#include
// "cp3/async/promise.hpp"` remains the single codegen entry point.
// =============================================================================
#pragma once

#include <coroutine>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"

namespace cp3::runtime {

/// `co_await awaitValue(promise)`. The coroutine always suspends: a settled
/// promise resumes from the microtask queue (behind already-queued
/// continuations, like JS `await`); a pending one resumes when it settles.
/// Resuming inline would run the rest of the callee before the caller regains
/// control after the call, which is observable (see the async ordering tests).
template <class T>
struct PromiseAwaiter {
    Promise<T> promise;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        // One continuation observes both settlements: registering it also marks
        // the promise observed, so `await` counts as handling a rejection
        // (DECISIONS D43). `sharedState()` returns a reference, so this does not
        // pay a refcount pair on the hottest path.
        detail::addSettledContinuation<T>(promise.sharedState(),
                                          [handle]() { handle.resume(); });
    }

    /// A rejection rethrows `Thrown`; the enclosing async body turns it back
    /// into a rejection (PromiseCoroutine::unhandled_exception).
    T await_resume() {
        if (promise.rejected()) {
            // The `await` itself observes the rejection (DECISIONS D43); the
            // registration in `await_suspend` already did for settled ones.
            detail::markObserved(promise.sharedState());
            throw promise.reason();
        }
        return detail::ValueAccess<T>::get(*promise.sharedState());
    }
};

/// `co_await awaitValue(nonPromise)` - DECISIONS D41: yields the value itself.
template <class T>
struct ReadyAwaiter {
    T value;

    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    T await_resume() { return std::move(value); }
};

/// `co_await valueAwait(v)` - codegen's lowering of `await Promise.resolve(v)`
/// when `v` is not itself a promise. The temporary promise is never observed,
/// so its state allocation is pure overhead. It still suspends: the resume is
/// posted as a microtask at exactly the point the fulfilled temporary's
/// continuation was posted. Resuming inline would run the rest of the callee
/// before the caller regains control, which is observable, so `await_ready`
/// stays false (the same rule as `PromiseAwaiter`).
template <class T>
struct ValueAwaiter {
    T value;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        currentLoop().post(Task([handle]() { handle.resume(); }));
    }
    T await_resume() { return std::move(value); }
};

template <class X>
ValueAwaiter<std::remove_cvref_t<X>> valueAwait(X&& value) {
    return ValueAwaiter<std::remove_cvref_t<X>>{std::forward<X>(value)};
}

/// `co_await sleepAwait(ms)` - the direct await of `sleep(ms)`.
///
/// Awaiting the temporary `sleep(...)` promise is the common timer shape, and
/// routing it through a `Promise<void>` state costs one shared allocation plus
/// the continuation bookkeeping. This awaiter arms the same timer, but posts
/// the resume as a microtask from the timer callback instead of resuming
/// inline: that is exactly where the ordinary `Promise<void>` continuation ran
/// (a queued microtask), so FIFO order with microtasks queued earlier in the
/// same timer batch is unchanged. A program that binds the promise
/// (`const p = sleep(ms)`) or chains on it keeps the ordinary path.
struct SleepAwaiter {
    double milliseconds = 0;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        const double clamped = milliseconds < 0 ? 0.0 : milliseconds;
        currentLoop().addTimer(static_cast<std::int64_t>(clamped), Task([handle]() {
            currentLoop().post(Task([handle]() { handle.resume(); }));
        }));
    }
    void await_resume() const noexcept {}
};

inline SleepAwaiter sleepAwait(double milliseconds) { return {milliseconds}; }

/// The one spelling codegen uses for every `await`.
template <class T>
PromiseAwaiter<T> awaitValue(Promise<T> promise) {
    return PromiseAwaiter<T>{std::move(promise)};
}

template <class X, std::enable_if_t<!detail::is_promise_v<X>, int> = 0>
ReadyAwaiter<std::remove_cvref_t<X>> awaitValue(X&& value) {
    return ReadyAwaiter<std::remove_cvref_t<X>>{std::forward<X>(value)};
}

}  // namespace cp3::runtime
