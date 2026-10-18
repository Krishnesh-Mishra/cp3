// =============================================================================
// cp3/std/collections.hpp - `Map` / `Set` (std/collections) backing: the
// insertion-ordered collection types.
//
// OWNERSHIP: the std/collections family (src/std/collections/**). Additive to
// the generated-code ABI (D14): all types are header-only and mirror the
// STDLIB docs §04 Map/Set signature tables one-to-one.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `new Map<K, V>()`      -> `::cp3::runtime::collections::makeMap<K, V>()`
//   `new Set<T>()`         -> `::cp3::runtime::collections::makeSet<T>()`
//   `m.set(k, v)` / `m.get(k)` / `m.has(k)` / `m.delete(k)`
//                          -> the MapData member calls (delete -> `remove`)
//   `s.add(v)` / `s.has(v)` / `s.delete(v)`
//                          -> the SetData member calls (delete -> `remove`)
//   `m.size` / `s.size`    -> `->size()`, a plain element count
//   `m.keys()` / `m.values()` / `m.entries()` / `s.values()`
//                          -> `Array` results (a Map entry is a
//                             `std::pair<K, V>`)
//   `m.forEach(cb)` / `s.forEach(cb)`
//                          -> `->forEach(lambda)` with the callback's
//                             parameters in (value, key) / (value) order
//
// -----------------------------------------------------------------------------
// SEMANTICS (pinned by tests/cases/stdlib/map_* and set_*)
// -----------------------------------------------------------------------------
//   * Insertion-ordered (DECISIONS D44): iteration visits the entries in the
//     order the keys were first set, `Set` likewise for values; deleting a key
//     removes just that entry. The rows are a flat vector, so the order is the
//     storage order - no hashing anywhere.
//   * `get` on a missing key yields no value (`V | undefined` in sema terms);
//     `delete` returns whether the key was present.
//   * Map keys use `==` equality (numbers and strings are the M1 key types),
//     and a repeated `set` updates the value in place without moving the
//     entry.
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "cp3/core/value.hpp"

namespace cp3::runtime::collections {

// -----------------------------------------------------------------------------
// Map
// -----------------------------------------------------------------------------

// The backing store of `Map<K, V>`: one insertion-ordered entry vector. Small
// collections are the norm in M1, so every lookup is a linear scan; that also
// keeps the observable order exactly the insertion order.
template <class K, class V>
class MapData {
public:
    void set(const K& key, const V& value) {
        for (auto& entry : entries_) {
            if (entry.first == key) {
                entry.second = value;
                return;
            }
        }
        entries_.emplace_back(key, value);
    }

    std::optional<V> get(const K& key) const {
        for (const auto& entry : entries_) {
            if (entry.first == key) return entry.second;
        }
        return std::nullopt;
    }

    bool has(const K& key) const {
        for (const auto& entry : entries_) {
            if (entry.first == key) return true;
        }
        return false;
    }

    // The cp3 surface calls this `delete`; `remove` avoids the keyword clash.
    bool remove(const K& key) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->first == key) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    void clear() { entries_.clear(); }

    // `size` is a property; codegen widens the count to a cp3 number.
    double size() const { return static_cast<double>(entries_.size()); }

    Array<K> keys() const {
        std::vector<K> keys;
        keys.reserve(entries_.size());
        for (const auto& entry : entries_) keys.push_back(entry.first);
        return makeArray<K>(std::move(keys));
    }

    Array<V> values() const {
        std::vector<V> values;
        values.reserve(entries_.size());
        for (const auto& entry : entries_) values.push_back(entry.second);
        return makeArray<V>(std::move(values));
    }

    Array<std::pair<K, V>> entries() const {
        return makeArray<std::pair<K, V>>(entries_);
    }

    // JS order: (value, key) per entry; codegen matches the callback types.
    template <class F>
    void forEach(F fn) const {
        for (const auto& entry : entries_) fn(entry.second, entry.first);
    }

private:
    std::vector<std::pair<K, V>> entries_;
};

template <class K, class V>
using Map = Ref<MapData<K, V>>;

template <class K, class V>
Map<K, V> makeMap() {
    return makeRef<MapData<K, V>>();
}

// -----------------------------------------------------------------------------
// Set
// -----------------------------------------------------------------------------

// The backing store of `Set<T>`: insertion-ordered values, deduplicated with
// `==` on insertion.
template <class T>
class SetData {
public:
    void add(const T& value) {
        if (!has(value)) items_.push_back(value);
    }

    bool has(const T& value) const {
        for (const auto& item : items_) {
            if (item == value) return true;
        }
        return false;
    }

    // The cp3 surface calls this `delete`; `remove` avoids the keyword clash.
    bool remove(const T& value) {
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (*it == value) {
                items_.erase(it);
                return true;
            }
        }
        return false;
    }

    void clear() { items_.clear(); }

    double size() const { return static_cast<double>(items_.size()); }

    Array<T> values() const { return makeArray<T>(items_); }

    // JS order: (value) per element; codegen matches the callback type.
    template <class F>
    void forEach(F fn) const {
        for (const auto& item : items_) fn(item);
    }

private:
    std::vector<T> items_;
};

template <class T>
using Set = Ref<SetData<T>>;

template <class T>
Set<T> makeSet() {
    return makeRef<SetData<T>>();
}

}  // namespace cp3::runtime::collections
