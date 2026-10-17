// =============================================================================
// cp3/promise.hpp — `Promise<T>`, the C++20-coroutine `await` bridge, and the
// async ABI surface. Implementation lives in cp3/promise_impl.hpp; the
// combinators live in cp3/promise_combinators.hpp. Including THIS header gives
// the whole async surface (both are included at the bottom).
//
// OWNERSHIP: runtime module. This is THE async ABI that codegen targets.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   cp3 `Promise<T>`            -> `::cp3::runtime::Promise<T>`
//   cp3 `Promise<void>`         -> `::cp3::runtime::Promise<void>`
//   `async function f(): R`     -> a C++20 coroutine whose promise type is
//                                  `Promise<R>::promise_type`; the body uses
//                                  `co_return` for `R` (or `co_return;` for
//                                  `Promise<void>`).
//   `await expr`                -> `co_await ::cp3::runtime::awaitValue(expr)`
//                                  ALWAYS wrap: `awaitValue` yields the value
//                                  itself for a non-Promise (DECISIONS D41) and
//                                  the awaited value for a Promise.
//   `p.then(f)`                 -> `p.then(f)`
//   `p.catch(f)`                -> `p.catchError(f)`  (`catch` is a C++ keyword)
//   `p.finally(f)`              -> `p.finally(f)`
//   `Promise.resolve(v)`        -> `::cp3::runtime::Promise<T>::resolve(v)`
//   `Promise.reject(v)`         -> `Promise<T>::reject(Thrown::make(v))`
//   `Promise.all/race/allSettled([...])`
//                               -> `::cp3::runtime::all|race|allSettled(vec)`
//   `sleep(ms)`                 -> `::cp3::runtime::sleep(ms)` (Promise<void>)
//
// A rejection handler (`.catch`, `then`'s second argument) takes a
// `::cp3::runtime::Thrown` and unboxes the cp3-level thrown value itself.
//
// Generated `main()` must finish with `::cp3::runtime::runEventLoop();` so the
// loop drains before the process exits (spec §14 "Program exit"). Top-level
// `await` is supported by wrapping the entry unit's top-level statements in an
// async coroutine and then running the loop.
//
// The include set generated code needs (each header is self-sufficient):
//   cp3/runtime.hpp  cp3/value.hpp  cp3/error.hpp  cp3/eventloop.hpp
//   cp3/promise.hpp  cp3/worker.hpp cp3/parallel.hpp cp3/console.hpp
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * One loop per thread (cp3/eventloop.hpp); continuations are microtasks, so
//     JS ordering holds.
//   * `async` bodies are EAGER: calling one runs it synchronously up to its
//     first real suspension point, exactly like JS (spec §14's "async inlining"
//     note is then purely an optimisation).
//   * Chaining (DECISIONS D42): `then`/`catchError`/`finally` return a NEW
//     promise; a handler returning a Promise is FLATTENED (the derived promise
//     adopts the inner one) — JS semantics.
//   * Rejection reasons are `cp3::runtime::Thrown` (DECISIONS D39: any value can
//     be thrown). `await_resume` throws that `Thrown` as a C++ exception, so a
//     plain cp3 `try`/`catch` around `await` works unchanged, and a rejection
//     that escapes an async body rejects that body's promise.
//   * Unhandled rejection (DECISIONS D43): a rejected promise with no
//     `catchError`/`then(onRej)`/`await` observing it is reported when the loop
//     drains; the process exits with `kUnhandledRejectionExitCode` (nonzero).
// =============================================================================
#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/error.hpp"
#include "cp3/eventloop.hpp"
#include "cp3/runtime.hpp"
#include "cp3/value.hpp"

