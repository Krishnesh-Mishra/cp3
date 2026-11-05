// =============================================================================
// cp3/std/object.hpp - `Object.fromEntries` / `Object.groupBy` runtime, plus
// the `Object.assign` / `.is` / `.freeze`-family canonical runtime.
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
//
// --- Freeze family (ROADMAP §H) --------------------------------------------
//
// CP3 objects are `Ref<T>` handles (`std::shared_ptr<T>`) with a statically
// known shape and no runtime property table, so freeze state cannot live on
// the object itself. The canonical state is therefore a side registry keyed
// by the object's address, held in a function-local static of an inline
// function (one registry per program, shared across translation units):
//
//   freeze(o)           -> marks o (and, deep-freeze, every nested object /
//                          array reached from it) frozen; returns o.
//   seal(o)             -> marks o sealed (non-configurable, non-extensible,
//                          still writable); returns o.
//   preventExtensions(o) -> marks o non-extensible; returns o.
//   isFrozen(o)         -> true iff o is frozen (frozen implies sealed).
//   isSealed(o)         -> true iff o is sealed or frozen.
//   isExtensible(o)     -> false iff o is frozen, sealed, or prevented.
//   getPrototypeOf(o)   -> nullptr (CP3 objects have no prototype).
//   setPrototypeOf(o, p) -> requires p == nullptr; returns o unchanged.
//
// The registry is intentionally a testable canonical spelling, not generated
// code: codegen.cpp cannot include this header today (no include flag feeds
// it into generated units, and the runtime headers are frozen), so generated
// code uses the identity + syntactic-folding lowering documented there.
// Wiring this header into generated units (one include flag in the module
// writer, then `::cp3::runtime::object::freeze(...)` in codegen) upgrades
// freeze/isFrozen to track through variables and across functions with no
// other change. Until then, direct field writes after `freeze` are not
// intercepted by the generated code (assignments are lowered elsewhere).
// =============================================================================
#pragma once

#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

// --- Freeze-family canonical registry ---------------------------------------

namespace detail {

// One registry entry per frozen/sealed/non-extensible object identity.
// `flags`: 1 = non-extensible, 2 = sealed, 4 = frozen (frozen sets all).
struct FreezeEntry {
    const void* identity;
    unsigned flags;
};

// The single process-wide registry. A function-local static in an inline
// function is shared by every translation unit that includes this header.
inline std::vector<FreezeEntry>& freezeRegistry() {
    static std::vector<FreezeEntry> registry;
    return registry;
}

inline FreezeEntry* findEntry(const void* identity) {
    for (auto& entry : freezeRegistry()) {
        if (entry.identity == identity) return &entry;
    }
    return nullptr;
}

inline void mark(const void* identity, unsigned flags) {
    if (identity == nullptr) return;
    if (FreezeEntry* entry = findEntry(identity)) {
        entry->flags |= flags;
    } else {
        freezeRegistry().push_back(FreezeEntry{identity, flags});
    }
}

inline unsigned flagsOf(const void* identity) {
    const FreezeEntry* entry = findEntry(identity);
    return entry == nullptr ? 0u : entry->flags;
}

}  // namespace detail

// Identity of a reference handle: the shared object's address. Null handles
// are the null object (JS: `Object.freeze(null)` throws; the generated
// lowering rejects nullish receivers in sema instead, so this is a guard).
template <class T>
inline const void* identityOf(const Ref<T>& handle) {
    return handle == nullptr ? nullptr
                             : static_cast<const void*>(handle.get());
}

// `Object.freeze(o)`: marks `o` frozen; returns the same handle. This
// canonical spelling marks the object itself; freezing nested objects is
// done by freezing each nested value (a generated deep-freeze walk can call
// `freeze` per nested field, as the syntactic deep query in codegen.cpp
// does for `freeze(o).nested`).
template <class T>
inline Ref<T> freeze(const Ref<T>& handle) {
    if (handle == nullptr) return handle;
    const void* identity = identityOf(handle);
    if (detail::flagsOf(identity) == 7u) return handle;  // already frozen
    detail::mark(identity, 7u);
    return handle;
}

// `Object.seal(o)`: sealed = non-extensible + non-configurable (writable
// properties stay writable, so seal does not imply frozen). Returns `o`.
template <class T>
inline Ref<T> seal(const Ref<T>& handle) {
    if (handle != nullptr) {
        detail::mark(identityOf(handle), 1u | 2u);
    }
    return handle;
}

// `Object.preventExtensions(o)`: no new properties; existing ones stay
// writable and configurable. Returns `o`.
template <class T>
inline Ref<T> preventExtensions(const Ref<T>& handle) {
    if (handle != nullptr) {
        detail::mark(identityOf(handle), 1u);
    }
    return handle;
}

// `Object.isFrozen(o)`: frozen implies sealed implies non-extensible, so a
// frozen object reports true for all three queries.
template <class T>
inline bool isFrozen(const Ref<T>& handle) {
    return handle != nullptr && (detail::flagsOf(identityOf(handle)) & 4u) != 0;
}

template <class T>
inline bool isSealed(const Ref<T>& handle) {
    return handle != nullptr && (detail::flagsOf(identityOf(handle)) & 2u) != 0;
}

template <class T>
inline bool isExtensible(const Ref<T>& handle) {
    return handle == nullptr ||
           (detail::flagsOf(identityOf(handle)) & 1u) == 0;
}

// Primitive receivers (JS: primitives are frozen, sealed, non-extensible).
inline bool isFrozenPrimitive() { return true; }
inline bool isSealedPrimitive() { return true; }
inline bool isExtensiblePrimitive() { return false; }

// `Object.getPrototypeOf(o)`: CP3 objects have no prototype.
template <class T>
inline std::nullptr_t getPrototypeOf(const Ref<T>&) {
    return nullptr;
}

// `Object.setPrototypeOf(o, prototype)`: only `nullptr` is meaningful in
// the static model; anything else is rejected in sema with the ROADMAP §7
// diagnostic. Returns `o` unchanged.
template <class T>
inline Ref<T> setPrototypeOf(const Ref<T>& handle, std::nullptr_t) {
    return handle;
}

// `Object.is(a, b)` for reference handles: identity, matching the generated
// lowering's `((a) == (b))` for non-primitive kinds. The numeric/string/bool
// overloads live in cp3/core/runtime.hpp (`cp3_object_is`).
template <class T>
inline bool is(const Ref<T>& a, const Ref<T>& b) {
    return identityOf(a) == identityOf(b);
}

inline bool is(std::nullptr_t, std::nullptr_t) { return true; }
inline bool is(::cp3::runtime::Undefined, ::cp3::runtime::Undefined) {
    return true;
}

}  // namespace cp3::runtime::object
