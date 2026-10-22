// =============================================================================
// cp3/async/promise_impl.hpp - the Promise machinery: state transitions, continuation
// registration, and the `then` / `catchError` / `finally` handlers.
//
// OWNERSHIP: runtime module. Included by cp3/async/promise.hpp (the public async ABI);
// generated code includes cp3/async/promise.hpp, never this file directly.
// =============================================================================
#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/async/promise.hpp"

namespace cp3::runtime {
namespace detail {

// --- state transitions ------------------------------------------------------

template <class T>
void settleFulfilled(const std::shared_ptr<PromiseState<T>>& state) {
    static_assert(std::is_void_v<T>, "settleFulfilled(state) is for Promise<void>");
    if (state->status != PromiseStatus::Pending) {
        return;
    }
    state->status = PromiseStatus::Fulfilled;
    state->unhandled.reset();
    // The rejected-only entries the promise can no longer run are dropped by
    // the drain, after the fulfilment entries have been posted.
    state->continuations.drainTo(currentLoop(), true);
}

template <class T, class V>
void settleFulfilled(const std::shared_ptr<PromiseState<T>>& state, V&& value) {
    static_assert(!std::is_void_v<T>, "use settleFulfilled(state) for Promise<void>");
    if (state->status != PromiseStatus::Pending) {
        return;
    }
    state->status = PromiseStatus::Fulfilled;
    state->slot.value = std::forward<V>(value);
    state->unhandled.reset();
    state->continuations.drainTo(currentLoop(), true);
}

template <class T>
void settleRejected(const std::shared_ptr<PromiseState<T>>& state, Thrown reason) {
    if (state->status != PromiseStatus::Pending) {
        return;
    }
    state->status = PromiseStatus::Rejected;
    state->reason = std::move(reason);

    if (!state->continuations.hasRejectedObserver()) {
        // Fulfilment-only continuations can no longer run. Drop them now, as the
        // separate fulfilment list did, so a rejected promise does not keep
        // their captures (and the states they hold) alive.
        state->continuations.clear();
        // No observer: unhandled unless one arrives before the loop drains (D43).
        if (!state->unhandled) {
            state->unhandled = std::make_shared<bool>(true);
            trackUnhandled(state->unhandled, state->reason);
        }
        return;
    }
    state->unhandled.reset();
    state->continuations.drainTo(currentLoop(), false);
}

template <class T>
void boxCurrentException(const std::shared_ptr<PromiseState<T>>& state) noexcept {
    try {
        throw;
    } catch (const Thrown& thrown) {
        settleRejected(state, thrown);
    } catch (const std::exception& ex) {
        settleRejected(state, Thrown::make(std::string(ex.what())));
    } catch (...) {
        settleRejected(state, Thrown::fromCurrentException());
    }
}

// --- continuation registration ---------------------------------------------

template <class T>
void addContinuation(const std::shared_ptr<PromiseState<T>>& state, Task fn,
                     ContinuationKind kind) {
    switch (state->status) {
        case PromiseStatus::Fulfilled:
            if (kind != ContinuationKind::Rejected) {
                currentLoop().post(std::move(fn));
            }
            break;
        case PromiseStatus::Pending:
            state->continuations.push_back(std::move(fn), kind);
            break;
        case PromiseStatus::Rejected:
            if (kind != ContinuationKind::Fulfilled) {
                markObserved(state);
                currentLoop().post(std::move(fn));
            }
            break;
    }
}

// --- settling a derived promise --------------------------------------------

template <class D>
void resolveVoid(Promise<D> derived) {
    static_assert(std::is_void_v<D>, "resolveVoid is for Promise<void>");
    derived.fulfill();
}

template <class D>
void passThrough(const std::shared_ptr<PromiseState<D>>& state, Promise<D> derived) {
    if constexpr (std::is_void_v<D>) {
        (void)state;
        derived.fulfill();
    } else {
        derived.fulfill(state->slot.value);
    }
}

template <class D, class R>
void resolveWith(Promise<D> derived, R&& value) {
    using Raw = std::remove_cvref_t<R>;
    if constexpr (is_promise_v<Raw>) {
        adopt<D>(static_cast<Promise<D>>(std::forward<R>(value)), derived);
    } else if constexpr (std::is_void_v<D>) {
        (void)value;
        derived.fulfill();
    } else {
        derived.fulfill(std::forward<R>(value));
    }
}

template <class D>
void rejectWith(Promise<D> derived, const Thrown& reason) {
    derived.rejectWith(reason);
}

template <class D>
void adopt(Promise<D> inner, Promise<D> derived) {
    const std::shared_ptr<PromiseState<D>> st = inner.sharedState();
    addFulfilledContinuation<D>(st, [st, derived]() mutable { passThrough<D>(st, derived); });
    addRejectedContinuation<D>(st, [st, derived]() mutable { derived.rejectWith(st->reason); });
}

template <class D>
void rejectFromCurrentException(Promise<D> derived) noexcept {
    try {
        throw;
    } catch (const Thrown& thrown) {
        derived.rejectWith(thrown);
    } catch (const std::exception& ex) {
        derived.rejectWith(Thrown::make(std::string(ex.what())));
    } catch (...) {
        derived.rejectWith(Thrown::fromCurrentException());
    }
}

// --- handler invocation ----------------------------------------------------

// `R` is the handler's (adapted) result type - see `detail::invokeHandler` and
// `detail::handler_result_t` in cp3/async/promise.hpp.

template <class T, class R, class D, class F>
void invokeFulfilled(const std::shared_ptr<PromiseState<T>>& st, Promise<D> derived,
                     F& onFulfilled) {
    if constexpr (std::is_void_v<T>) {
        if constexpr (std::is_void_v<R>) {
            onFulfilled();
            resolveVoid<D>(derived);
        } else {
            resolveWith<D>(derived, onFulfilled());
        }
    } else {
        if constexpr (std::is_void_v<R>) {
            (void)invokeHandler(onFulfilled, st->slot.value);
            resolveVoid<D>(derived);
        } else {
            resolveWith<D>(derived, invokeHandler(onFulfilled, st->slot.value));
        }
    }
}

template <class D, class F>
void invokeRejected(Promise<D> derived, const Thrown& reason, F& onRejected) {
    using RR = handler_result_t<Thrown, F>;
    if constexpr (std::is_void_v<RR>) {
        // A void rejection handler fulfills the derived promise. For a void
        // derived that is a plain fulfilment; for a non-void derived JS parity
        // says "undefined", which cp3 approximates with a value-initialised D.
        (void)invokeHandler(onRejected, reason);
        if constexpr (std::is_void_v<D>) {
            derived.fulfill();
        } else {
            derived.fulfill(D{});
        }
    } else if constexpr (is_promise_v<RR>) {
        resolveWith<D>(derived, invokeHandler(onRejected, reason));
    } else if constexpr (std::is_void_v<D>) {
        (void)invokeHandler(onRejected, reason);
        derived.fulfill();
    } else {
        derived.fulfill(invokeHandler(onRejected, reason));
    }
}

template <class T, class R, class D, class F>
std::function<void()> makeFulfilledStep(const std::shared_ptr<PromiseState<T>>& st,
                                        Promise<D> derived, F onFulfilled) {
    return [st, derived, onFulfilled = std::move(onFulfilled)]() mutable {
        try {
            invokeFulfilled<T, R, D, F>(st, derived, onFulfilled);
        } catch (...) {
            rejectFromCurrentException<D>(derived);
        }
    };
}

template <class T, class D, class F>
std::function<void()> makeRejectedStep(const std::shared_ptr<PromiseState<T>>& st,
                                       Promise<D> derived, F onRejected) {
    return [st, derived, onRejected = std::move(onRejected)]() mutable {
        try {
            invokeRejected<D, F>(derived, st->reason, onRejected);
        } catch (...) {
            rejectFromCurrentException<D>(derived);
        }
    };
}

template <class T, class D>
std::function<void()> makeRejectForwardStep(const std::shared_ptr<PromiseState<T>>& st,
                                            Promise<D> derived) {
    return [st, derived]() mutable { rejectWith<D>(derived, st->reason); };
}

template <class D>
std::function<void()> makePassThroughStep(const std::shared_ptr<PromiseState<D>>& st,
                                          Promise<D> derived) {
    return [st, derived]() mutable { passThrough<D>(st, derived); };
}

// --- then / catch ----------------------------------------------------------

template <class T, class F>
auto thenImpl(const Promise<T>& self, F onFulfilled)
    -> Promise<unwrap_t<handler_result_t<T, F>>> {
    using R = handler_result_t<T, F>;
    using D = unwrap_t<R>;
    Promise<D> derived = Promise<D>::pending();
    const std::shared_ptr<PromiseState<T>> st = self.sharedState();
    addFulfilledContinuation<T>(
        st, makeFulfilledStep<T, R, D, F>(st, derived, std::move(onFulfilled)));
    addRejectedContinuation<T>(st, makeRejectForwardStep<T, D>(st, derived));
    return derived;
}

template <class T, class F, class R>
auto thenImpl(const Promise<T>& self, F onFulfilled, R onRejected)
    -> Promise<unwrap_t<handler_result_t<T, F>>> {
    using FR = handler_result_t<T, F>;
    using D = unwrap_t<FR>;
    Promise<D> derived = Promise<D>::pending();
    const std::shared_ptr<PromiseState<T>> st = self.sharedState();
    addFulfilledContinuation<T>(
        st, makeFulfilledStep<T, FR, D, F>(st, derived, std::move(onFulfilled)));
    addRejectedContinuation<T>(
        st, makeRejectedStep<T, D, R>(st, derived, std::move(onRejected)));
    return derived;
}

template <class T, class R>
Promise<T> catchImpl(const Promise<T>& self, R onRejected) {
    Promise<T> derived = Promise<T>::pending();
    const std::shared_ptr<PromiseState<T>> st = self.sharedState();
    addFulfilledContinuation<T>(st, makePassThroughStep<T>(st, derived));
    addRejectedContinuation<T>(st, makeRejectedStep<T, T, R>(st, derived, std::move(onRejected)));
    return derived;
}

// --- finally ---------------------------------------------------------------

template <class T, class F>
void runFinallyFulfilled(const std::shared_ptr<PromiseState<T>>& st, Promise<T> derived,
                         F& onFinally) {
    using RR = std::invoke_result_t<F>;
    if constexpr (is_promise_v<RR>) {
        using CU = unwrap_t<RR>;
        const Promise<CU> cleanup = onFinally();
        addFulfilledContinuation<CU>(cleanup.sharedState(), makePassThroughStep<T>(st, derived));
        addRejectedContinuation<CU>(cleanup.sharedState(),
                                    makeRejectForwardStep<CU, T>(cleanup.sharedState(), derived));
    } else {
        onFinally();
        passThrough<T>(st, derived);
    }
}

template <class T, class F>
void runFinallyRejected(const std::shared_ptr<PromiseState<T>>& st, Promise<T> derived,
                        F& onFinally) {
    using RR = std::invoke_result_t<F>;
    if constexpr (is_promise_v<RR>) {
        using CU = unwrap_t<RR>;
        const Promise<CU> cleanup = onFinally();
        addFulfilledContinuation<CU>(cleanup.sharedState(),
                                     makeRejectForwardStep<T, T>(st, derived));
        addRejectedContinuation<CU>(cleanup.sharedState(),
                                    makeRejectForwardStep<CU, T>(cleanup.sharedState(), derived));
    } else {
        onFinally();
        derived.rejectWith(st->reason);
    }
}

template <class T, class F>
std::function<void()> makeFinallyFulfilledStep(const std::shared_ptr<PromiseState<T>>& st,
                                               Promise<T> derived, F onFinally) {
    return [st, derived, onFinally]() mutable {
        try {
            runFinallyFulfilled<T, F>(st, derived, onFinally);
        } catch (...) {
            rejectFromCurrentException<T>(derived);
        }
    };
}

template <class T, class F>
std::function<void()> makeFinallyRejectedStep(const std::shared_ptr<PromiseState<T>>& st,
                                              Promise<T> derived, F onFinally) {
    return [st, derived, onFinally]() mutable {
        try {
            runFinallyRejected<T, F>(st, derived, onFinally);
        } catch (...) {
            rejectFromCurrentException<T>(derived);
        }
    };
}

template <class T, class F>
Promise<T> finallyImpl(const Promise<T>& self, F onFinally) {
    Promise<T> derived = Promise<T>::pending();
    const std::shared_ptr<PromiseState<T>> st = self.sharedState();
    addFulfilledContinuation<T>(st, makeFinallyFulfilledStep<T, F>(st, derived, onFinally));
    addRejectedContinuation<T>(st, makeFinallyRejectedStep<T, F>(st, derived, onFinally));
    return derived;
}

}  // namespace detail

// =============================================================================
// Coroutine glue (out-of-line: `Promise<T>` is complete here)
// =============================================================================
template <class T>
Promise<T> PromiseCoroutine<T>::get_return_object() noexcept {
    return Promise<T>(state);
}

inline Promise<void> PromiseCoroutine<void>::get_return_object() noexcept {
    return Promise<void>(state);
}

// =============================================================================
// Promise<T>::then / catchError / finally
// =============================================================================
template <class T>
template <class F>
auto Promise<T>::then(F onFulfilled) const
    -> Promise<detail::unwrap_t<detail::handler_result_t<T, F>>> {
    return detail::thenImpl<T, F>(*this, std::move(onFulfilled));
}

template <class T>
template <class F, class R>
auto Promise<T>::then(F onFulfilled, R onRejected) const
    -> Promise<detail::unwrap_t<detail::handler_result_t<T, F>>> {
    return detail::thenImpl<T, F, R>(*this, std::move(onFulfilled), std::move(onRejected));
}

template <class T>
template <class R>
Promise<T> Promise<T>::catchError(R onRejected) const {
    return detail::catchImpl<T, R>(*this, std::move(onRejected));
}

template <class T>
template <class F>
Promise<T> Promise<T>::finally(F onFinally) const {
    return detail::finallyImpl<T, F>(*this, std::move(onFinally));
}

}  // namespace cp3::runtime
