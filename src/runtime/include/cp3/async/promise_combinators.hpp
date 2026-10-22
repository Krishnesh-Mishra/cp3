// =============================================================================
// cp3/async/promise_combinators.hpp - `Promise.all` / `.race` / `.allSettled`.
//
// OWNERSHIP: runtime module. Included by cp3/async/promise.hpp; generated code emits
// `::cp3::runtime::all|race|allSettled(...)` over a `std::vector<Promise<T>>`.
//
//   * `all`        - resolves with the values in input order; rejects as soon as
//                    any promise rejects.
//   * `race`       - settles as soon as any promise settles (first wins).
//   * `allSettled` - always resolves, with each result tagged ok/error
//                    (`Settled<T>`).
//
// All three leave every input promise observed, so combining promises can never
// itself produce an unhandled rejection.
// =============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "cp3/async/promise.hpp"

namespace cp3::runtime {

/// An `allSettled` result: `ok` plus either the value or the rejection reason.
template <class T>
struct Settled {
    bool ok = false;
    T value{};
    Thrown reason;

    static Settled fulfilled(T v) {
        Settled s;
        s.ok = true;
        s.value = std::move(v);
        return s;
    }
    static Settled rejected(Thrown r) {
        Settled s;
        s.ok = false;
        s.reason = std::move(r);
        return s;
    }
};

template <>
struct Settled<void> {
    bool ok = false;
    Thrown reason;

    static Settled fulfilled() {
        Settled s;
        s.ok = true;
        return s;
    }
    static Settled rejected(Thrown r) {
        Settled s;
        s.ok = false;
        s.reason = std::move(r);
        return s;
    }
};

namespace detail {

/// One allocation per `Promise.all` call: the input states, the result slots,
/// the countdown and the derived promise live together, so the per-element
/// continuation captures only this state and its index (24 bytes, inline in the
/// continuation) instead of the element's own state as well (40 bytes, which
/// spilled to the heap). The finished vector moves into the derived promise
/// instead of being copied.
template <class T>
struct AllState {
    // The input handles, moved in (not copied) so the combinator does not pay
    // a refcount pair per element. They keep the element states alive.
    std::vector<Promise<T>> elements;
    std::vector<T> results;
    std::size_t remaining = 0;
    Promise<std::vector<T>> derived;

    AllState(Promise<std::vector<T>> promise, std::vector<Promise<T>> inputs)
        : elements(std::move(inputs)), results(elements.size()),
          remaining(elements.size()), derived(std::move(promise)) {}
};

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<std::vector<T>> allImpl(std::vector<Promise<T>> promises) {
    Promise<std::vector<T>> derived = Promise<std::vector<T>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(std::vector<T>{});
        return derived;
    }
    auto all = std::make_shared<AllState<T>>(derived, std::move(promises));
    for (std::size_t i = 0; i < count; ++i) {
        // One continuation per element observes both settlements: fulfilment
        // copies the value into input order, rejection settles the derived
        // promise with the first reason (later ones are no-ops). The element
        // state is kept alive by `all->elements`, so the continuation captures
        // only this state and its index.
        const std::shared_ptr<PromiseState<T>> element = all->elements[i].sharedState();
        addSettledContinuation<T>(element, [all, i]() {
            const std::shared_ptr<PromiseState<T>>& st = all->elements[i].sharedState();
            if (st->status == PromiseStatus::Rejected) {
                if (!all->derived.settled()) {
                    all->derived.rejectWith(st->reason);
                }
                return;
            }
            all->results[i] = st->slot.value;
            if (--all->remaining == 0) {
                std::vector<T> results = std::move(all->results);
                all->derived.fulfill(std::move(results));
            }
        });
    }
    return derived;
}

inline Promise<void> allVoidImpl(const std::vector<Promise<void>>& promises) {
    Promise<void> derived = Promise<void>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill();
        return derived;
    }
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<void>> st = promises[i].sharedState();
        addFulfilledContinuation<void>(st, [remaining, derived]() mutable {
            if (--*remaining == 0) {
                derived.fulfill();
            }
        });
        addRejectedContinuation<void>(st, [st, derived]() mutable {
            if (!derived.settled()) {
                derived.rejectWith(st->reason);
            }
        });
    }
    return derived;
}

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<T> raceImpl(const std::vector<Promise<T>>& promises) {
    Promise<T> derived = Promise<T>::pending();
    for (const Promise<T>& p : promises) {
        const std::shared_ptr<PromiseState<T>> st = p.sharedState();
        addFulfilledContinuation<T>(st, [st, derived]() mutable {
            if (!derived.settled()) {
                derived.fulfill(st->slot.value);
            }
        });
        addRejectedContinuation<T>(st, [st, derived]() mutable {
            if (!derived.settled()) {
                derived.rejectWith(st->reason);
            }
        });
    }
    return derived;
}