namespace cp3::runtime {

/// The exit code used when an unhandled rejection terminates the process.
/// DECISIONS D43 defers the exact value; this is the single place to change it.
inline constexpr int kUnhandledRejectionExitCode = kFailureExitCode;

template <class T>
class Promise;

template <class T>
struct PromiseAwaiter;

template <class T>
struct ReadyAwaiter;

template <class T>
struct PromiseCoroutine;

// =============================================================================
// detail — shared state, classification traits, declarations of the machinery
// =============================================================================
namespace detail {

/// A value slot that exists for every T except void.
template <class T>
struct ValueSlot {
    T value{};
};
template <>
struct ValueSlot<void> {};

enum class PromiseStatus { Pending, Fulfilled, Rejected };

/// The shared state behind a `Promise<T>` handle. Reference-counted; the promise
/// handle is just a `shared_ptr` to it. The boxed state outlives the coroutine
/// frame (which is why `final_suspend` may destroy the frame immediately).
template <class T>
struct PromiseState {
    PromiseStatus status = PromiseStatus::Pending;
    ValueSlot<T> slot;                 // valid iff Fulfilled
    Thrown reason;                     // valid iff Rejected
    std::vector<std::function<void()>> onFulfilled;
    std::vector<std::function<void()>> onRejected;

    // Set while this (rejected, unobserved) promise is tracked as an unhandled
    // rejection; cleared as soon as a rejection handler observes it.
    std::shared_ptr<bool> unhandled;
};

/// True when `T` is a `Promise<...>` specialization.
template <class T>
struct is_promise : std::false_type {};
template <class T>
struct is_promise<Promise<T>> : std::true_type {};
template <class T>
inline constexpr bool is_promise_v = is_promise<std::remove_cvref_t<T>>::value;

/// The value type a handler's return unwraps to (Promise<U> -> U, else R).
template <class R>
struct unwrap {
    using type = std::remove_cvref_t<R>;
};
template <class U>
struct unwrap<Promise<U>> {
    using type = U;
};
template <class R>
using unwrap_t = typename unwrap<std::remove_cvref_t<R>>::type;

/// Invoke a promise handler with its contextual argument (the fulfilment value
/// for `then`, the rejection reason for a rejection handler). JS always hands
/// the argument over, but a handler may declare fewer parameters and ignore it
/// (`g().then(() => …)`, M2-PLAN bug 7): the nullary form below keeps that
/// compilable without knowing the handler's arity up front. Invocation and
/// `HandlerResult` below must agree, so both go through here.
template <class F, class A>
auto invokeHandler(F& f, const A& arg) {
    if constexpr (std::is_invocable_v<F&, const A&>) {
        return f(arg);
    } else {
        (void)arg;  // the handler ignores the value, exactly like JS
        return f();
    }
}

/// The result type of calling a then-handler for a promise of value type T.
template <class T, class F, bool IsVoid = std::is_void_v<T>>
struct HandlerResult {
    using type = decltype(
        invokeHandler(std::declval<F&>(), std::declval<const T&>()));
};
template <class T, class F>
struct HandlerResult<T, F, true> {
    using type = std::invoke_result_t<F>;
};
template <class T, class F>
using handler_result_t = typename HandlerResult<T, F>::type;

/// Value access that works for void.
template <class T>
struct ValueAccess {
    static const T& get(const PromiseState<T>& state) { return state.slot.value; }
};
template <>
struct ValueAccess<void> {
    static void get(const PromiseState<void>&) {}
};

// --- state transitions (defined in cp3/promise_impl.hpp) ------------------
template <class T>
void settleFulfilled(const std::shared_ptr<PromiseState<T>>& state);

template <class T, class V>
void settleFulfilled(const std::shared_ptr<PromiseState<T>>& state, V&& value);

template <class T>
void settleRejected(const std::shared_ptr<PromiseState<T>>& state, Thrown reason);

template <class T>
bool isSettled(const std::shared_ptr<PromiseState<T>>& state) noexcept {
    return state->status != PromiseStatus::Pending;
}
template <class T>
bool isFulfilled(const std::shared_ptr<PromiseState<T>>& state) noexcept {
    return state->status == PromiseStatus::Fulfilled;
}
template <class T>
bool isRejected(const std::shared_ptr<PromiseState<T>>& state) noexcept {
    return state->status == PromiseStatus::Rejected;
}

/// Box the exception currently being handled into a rejection on `state`.
template <class T>
void boxCurrentException(const std::shared_ptr<PromiseState<T>>& state) noexcept;

// --- continuation registration (defined in cp3/promise_impl.hpp) ----------
template <class T>
void addFulfilledContinuation(const std::shared_ptr<PromiseState<T>>& state,
                              std::function<void()> fn);
template <class T>
void addRejectedContinuation(const std::shared_ptr<PromiseState<T>>& state,
                             std::function<void()> fn);

// --- unhandled-rejection tracking (defined in promise.cpp) ----------------
void trackUnhandled(const std::shared_ptr<bool>& flag, const Thrown& reason);
void clearUnhandled(const std::shared_ptr<bool>& flag);
void installUnhandledRejectionHook(EventLoop& loop);

/// Record that `state` has been observed by a rejection handler / `await`, so
/// it must not be reported as an unhandled rejection (DECISIONS D43).
template <class T>
void markObserved(const std::shared_ptr<PromiseState<T>>& state) {
    if (state->unhandled) {
        *state->unhandled = false;
        clearUnhandled(state->unhandled);
        state->unhandled.reset();
    }
}

// --- chaining machinery (defined in cp3/promise_impl.hpp) -----------------
template <class T, class F>
auto thenImpl(const Promise<T>& self, F onFulfilled)
    -> Promise<unwrap_t<handler_result_t<T, F>>>;

template <class T, class F, class R>
auto thenImpl(const Promise<T>& self, F onFulfilled, R onRejected)
    -> Promise<unwrap_t<handler_result_t<T, F>>>;

template <class T, class R>
Promise<T> catchImpl(const Promise<T>& self, R onRejected);

template <class T, class F>
Promise<T> finallyImpl(const Promise<T>& self, F onFinally);

/// Resolve `derived` with `value`: flattens a returned Promise, else fulfils.
template <class D, class R>
void resolveWith(Promise<D> derived, R&& value);

template <class D>
void resolveVoid(Promise<D> derived);

template <class D>
void passThrough(const std::shared_ptr<PromiseState<D>>& state, Promise<D> derived);

template <class D>
void rejectWith(Promise<D> derived, const Thrown& reason);

/// Adopt `inner` into `derived` (same value type): forward both settlements.
template <class D>
void adopt(Promise<D> inner, Promise<D> derived);

template <class D>
void rejectFromCurrentException(Promise<D> derived) noexcept;

}  // namespace detail

// =============================================================================
// Promise<T>
// =============================================================================
template <class T>
class Promise {
public:
    using value_type = T;
    using state_type = detail::PromiseState<T>;
    using promise_type = PromiseCoroutine<T>;

