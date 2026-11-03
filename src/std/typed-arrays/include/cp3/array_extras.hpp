// =============================================================================
// src/std/typed-arrays/include/cp3/array_extras.hpp - the regular-array extras of
// the JS-compat family (D69).
//
// `Array(n)` (`makeArrayDefault`: a dense default-initialized array), `.fill`,
// `.join`, `.sort`, `.includes`, `Array.of` / `Array.from` (array and typed
// sources), the indexed accessors (`arrayAt` / `arrayWriteAt`) and the element
// text `join` uses. Pulled by cp3/typed_array.hpp; a standalone include works
// through the guard.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "cp3/core/value.hpp"

namespace cp3::runtime {

// -----------------------------------------------------------------------------
// Regular arrays: `Array(n)`, `.fill`, `.join`, `Array.of` / `Array.from`
// -----------------------------------------------------------------------------

// `Array(n)`: a dense array, every element default-initialized (no holes).
template <class E>
Array<E> makeArrayDefault(double length) {
    const std::size_t len = toIndex(length, "Invalid array length");
    return makeArray<E>(std::vector<E>(len));
}

// `a.fill(value, start?, end?)`: in place, returns the receiver.
template <class E>
Array<E> arrayFill(const Array<E>& a, const E& value, double start = 0.0,
                   std::optional<double> end = std::nullopt) {
    const std::size_t from = startIndex(start, a->size());
    const std::size_t to = endIndex(end, a->size());
    for (std::size_t i = from; i < to; ++i) (*a)[i] = value;
    return a;
}

template <class E>
std::string arrayJoin(const Array<E>& a, const std::string& sep);

inline std::string arrayElementText(double v) { return toString(v); }
inline std::string arrayElementText(const std::string& v) { return v; }
inline std::string arrayElementText(bool v) { return toString(v); }
// A value-union element (`(number | string)[]` -> `std::variant`): render the
// alternative the union currently holds.
template <class... Ts>
std::string arrayElementText(const std::variant<Ts...>& v) {
    return std::visit([](const auto& x) { return arrayElementText(x); }, v);
}
template <class E>
std::string arrayElementText(const Array<E>& a) {
    return arrayJoin(a, std::string(","));
}
// An object element (a `Ref<T>` handle): the pointee renders itself. The
// SFINAE keeps a handle whose pointee has no renderer out of the overload set
// (an interface wrapper is a by-value struct, not a handle).
template <class T>
inline auto arrayElementText(const ::cp3::runtime::Ref<T>& v)
    -> decltype(v->cp3ToText()) {
    return v->cp3ToText();
}

template <class E>
std::string arrayJoin(const Array<E>& a, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (i != 0) out += sep;
        out += arrayElementText((*a)[i]);
    }
    return out;
}

// `a.sort()` (docs/language-spec/09-arrays-strings.md): in place, stable,
// returns the receiver. The default ordering is type-aware - numeric for
// number arrays, lexicographic for string arrays - so `[10, 9, 2].sort()`
// yields `[2, 9, 10]` where JS's lexicographic default would not.
template <class E>
Array<E> arraySort(const Array<E>& a) {
    std::stable_sort(a->begin(), a->end(),
                     [](const E& x, const E& y) { return x < y; });
    return a;
}

// `a.sort((a, b) => number)`: the JS comparator contract (negative sorts the
// left value first); stable, so a comparator that returns 0 keeps the input
// order.
template <class E, class F>
Array<E> arraySortWith(const Array<E>& a, F cmp) {
    std::stable_sort(a->begin(), a->end(),
                     [&cmp](const E& x, const E& y) { return cmp(x, y) < 0.0; });
    return a;
}

// `a.toSorted()` / `a.toSorted(cmp)`: a sorted copy; the receiver is left
// untouched. The default ordering and comparator contract match `sort`.
template <class E>
Array<E> arrayToSorted(const Array<E>& a) {
    std::vector<E> out(a->begin(), a->end());
    std::stable_sort(out.begin(), out.end(),
                     [](const E& x, const E& y) { return x < y; });
    return makeArray<E>(std::move(out));
}

