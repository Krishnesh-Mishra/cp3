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
    // The derived promise resolves with a cp3 array, so the awaited value has
    // the array surface (`.length`, `[i]`, `.join(",")`).
    Promise<Array<T>> derived;

    AllState(Promise<Array<T>> promise, std::vector<Promise<T>> inputs)
        : elements(std::move(inputs)), results(elements.size()),
          remaining(elements.size()), derived(std::move(promise)) {}
};

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<Array<T>> allImpl(std::vector<Promise<T>> promises) {
    Promise<Array<T>> derived = Promise<Array<T>>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.fulfill(makeArray<T>(std::vector<T>{}));
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
                all->derived.fulfill(makeArray<T>(std::move(results)));
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
Promise<Array<T>> all(std::vector<Promise<T>> promises) {
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

/// `Promise.allSettled([...])`. The result is a cp3 array of settled records.
template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<Array<Settled<T>>> allSettled(const std::vector<Promise<T>>& promises) {
    return detail::allSettledImpl<T>(promises);
}
inline Promise<Array<Settled<void>>> allSettled(
    const std::vector<Promise<void>>& promises) {
    return detail::allSettledVoidImpl(promises);
}

/// `Promise.any([...])`.
///   * resolves as soon as any input fulfils, with that value;
///   * rejects with an `AggregateError` (message "All promises were rejected")
///     when every input rejected; an empty input rejects the same way.
namespace detail {

template <class T>
struct AnyState {
    std::vector<Promise<T>> elements;
    std::vector<Thrown> reasons;
    std::size_t remaining = 0;
    Promise<T> derived;

    AnyState(Promise<T> promise, std::vector<Promise<T>> inputs)
        : elements(std::move(inputs)), reasons(elements.size()),
          remaining(elements.size()), derived(std::move(promise)) {}
};

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<T> anyImpl(std::vector<Promise<T>> promises) {
    Promise<T> derived = Promise<T>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.rejectWith(Thrown::makeError(std::make_shared<AggregateError>()));
        return derived;
    }
    auto all = std::make_shared<AnyState<T>>(derived, std::move(promises));
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<T>> element = all->elements[i].sharedState();
        addRejectedContinuation<T>(element, [all, i]() {
            all->reasons[i] = all->elements[i].sharedState()->reason;
            if (--all->remaining == 0) {
                std::vector<Ref<Error>> errors;
                errors.reserve(all->reasons.size());
                for (const Thrown& reason : all->reasons) {
                    if (const Ref<Error>* boxed =
                            reason.get<Ref<Error>>()) {
                        errors.push_back(*boxed);
                    } else {
                        errors.push_back(
                            std::make_shared<Error>(reason.toString()));
                    }
                }
                auto aggregate = std::make_shared<AggregateError>(
                    std::make_shared<std::vector<Ref<Error>>>(
                        std::move(errors)),
                    std::string("All promises were rejected"));
                if (!all->derived.settled()) {
                    all->derived.rejectWith(
                        Thrown::makeError(std::move(aggregate)));
                }
            }
        });
        addFulfilledContinuation<T>(element, [all, i]() {
            if (!all->derived.settled()) {
                all->derived.fulfill(all->elements[i].sharedState()->slot.value);
            }
        });
    }
    return derived;
}

inline Promise<void> anyVoidImpl(const std::vector<Promise<void>>& promises) {
    Promise<void> derived = Promise<void>::pending();
    const std::size_t count = promises.size();
    if (count == 0) {
        derived.rejectWith(Thrown::makeError(std::make_shared<AggregateError>()));
        return derived;
    }
    // The inputs and the countdown live as long as the continuations need
    // them (mirrors allSettledVoidImpl).
    auto inputs = std::make_shared<std::vector<Promise<void>>>(promises);
    auto remaining = std::make_shared<std::size_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::shared_ptr<PromiseState<void>> element =
            (*inputs)[i].sharedState();
        addFulfilledContinuation<void>(element, [derived]() mutable {
            if (!derived.settled()) derived.fulfill();
        });
        addRejectedContinuation<void>(element, [derived, remaining]() mutable {
            if (--*remaining == 0 && !derived.settled()) {
                derived.rejectWith(
                    Thrown::makeError(std::make_shared<AggregateError>()));
            }
        });
    }
    return derived;
}

}  // namespace detail

/// `Promise.withResolvers()` — the resolver pair exposed alongside the promise.
/// `resolve` adopts nothing: it settles with the value it receives. The cp3
/// source spells the return value as the `$withResolvers` marker (typed via
/// `Promise.withResolvers<T>()`); codegen emits this struct directly.
template <class T>
struct WithResolvers {
    Promise<T> promise = Promise<T>::pending();
    std::function<void(T)> resolve;
    std::function<void(Ref<::cp3::Error>)> reject;
    WithResolvers() {
        Promise<T> p = promise;
        resolve = [p](T value) mutable {
            if (!p.settled()) p.fulfill(std::move(value));
        };
        reject = [p](Ref<::cp3::Error> reason) mutable {
            if (!p.settled()) p.rejectWith(Thrown::makeError(std::move(reason)));
        };
    }
};

template <>
struct WithResolvers<void> {
    Promise<void> promise = Promise<void>::pending();
    std::function<void()> resolve;
    std::function<void(Ref<::cp3::Error>)> reject;
    WithResolvers() {
        Promise<void> p = promise;
        resolve = [p]() mutable {
            if (!p.settled()) p.fulfill();
        };
        reject = [p](Ref<::cp3::Error> reason) mutable {
            if (!p.settled()) p.rejectWith(Thrown::makeError(std::move(reason)));
        };
    }
};

/// `Promise.withResolvers()`.
template <class T>
WithResolvers<T> withResolvers() {
    return WithResolvers<T>{};
}

template <class T, std::enable_if_t<!std::is_void_v<T>, int> = 0>
Promise<T> any(std::vector<Promise<T>> promises) {
    return detail::anyImpl<T>(std::move(promises));
}
inline Promise<void> any(const std::vector<Promise<void>>& promises) {
    return detail::anyVoidImpl(promises);
}

}  // namespace cp3::runtime
