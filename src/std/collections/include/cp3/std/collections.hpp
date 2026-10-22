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
//     removes just that entry. `Map` keeps the keys in an insertion-ordered
//     vector (that vector is the iteration order) and the values in a hash
//     index probed by key, so `set`/`get`/`has` are constant-time and a lookup
//     is one probe (an index that only mapped key -> row would add a dependent
//     load to every update). `remove` erases from both.
//   * `get` on a missing key yields no value (`V | undefined` in sema terms);
//     `delete` returns whether the key was present.
//   * Map keys use SameValueZero equality (numbers and strings are the M1 key
//     types): `==` for everything, with NaN matching itself like JS `Map`. A
//     repeated `set` updates the value in place without moving the entry.
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/core/value.hpp"

namespace cp3::runtime::collections {

// -----------------------------------------------------------------------------
// Map
// -----------------------------------------------------------------------------

// SameValueZero key equality (the JS `Map` rule): `==` for everything, with a
// NaN matching itself. `+0` and `-0` are already equal under `==`.
template <class K>
bool sameKey(const K& a, const K& b) {
    if constexpr (std::is_floating_point_v<K>) {
        if (a != a) return b != b;
        return a == b;
    } else {
        return a == b;
    }
}

// A hash consistent with `sameKey`: every NaN goes to one bucket and `-0` hashes
// like `+0` (a bitwise number hash would split both groups). Keys that are not
// floating point defer to the standard hash.
template <class K>
struct KeyHash {
    std::size_t operator()(const K& key) const {
        if constexpr (std::is_floating_point_v<K>) {
            if (key != key) return static_cast<std::size_t>(0x7ff8000000000000ULL);
            if (key == 0) return std::hash<K>{}(static_cast<K>(0));
            return std::hash<K>{}(key);
        } else {
            return std::hash<K>{}(key);
        }
    }
};

template <class K>
struct KeyEqual {
    bool operator()(const K& a, const K& b) const { return sameKey(a, b); }
};

// The backing store of `Map<K, V>`: an insertion-ordered key vector (the
// iteration order, D44) plus a hash index that owns the values. A lookup is one
// probe; keeping the values behind a key -> row index instead would put a
// dependent load on every update (measured: 25% on word-frequency).
template <class K, class V>
class MapData {
public:
    // A `Map` starts at 541 buckets (libstdc++'s prime just above 512, ~4 KB).
    // On word-frequency (64 keys, 3M updates) the bucket count, not the load
    // factor, dominates the lookup cost: 257 buckets -> 60.4 ms, 541 -> 56.0,
    // 769 -> 55.6, 1031 -> 51.2 (best), versus 57.5-58.4 for the cpp entry.
    // 541 is the smallest count that recovered the whole gap; 1031 buys ~4 ms
    // more for a table nobody asked for. The default growth (13, 29, ...)
    // spends the same probes on collision chains.
    MapData() { values_.rehash(512); }

    void set(const K& key, const V& value) {
        const auto it = values_.find(key);
        if (it != values_.end()) {
            it->second = value;
            return;
        }
        values_.emplace(key, value);
        order_.push_back(key);
    }

    std::optional<V> get(const K& key) const {
        const auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        return it->second;
    }

    bool has(const K& key) const { return values_.find(key) != values_.end(); }

    // The stored value for `key`, or nullptr when the key is absent: a mutable
    // pointer so an update can run in place, one probe instead of a `get`
    // followed by a `set`. The index is node-based, so the pointer stays valid
    // across insertions of other keys.
    V* findRef(const K& key) {
        const auto it = values_.find(key);
        if (it == values_.end()) return nullptr;
        return &it->second;
    }

    // The stored value for `key`, inserting `fallback` first when the key is
    // absent. The fused update probes with `findRef` and reaches this only on a
    // miss, so the returned reference is the slot to store in place.
    V& getOrInsert(const K& key, const V& fallback) {
        const auto [it, inserted] = values_.try_emplace(key, fallback);
        if (inserted) order_.push_back(key);
        return it->second;
    }

    // The cp3 surface calls this `delete`; `remove` avoids the keyword clash.
    bool remove(const K& key) {
        if (values_.erase(key) == 0) return false;
        for (auto it = order_.begin(); it != order_.end(); ++it) {
            if (sameKey(*it, key)) {
                order_.erase(it);
                break;
            }
        }
        return true;
    }

    void clear() {
        values_.clear();
        order_.clear();
    }

    // `size` is a property; codegen widens the count to a cp3 number.
    double size() const { return static_cast<double>(order_.size()); }

    Array<K> keys() const { return makeArray<K>(order_); }

    Array<V> values() const {
        std::vector<V> values;
        values.reserve(order_.size());
        for (const K& key : order_) values.push_back(values_.at(key));
        return makeArray<V>(std::move(values));
    }

    Array<std::pair<K, V>> entries() const {
        std::vector<std::pair<K, V>> entries;
        entries.reserve(order_.size());
        for (const K& key : order_) entries.emplace_back(key, values_.at(key));
        return makeArray<std::pair<K, V>>(std::move(entries));
    }

    // JS order: (value, key) per entry; codegen matches the callback types.
    template <class F>
    void forEach(F fn) const {
        for (const K& key : order_) fn(values_.at(key), key);
    }

private:
    std::vector<K> order_;
    std::unordered_map<K, V, KeyHash<K>, KeyEqual<K>> values_;
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