template <class E, class F>
Array<E> arrayToSortedWith(const Array<E>& a, F cmp) {
    std::vector<E> out(a->begin(), a->end());
    std::stable_sort(out.begin(), out.end(),
                     [&cmp](const E& x, const E& y) { return cmp(x, y) < 0.0; });
    return makeArray<E>(std::move(out));
}

// `a.toReversed()`: a reversed copy; the receiver is left untouched.
template <class E>
Array<E> arrayToReversed(const Array<E>& a) {
    std::vector<E> out(a->begin(), a->end());
    std::reverse(out.begin(), out.end());
    return makeArray<E>(std::move(out));
}

// SameValueZero (JS `includes` / `indexOf` on the double instantiation):
// NaN finds itself and +0/-0 compare equal; the template covers every other
// element type with `==` (bool, string, an enum).
inline bool sameValueZero(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}
template <class E>
bool sameValueZero(const E& a, const E& b) {
    return a == b;
}

template <class I>
inline bool integerIndexInRange(I index, std::size_t length) {
    static_assert(std::is_integral_v<I>);
    if constexpr (std::is_same_v<std::remove_cv_t<I>, bool>) {
        return static_cast<std::size_t>(index) < length;
    } else {
        if constexpr (std::is_signed_v<I>) {
            if (index < 0) return false;
        }
        using Unsigned = std::make_unsigned_t<I>;
        return static_cast<Unsigned>(index) < length;
    }
}

