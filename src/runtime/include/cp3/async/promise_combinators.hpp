// =============================================================================
// cp3/async/promise_combinators.hpp - `Promise.all` / `.race` / `.allSettled`.
//
// OWNERSHIP: runtime module. Included by cp3/async/promise.hpp; generated code emits
// `::cp3::runtime::all|race|allSettled(...)` over a `std::vector<Promise<T>>`.
//
//   * `all`        - resolves with the values in input order; rejects as soon as
//                    any promise rejects. The result is a cp3 array
//                    (`Array<T>`), so the awaited value has the array surface
//                    (`.length`, `[i]`, `.join(",")`).
//   * `race`       - settles as soon as any promise settles (first wins).
//   * `allSettled` - always resolves, with each result tagged ok/error
//                    (`Settled<T>`), again as a cp3 array.
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

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<Array<T>> allImpl(const std::vector<Promise<T>>& promises) {
    Promise<Array<T>> derived = Promise<Array<T>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(makeArray<T>(std::vector<T>{}));
        return derived;
    }
    auto results = std::make_shared<std::vector<T>>(count);
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<T>> st = promises[i].sharedState();
        addFulfilledContinuation<T>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = st->slot.value;
            if (--*remaining == 0) {
                derived.fulfill(makeArray<T>(std::move(*results)));
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
Promise<Array<Settled<T>>> allSettledImpl(const std::vector<Promise<T>>& promises) {
    Promise<Array<Settled<T>>> derived =
        Promise<Array<Settled<T>>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(makeArray<Settled<T>>(std::vector<Settled<T>>{}));
        return derived;
    }
    auto results = std::make_shared<std::vector<Settled<T>>>(count);
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<T>> st = promises[i].sharedState();
        addFulfilledContinuation<T>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<T>::fulfilled(st->slot.value);
            if (--*remaining == 0) {
                derived.fulfill(makeArray<Settled<T>>(std::move(*results)));
            }
        });
        addRejectedContinuation<T>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<T>::rejected(st->reason);
            if (--*remaining == 0) {
                derived.fulfill(makeArray<Settled<T>>(std::move(*results)));
            }
        });
    }
    return derived;
}

inline Promise<Array<Settled<void>>> allSettledVoidImpl(
    const std::vector<Promise<void>>& promises) {
    Promise<Array<Settled<void>>> derived =
        Promise<Array<Settled<void>>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(makeArray<Settled<void>>(std::vector<Settled<void>>{}));
        return derived;
    }
    auto results = std::make_shared<std::vector<Settled<void>>>(count);
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<void>> st = promises[i].sharedState();
        addFulfilledContinuation<void>(st, [results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<void>::fulfilled();
            if (--*remaining == 0) {
                derived.fulfill(makeArray<Settled<void>>(std::move(*results)));
            }
        });
        addRejectedContinuation<void>(st, [st, results, remaining, derived, i]() mutable {
            (*results)[i] = Settled<void>::rejected(st->reason);
            if (--*remaining == 0) {
                derived.fulfill(makeArray<Settled<void>>(std::move(*results)));
            }
        });
    }
    return derived;
}

}  // namespace detail

/// `Promise.all([...])`. The result is a cp3 array of the values, in input
/// order, so the awaited value has the array surface (`.length`, `[i]`,
/// `.join(",")`).
template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<Array<T>> all(const std::vector<Promise<T>>& promises) {
    return detail::allImpl<T>(promises);
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

/// `Promise.allSettled([...])`. The result is a cp3 array of settled records.
template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<Array<Settled<T>>> allSettled(const std::vector<Promise<T>>& promises) {
    return detail::allSettledImpl<T>(promises);
}
inline Promise<Array<Settled<void>>> allSettled(
    const std::vector<Promise<void>>& promises) {
    return detail::allSettledVoidImpl(promises);
}

}  // namespace cp3::runtime