    /// A pending promise with no producer. Prefer `resolve()`/`reject()` or a
    /// coroutine; a never-settled promise simply never resolves (the loop waits
    /// for pending *work*, never for promises — spec §14).
    Promise() : state_(std::make_shared<state_type>()) {}
    explicit Promise(std::shared_ptr<state_type> state) : state_(std::move(state)) {}

    static Promise pending() { return Promise(); }

    /// `Promise.resolve(v)` (DECISIONS D40). For `Promise<void>`, `resolve()`.
    template <class U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    static Promise resolve(U value) {
        Promise p;
        detail::settleFulfilled(p.state_, std::move(value));
        return p;
    }
    template <class U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    static Promise resolve() {
        Promise p;
        detail::settleFulfilled(p.state_);
        return p;
    }

    /// `Promise.reject(reason)` (DECISIONS D40).
    static Promise reject(Thrown reason) {
        Promise p;
        detail::settleRejected(p.state_, std::move(reason));
        return p;
    }

    bool settled() const noexcept { return detail::isSettled(state_); }
    bool fulfilled() const noexcept { return detail::isFulfilled(state_); }
    bool rejected() const noexcept { return detail::isRejected(state_); }

    /// The rejection reason. Precondition: `rejected()`.
    const Thrown& reason() const { return state_->reason; }

    /// The fulfilment value. Precondition: `fulfilled()` and `!is_void_v<T>`.
    template <class U = T>
    const U& value() const {
        return detail::ValueAccess<U>::get(*state_);
    }

