// =============================================================================
// cp3/std/object.hpp - `Object.fromEntries` / `Object.groupBy` runtime.
//
// OWNERSHIP: the std/object family (src/std/object/**). Header-only (D14).
// `Record<string, V>` canonicalizes to `Map<string, V>`
// (docs/design/record-index-signatures.md), so both helpers return the
// collections Map handle directly:
//
//   Object.fromEntries(entries) -> fromEntries<V>(entries)
//       A fresh Map<string, V> with every [key, value] entry set in order;
//       a later duplicate key overwrites the earlier value in place
//       (Map.set semantics, insertion order of first set kept).
//   Object.groupBy(list, keyFn) -> groupBy<T>(list, keyFn)
//       A fresh Map<string, Array<T>> grouping the items by the callback's
//       string key (insertion-ordered keys, original order within a group).
//       The callback contract is (item, index, array), matching sema's
//       Object.groupBy context; a callback may declare any prefix.
//
// The generated code currently inlines these two bodies in
// src/std/object/codegen.cpp (via the collections runtime + the existing
// `needsCollections_` include flag) so no new include flag is required; this
// header is the canonical, testable spelling of the same logic. Wiring it in
// (see the module note in object/codegen.cpp) is a one-include change.
// =============================================================================
#pragma once

#include <string>
#include <tuple>
#include <utility>

#include "cp3/core/runtime.hpp"
#include "cp3/std/collections.hpp"

namespace cp3::runtime::object {

// `Object.fromEntries(entries)`: every entry is a pair or a tuple
// (std::get reads both), as with `new Map<K, V>(entries)`.
template <class V, class E>
collections::Map<std::string, V> fromEntries(
    const Array<E>& entries) {
    auto out = collections::makeMap<std::string, V>();
    for (const auto& e : *entries) {
        out->set(std::get<0>(e), std::get<1>(e));
    }
    return out;
}

// `Object.groupBy(list, keyFn)`: the string-key specialization of
// collections::groupBy; the key callback must return std::string.
template <class T, class F>
collections::Map<std::string, Array<T>> groupBy(const Array<T>& list,
                                                F keyFn) {
    return collections::groupBy<std::string, T>(list, keyFn);
}

}  // namespace cp3::runtime::object
