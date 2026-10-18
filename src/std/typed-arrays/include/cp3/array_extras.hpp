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
#include <optional>
#include <string>
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

}  // namespace cp3::runtime