    /// Settle an existing promise (worker, chaining, codegen helpers).
    template <class U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    void fulfill(U value) {
        detail::settleFulfilled(state_, std::move(value));
    }
    template <class U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    void fulfill() {
        detail::settleFulfilled(state_);
    }
    void rejectWith(Thrown reason) { detail::settleRejected(state_, std::move(reason)); }

    /// The shared state (used by the runtime's own machinery; internal).
    const std::shared_ptr<state_type>& sharedState() const noexcept { return state_; }

    // --- chaining (DECISIONS D42); defined in cp3/promise_impl.hpp ----------
    template <class F>
    auto then(F onFulfilled) const
        -> Promise<detail::unwrap_t<detail::handler_result_t<T, F>>>;

    template <class F, class R>
    auto then(F onFulfilled, R onRejected) const
        -> Promise<detail::unwrap_t<detail::handler_result_t<T, F>>>;

    /// cp3 `.catch(f)` — spelled `catchError` (`catch` is a C++ keyword).
    template <class R>
    Promise<T> catchError(R onRejected) const;

    template <class F>
    Promise<T> finally(F onFinally) const;

    /// The await bridge. Codegen always goes through `awaitValue` instead.
    PromiseAwaiter<T> operator co_await() const { return PromiseAwaiter<T>{*this}; }

private:
    std::shared_ptr<state_type> state_;
};

// =============================================================================
// Coroutine support (the `promise_type` of an `async function`)
// =============================================================================
template <class T>
struct PromiseCoroutine {
    std::shared_ptr<detail::PromiseState<T>> state = std::make_shared<detail::PromiseState<T>>();

    Promise<T> get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept { return {}; }  // eager, like JS
    std::suspend_never final_suspend() noexcept { return {}; }    // state outlives frame
    void return_value(T value) { detail::settleFulfilled(state, std::move(value)); }
    void unhandled_exception() noexcept { detail::boxCurrentException(state); }
};

template <>
struct PromiseCoroutine<void> {
    std::shared_ptr<detail::PromiseState<void>> state =
        std::make_shared<detail::PromiseState<void>>();

    Promise<void> get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() { detail::settleFulfilled(state); }
    void unhandled_exception() noexcept { detail::boxCurrentException(state); }
};

// =============================================================================
// Awaiter
// =============================================================================

/// `co_await awaitValue(promise)`. The coroutine always suspends: a settled
/// promise resumes from the microtask queue (behind already-queued
/// continuations, like JS `await`); a pending one resumes when it settles.
template <class T>
struct PromiseAwaiter {
    Promise<T> promise;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        const std::shared_ptr<detail::PromiseState<T>> st = promise.sharedState();
        // Registering the rejection side also marks the promise observed, so
        // `await` counts as handling a rejection (DECISIONS D43).
        detail::addFulfilledContinuation<T>(st, [handle]() { handle.resume(); });
        detail::addRejectedContinuation<T>(st, [handle]() { handle.resume(); });
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

/// `co_await awaitValue(nonPromise)` — DECISIONS D41: yields the value itself.
template <class T>
struct ReadyAwaiter {
    T value;

    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    T await_resume() { return std::move(value); }
};

/// The one spelling codegen uses for every `await`.
template <class T>
PromiseAwaiter<T> awaitValue(Promise<T> promise) {
    return PromiseAwaiter<T>{std::move(promise)};
}

template <class X, std::enable_if_t<!detail::is_promise_v<X>, int> = 0>
ReadyAwaiter<std::remove_cvref_t<X>> awaitValue(X&& value) {
    return ReadyAwaiter<std::remove_cvref_t<X>>{std::forward<X>(value)};
}

// =============================================================================
// `sleep(ms)` — core builtin, `Promise<void>` (STDLIB docs §03)
// =============================================================================
Promise<void> sleep(double milliseconds);

}  // namespace cp3::runtime

// The machinery (state transitions, chaining) and the combinators. Included
// here so `#include "cp3/promise.hpp"` is the single codegen entry point.
#include "cp3/promise_impl.hpp"
#include "cp3/promise_combinators.hpp"