// A bounds-checked read/write (the corpus only indexes in range; anything else
// panics instead of reading out of bounds). The read returns a reference:
// generated uses copy only where cp3 semantics copy (a binding, argument or
// return), so a member read or method call through the element does not copy a
// reference-counted handle.
template <class E>
const E& arrayAt(const Array<E>& a, double index) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E, class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
const E& arrayAt(const Array<E>& a, I index) {
    if (!integerIndexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

// The non-owning form used only for a compiler-proven local vector. Keeping
// the same bounds check preserves array semantics while avoiding a shared_ptr
// handle and control block for the vector object itself.
template <class E>
const E& arrayAt(const std::vector<E>* a, double index) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E, class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
const E& arrayAt(const std::vector<E>* a, I index) {
    if (!integerIndexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

// The element at `index`, or `fallback` when the array is shorter. A binding
// pattern's default (`[a = 1]`) reads the element or the default, so a short
// array fills the missing slot instead of running off its end.
template <class E, class F>
E arrayAtOr(const Array<E>& a, double index, F fallback) {
    if (!indexInRange(index, a->size())) return static_cast<E>(fallback);
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E, class I, class F,
          std::enable_if_t<std::is_integral_v<I>, int> = 0>
E arrayAtOr(const Array<E>& a, I index, F fallback) {
    if (!integerIndexInRange(index, a->size())) return static_cast<E>(fallback);
    return (*a)[static_cast<std::size_t>(index)];
}

// The reference form of `arrayAt` (codegen: support/array_refs.cpp): the same
// bounds check and panic, without copying the element. VALID ONLY WHILE THE
// ARRAY IS NOT MUTATED: any push, pop, element write, sort or other
// size/content change may reallocate the vector and leave this reference
// dangling. Codegen binds it only after a whole-unit proof that the array is
// never mutated, aliased or escaped, and the result binding is never written
// or captured.
template <class E>
const E& arrayAtRef(const Array<E>& a, double index) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E, class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
const E& arrayAtRef(const Array<E>& a, I index) {
    if (!integerIndexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E>
const E& arrayAtRef(const std::vector<E>* a, double index) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E, class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
const E& arrayAtRef(const std::vector<E>* a, I index) {
    if (!integerIndexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

// A bounds-checked read with a default: the element at `index`, or `fallback`
// when the array is shorter. A destructuring pattern default (`[a = 1] = xs`)
// reads its element this way, so a missing element takes the default instead
// of panicking.
template <class E>
E arrayAtOrDefault(const Array<E>& a, double index, E fallback) {
    if (!indexInRange(index, a->size())) return fallback;
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E, class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
E arrayAtOrDefault(const Array<E>& a, I index, E fallback) {
    if (!integerIndexInRange(index, a->size())) return fallback;
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E>
void arrayWriteAt(const Array<E>& a, double index, const E& value) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    (*a)[static_cast<std::size_t>(index)] = value;
}

template <class E, class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
void arrayWriteAt(const Array<E>& a, I index, const E& value) {
    if (!integerIndexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    (*a)[static_cast<std::size_t>(index)] = value;
}

template <class E>
Array<E> arrayFromArray(const Array<E>& src) {
    return makeArray<E>(std::vector<E>(src->begin(), src->end()));
}

template <class Tag>
Array<double> arrayFromTyped(const TypedArray<Tag>& src) {
    std::vector<double> values(src->length);
    for (std::size_t i = 0; i < src->length; ++i) {
        values[i] = Tag::read(src->load(i));
    }
    return makeArray<double>(std::move(values));
}

// -----------------------------------------------------------------------------
// The method surface (docs/language-spec/09-arrays-strings.md): mutation,
// callback iteration, searching and slicing over regular arrays.
// -----------------------------------------------------------------------------

// `a.push(v)`: in place; returns the new length (JS).
template <class E>
double arrayPush(const Array<E>& a, const E& value) {
    a->push_back(value);
    return static_cast<double>(a->size());
}

// A freshly constructed element (`a.push(new C())` / `a.push({...})`) is moved
// into the vector: the temporary would be destroyed right after the call, so
// the handle copy (and its atomic increment) is unobservable.
template <class E>
double arrayPush(const Array<E>& a, E&& value) {
    a->push_back(std::move(value));
    return static_cast<double>(a->size());
}

template <class E>
double arrayPush(std::vector<E>* a, const E& value) {
    a->push_back(value);
    return static_cast<double>(a->size());
}

template <class E>
double arrayPush(std::vector<E>* a, E&& value) {
    a->push_back(std::move(value));
    return static_cast<double>(a->size());
}

// A capacity hint for a counted push loop (codegen: emit/push_reserve.cpp).
// Capacity is not an observable cp3 value, so this can never change a program's
// result: it only reserves room for the pushes the loop is about to perform.
// The reservation is capped at 256 MiB of element storage so an unrelated huge
// loop bound cannot commit an unreasonable amount, and a failed reservation is
// ignored because ordinary push growth is always correct.
template <class E>
void arrayReservePush(const Array<E>& a, double count) {
    if (!(count > 0.0) || !std::isfinite(count)) return;
    constexpr double kCapBytes = 268435456.0;  // 256 MiB
    const double cap = kCapBytes / static_cast<double>(sizeof(E));
    const std::size_t current = a->size();
    if (static_cast<double>(current) >= cap) return;
    if (count > cap - static_cast<double>(current)) return;
    const std::size_t add = static_cast<std::size_t>(count);
    if (add == 0) return;
    const std::size_t total = current + add;
    if (total <= a->capacity()) return;
    try {
        a->reserve(total);
    } catch (...) {
        // The hint is optional: pushes still grow the vector normally.
    }
}

template <class E>
void arrayReservePush(std::vector<E>* a, double count) {
    if (!(count > 0.0) || !std::isfinite(count)) return;
    constexpr double kCapBytes = 268435456.0;
    const double cap = kCapBytes / static_cast<double>(sizeof(E));
    const std::size_t current = a->size();
    if (static_cast<double>(current) >= cap) return;
    if (count > cap - static_cast<double>(current)) return;
    const std::size_t add = static_cast<std::size_t>(count);
    if (add == 0) return;
    const std::size_t total = current + add;
    if (total <= a->capacity()) return;
    try {
        a->reserve(total);
    } catch (...) {
    }
}

// `a.pop()`: removes and returns the last element; an empty array yields
// `undefined` (an empty optional), matching JS.
template <class E>
std::optional<E> arrayPop(const Array<E>& a) {
    if (a->empty()) return std::nullopt;
    E value = a->back();
    a->pop_back();
    return value;
}

template <class E>
std::optional<E> arrayPop(std::vector<E>* a) {
    if (a->empty()) return std::nullopt;
    E value = a->back();
    a->pop_back();
    return value;
}

// `a.shift()`: removes and returns the first element; empty -> `undefined`.
template <class E>
std::optional<E> arrayShift(const Array<E>& a) {
    if (a->empty()) return std::nullopt;
    E value = a->front();
    a->erase(a->begin());
    return value;
}

// `a.unshift(v)`: inserts at the front; returns the new length.
template <class E>
double arrayUnshift(const Array<E>& a, const E& value) {
    a->insert(a->begin(), value);
    return static_cast<double>(a->size());
}

// A JS-style array callback accepts the element, its index and the receiver.
// cp3 lets the callback declare any prefix of that shape, so the adapter picks
// the widest call the lambda is invocable with. The `if constexpr` branches
// are discarded at instantiation time, which keeps one-parameter callbacks
// compiling exactly as before.
template <class E, class F>
decltype(auto) callArrayCallback(F& fn, const E& value, std::size_t index,
                                 const Array<E>& array) {
    if constexpr (std::is_invocable_v<F&, const E&, double, const Array<E>&>) {
        return fn(value, static_cast<double>(index), array);
    } else if constexpr (std::is_invocable_v<F&, const E&, double>) {
        return fn(value, static_cast<double>(index));
    } else if constexpr (std::is_invocable_v<F&, const E&>) {
        return fn(value);
    } else {
        return fn();
    }
}

// A reduce callback accepts `(accumulator, value, index, array)`; the same
// prefix rule applies.
template <class E, class F, class R>
decltype(auto) callArrayReduceCallback(F& fn, R& accumulator, const E& value,
                                       std::size_t index,
                                       const Array<E>& array) {
    if constexpr (std::is_invocable_v<F&, R&, const E&, double,
                                      const Array<E>&>) {
        return fn(accumulator, value, static_cast<double>(index), array);
    } else if constexpr (std::is_invocable_v<F&, R&, const E&, double>) {
        return fn(accumulator, value, static_cast<double>(index));
    } else {
        return fn(accumulator, value);
    }
}

// `a.map(fn)`: a fresh array of the callback results.
template <class R, class E, class F>
Array<R> arrayMap(const Array<E>& a, F fn) {
    std::vector<R> out;
    out.reserve(a->size());
    for (std::size_t i = 0; i < a->size(); ++i) {
        out.push_back(callArrayCallback<E>(fn, (*a)[i], i, a));
    }
    return makeArray<R>(std::move(out));
}

// `a.flatMap(fn)`: map, then flatten one level.
template <class R, class E, class F>
Array<R> arrayFlatMap(const Array<E>& a, F fn) {
    std::vector<R> out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        Array<R> mapped = callArrayCallback<E>(fn, (*a)[i], i, a);
        out.insert(out.end(), mapped->begin(), mapped->end());
    }
    return makeArray<R>(std::move(out));
}

// `a.forEach(fn)`: the callback is called for its effects; the result is
// discarded, matching JS.
template <class E, class F>
void arrayForEach(const Array<E>& a, F fn) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        (void)callArrayCallback<E>(fn, (*a)[i], i, a);
    }
}

// `a.filter(pred)`: a fresh array of the elements the predicate accepts.
template <class E, class F>
Array<E> arrayFilter(const Array<E>& a, F pred) {
    std::vector<E> out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (callArrayCallback<E>(pred, (*a)[i], i, a)) out.push_back((*a)[i]);
    }
    return makeArray<E>(std::move(out));
}

// `a.reduce(fn, initial)`: left fold over the initial value.
template <class R, class E, class F>
R arrayReduce(const Array<E>& a, F fn, const R& initial) {
    R acc = initial;
    for (std::size_t i = 0; i < a->size(); ++i) {
        acc = callArrayReduceCallback<E>(fn, acc, (*a)[i], i, a);
    }
    return acc;
}

// A fused eager map/filter/reduce pipeline. The compiler selects this only
// when each callback is proven pure, so interleaving the callbacks preserves
// observable behavior while avoiding both full-sized intermediate arrays.
template <class R, class E, class M, class P, class F>
R arrayMapFilterReduce(const Array<E>& a, M map, P pred, F reduce,
                       const R& initial) {
    R acc = initial;
    for (std::size_t i = 0; i < a->size(); ++i) {
        auto mapped = map((*a)[i]);
        if (pred(mapped)) acc = reduce(acc, mapped);
    }
    return acc;
}

// `a.reduce(fn)` with no initial value (JS): the accumulator starts as the
// first element and an empty array is an error (JS throws; cp3 panics like the
// other runtime preconditions). The docs leave the no-initial form open
// (control-flow/_ambiguous/reduce_no_initial), so this follows JS.
template <class E, class F>
E arrayReduceNoInit(const Array<E>& a, F fn) {
    if (a->empty()) typedArrayPanic("reduce of empty array with no initial value");
    E acc = a->front();
    for (std::size_t i = 1; i < a->size(); ++i) {
        acc = callArrayReduceCallback<E>(fn, acc, (*a)[i], i, a);
    }
    return acc;
}

// `a.find(pred)`: the first matching element, or `undefined`.
template <class E, class F>
std::optional<E> arrayFind(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (callArrayCallback<E>(pred, (*a)[i], i, a)) return (*a)[i];
    }
    return std::nullopt;
}

// `a.findLast(pred)`: the last matching element, or `undefined`.
template <class E, class F>
std::optional<E> arrayFindLast(const Array<E>& a, F pred) {
    for (std::size_t i = a->size(); i-- > 0;) {
        if (callArrayCallback<E>(pred, (*a)[i], i, a)) return (*a)[i];
    }
    return std::nullopt;
}

// `a.findIndex(pred)`: the first matching index, or -1.
template <class E, class F>
double arrayFindIndex(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (callArrayCallback<E>(pred, (*a)[i], i, a)) {
            return static_cast<double>(i);
        }
    }
    return -1.0;
}

// `a.findLastIndex(pred)`: the last matching index, or -1.
template <class E, class F>
double arrayFindLastIndex(const Array<E>& a, F pred) {
    for (std::size_t i = a->size(); i-- > 0;) {
        if (callArrayCallback<E>(pred, (*a)[i], i, a)) {
            return static_cast<double>(i);
        }
    }
    return -1.0;
}

// `a.some(pred)` / `a.every(pred)`: short-circuiting, so the call count and
// order match JS (the corpus pins both).
template <class E, class F>
bool arraySome(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (callArrayCallback<E>(pred, (*a)[i], i, a)) return true;
    }
    return false;
}

template <class E, class F>
bool arrayEvery(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (!callArrayCallback<E>(pred, (*a)[i], i, a)) return false;
    }
    return true;
}

// `a.at(index)`: a relative index accessor; negative indices count from the
// end, out-of-range indices produce `undefined`.
template <class E>
std::optional<E> arrayElementAt(const Array<E>& a, double index) {
    if (std::isnan(index)) index = 0.0;
    double i = std::trunc(index);
    if (i < 0.0) i += static_cast<double>(a->size());
    if (!(i >= 0.0) || i >= static_cast<double>(a->size())) {
        return std::nullopt;
    }
    return (*a)[static_cast<std::size_t>(i)];
}

// `a.indexOf(x, from?)`: the first index equal to x, else -1.
template <class E>
double arrayIndexOf(const Array<E>& a, const E& value, double from = 0.0) {
    const std::size_t start = startIndex(from, a->size());
    for (std::size_t i = start; i < a->size(); ++i) {
        if ((*a)[i] == value) return static_cast<double>(i);
    }
    return -1.0;
}

// `a.includes(x, from?)`: SameValueZero (the double instantiation finds NaN
// and treats +0/-0 as equal, matching JS).
template <class E>
bool arrayIncludes(const Array<E>& a, const E& value, double from = 0.0) {
    const std::size_t start = startIndex(from, a->size());
    for (std::size_t i = start; i < a->size(); ++i) {
        if (sameValueZero((*a)[i], value)) return true;
    }
    return false;
}

// `a.slice(start?, end?)`: a shallow copy; negative indices count from the
// end (the same relative-index rule the typed views and `fill` use).
template <class E>
Array<E> arraySlice(const Array<E>& a, double start = 0.0,
                    std::optional<double> end = std::nullopt) {
    const std::size_t from = startIndex(start, a->size());
    const std::size_t to = endIndex(end, a->size());
    std::vector<E> out;
    if (to > from) {
        out.assign(a->begin() + static_cast<std::ptrdiff_t>(from),
                   a->begin() + static_cast<std::ptrdiff_t>(to));
    }
    return makeArray<E>(std::move(out));
}

// `a.flat()`: one level, a fresh array.
template <class E>
Array<E> arrayFlat(const Array<Array<E>>& a) {
    std::vector<E> out;
    for (const Array<E>& inner : *a) {
        out.insert(out.end(), inner->begin(), inner->end());
    }
    return makeArray<E>(std::move(out));
}

// `a.unique()`: in place; removes adjacent duplicates (`std::unique`).
template <class E>
Array<E> arrayUnique(const Array<E>& a) {
    a->erase(std::unique(a->begin(), a->end()), a->end());
    return a;
}

// `a.shuffle()`: in place Fisher-Yates with a seeded engine.
template <class E>
Array<E> arrayShuffle(const Array<E>& a) {
    static thread_local std::mt19937 engine(std::random_device{}());
    std::shuffle(a->begin(), a->end(), engine);
    return a;
}

// `a.binarySearch(target)`: `std::lower_bound` over a sorted array (the first
// equal slot), or -1 when the target is absent. A debug build checks the
// sorted precondition with a panic (docs 09: unsorted is a footgun, not an
// error, so release skips the check).
template <class E>
double arrayBinarySearch(const Array<E>& a, const E& target) {
#ifndef NDEBUG
    if (!std::is_sorted(a->begin(), a->end())) {
        typedArrayPanic("binarySearch on an unsorted array");
    }
#endif
    const auto it = std::lower_bound(a->begin(), a->end(), target);
    if (it == a->end() || !(*it == target)) return -1.0;
    return static_cast<double>(it - a->begin());
}

// A value-union slot (`(number | string)[]` -> `std::variant`): `===` only
// matches when the union currently holds the same alternative (exact
// type+value, TS-style).
template <class... Ts, class V>
bool unionStrictEquals(const std::variant<Ts...>& value, const V& other) {
    if (const V* held = std::get_if<V>(&value)) return *held == other;
    return false;
}

// JS ToNumber over the primitive alternatives of a value union (spec 05: the
// loose `==` coerces, the strict `===` does not). Booleans are 0/1; a string
// parses whole-text after JS whitespace trimming, empty is 0 and anything
// malformed is NaN so the comparison is false.
inline double looseToNumber(double v) { return v; }
inline double looseToNumber(bool v) { return v ? 1.0 : 0.0; }
inline double looseToNumber(const std::string& text) {
    const char* first = text.c_str();
    while (*first == ' ' || *first == '\t' || *first == '\n' ||
           *first == '\r' || *first == '\f' || *first == '\v') {
        ++first;
    }
    if (*first == '\0') return 0.0;
    char* last = nullptr;
    const double value = std::strtod(first, &last);
    while (*last == ' ' || *last == '\t' || *last == '\n' || *last == '\r' ||
           *last == '\f' || *last == '\v') {
        ++last;
    }
    if (*last != '\0') return std::numeric_limits<double>::quiet_NaN();
    return value;
}

// The loose form (`==`): a different alternative compares by JS abstract
// equality, so `1 == "1"` and `1 == true` hold for a union-held value.
template <class... Ts, class V>
bool unionLooseEquals(const std::variant<Ts...>& value, const V& other) {
    if (const V* held = std::get_if<V>(&value)) return *held == other;
    return std::visit(
        [&](const auto& held) {
            return looseToNumber(held) == looseToNumber(other);
        },
        value);
}

}  // namespace cp3::runtime
