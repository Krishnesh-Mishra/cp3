// =============================================================================
// src/std/typed-arrays/include/cp3/array_extras.hpp - the regular-array extras of
// the JS-compat family (D69).
//
// `Array(n)` (`makeArrayDefault`: a dense default-initialized array), `.fill`,
// `.join`, `.sort`, `Array.of` / `Array.from` (array and typed sources), the
// indexed accessors (`arrayAt` / `arrayWriteAt`) and the element text `join`
// uses. Pulled by cp3/typed_array.hpp; a standalone include works through the
// guard.
// =============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <random>
#include <string>
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

// A bounds-checked read/write (the corpus only indexes in range; anything else
// panics instead of reading out of bounds).
template <class E>
E arrayAt(const Array<E>& a, double index) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
    return (*a)[static_cast<std::size_t>(index)];
}

template <class E>
void arrayWriteAt(const Array<E>& a, double index, const E& value) {
    if (!indexInRange(index, a->size())) typedArrayPanic("Array index out of range");
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

// `a.pop()`: removes and returns the last element; an empty array yields
// `undefined` (an empty optional), matching JS.
template <class E>
std::optional<E> arrayPop(const Array<E>& a) {
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

// `a.map(fn)`: a fresh array of the callback results.
template <class R, class E, class F>
Array<R> arrayMap(const Array<E>& a, F fn) {
    std::vector<R> out;
    out.reserve(a->size());
    for (std::size_t i = 0; i < a->size(); ++i) out.push_back(fn((*a)[i]));
    return makeArray<R>(std::move(out));
}

// `a.filter(pred)`: a fresh array of the elements the predicate accepts.
template <class E, class F>
Array<E> arrayFilter(const Array<E>& a, F pred) {
    std::vector<E> out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (pred((*a)[i])) out.push_back((*a)[i]);
    }
    return makeArray<E>(std::move(out));
}

// `a.reduce(fn, initial)`: left fold over the initial value.
template <class R, class E, class F>
R arrayReduce(const Array<E>& a, F fn, const R& initial) {
    R acc = initial;
    for (std::size_t i = 0; i < a->size(); ++i) acc = fn(acc, (*a)[i]);
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
    for (std::size_t i = 1; i < a->size(); ++i) acc = fn(acc, (*a)[i]);
    return acc;
}

// `a.find(pred)`: the first matching element, or `undefined`.
template <class E, class F>
std::optional<E> arrayFind(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (pred((*a)[i])) return (*a)[i];
    }
    return std::nullopt;
}

// `a.findIndex(pred)`: the first matching index, or -1.
template <class E, class F>
double arrayFindIndex(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (pred((*a)[i])) return static_cast<double>(i);
    }
    return -1.0;
}

// `a.some(pred)` / `a.every(pred)`: short-circuiting, so the call count and
// order match JS (the corpus pins both).
template <class E, class F>
bool arraySome(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (pred((*a)[i])) return true;
    }
    return false;
}

template <class E, class F>
bool arrayEvery(const Array<E>& a, F pred) {
    for (std::size_t i = 0; i < a->size(); ++i) {
        if (!pred((*a)[i])) return false;
    }
    return true;
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

// `a.includes(x, from?)`.
template <class E>
bool arrayIncludes(const Array<E>& a, const E& value, double from = 0.0) {
    const std::size_t start = startIndex(from, a->size());
    for (std::size_t i = start; i < a->size(); ++i) {
        if ((*a)[i] == value) return true;
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