inline Promise<void> raceVoidImpl(const std::vector<Promise<void>>& promises) {
    Promise<void> derived = Promise<void>::pending();
    for (const Promise<void>& p : promises) {
        const std::shared_ptr<PromiseState<void>> st = p.sharedState();
        addFulfilledContinuation<void>(st, [derived]() mutable {
            if (!derived.settled()) {
                derived.fulfill();
            }
        });
        addRejectedContinuation<void>(st, [st, derived]() mutable {
            if (!derived.settled()) {
                derived.rejectWith(st->reason);
            }
        });
    }
    return derived;
}

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<std::vector<Settled<T>>> allSettledImpl(const std::vector<Promise<T>>& promises) {
    Promise<std::vector<Settled<T>>> derived = Promise<std::vector<Settled<T>>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(std::vector<Settled<T>>{});
        return derived;
    }
    auto results = std::make_shared<std::vector<Settled<T>>>(count);
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<T>> st = promises[i].sharedState();
        addFulfilledContinuation<T>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<T>::fulfilled(st->slot.value);
            if (--*remaining == 0) {
                derived.fulfill(*results);
            }
        });
        addRejectedContinuation<T>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<T>::rejected(st->reason);
            if (--*remaining == 0) {
                derived.fulfill(*results);
            }
        });
    }
    return derived;
}

inline Promise<std::vector<Settled<void>>> allSettledVoidImpl(
    const std::vector<Promise<void>>& promises) {
    Promise<std::vector<Settled<void>>> derived =
        Promise<std::vector<Settled<void>>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(std::vector<Settled<void>>{});
        return derived;
    }
    auto results = std::make_shared<std::vector<Settled<void>>>(count);
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<void>> st = promises[i].sharedState();
        addFulfilledContinuation<void>(st, [results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<void>::fulfilled();
            if (--*remaining == 0) {
                derived.fulfill(*results);
            }
        });
        addRejectedContinuation<void>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<void>::rejected(st->reason);
            if (--*remaining == 0) {
                derived.fulfill(*results);
            }
        });
    }
    return derived;
}

}  // namespace detail

/// `Promise.all([...])`.
template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<std::vector<T>> all(std::vector<Promise<T>> promises) {
    return detail::allImpl<T>(std::move(promises));
}
inline Promise<void> all(const std::vector<Promise<void>>& promises) {
    return detail::allVoidImpl(promises);
}

/// `Promise.race([...])`.
template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<T> race(const std::vector<Promise<T>>& promises) {
    return detail::raceImpl<T>(promises);
}
inline Promise<void> race(const std::vector<Promise<void>>& promises) {
    return detail::raceVoidImpl(promises);
}

/// `Promise.allSettled([...])`.
template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<std::vector<Settled<T>>> allSettled(const std::vector<Promise<T>>& promises) {
    return detail::allSettledImpl<T>(promises);
}
inline Promise<std::vector<Settled<void>>> allSettled(
    const std::vector<Promise<void>>& promises) {
    return detail::allSettledVoidImpl(promises);
}

}  // namespace cp3::runtime
