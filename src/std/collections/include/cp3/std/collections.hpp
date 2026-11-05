// =============================================================================
// cp3/std/collections.hpp - `Map` / `Set` / `LRUCache` / `Queue` / `Stack` /
// `PriorityQueue` / `RingBuffer` / `BitSet` / `MultiMap` / `ConcurrentMap`
// (std/collections) backing: the insertion-ordered + FIFO/LIFO + heap +
// ring + flag-word + grouped + mutex-guarded collection types. The latter
// seven were folded in from the former queue, stack, priority-queue,
// ring-buffer, bitset, multimap and concurrent-map families; all live in `cp3::runtime::collections` under
// the single `std/collections` import per ROADMAP ledger A.
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

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/core/bucket_hash.hpp"
#include "cp3/core/runtime.hpp"
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
// like `+0` (a bitwise number hash would split both groups).
//
// INTERNAL BUCKET HASH ONLY (Roadmap E, docs/design/hash-placement.md):
// wyhash with a per-process random seed from libsodium (HashDoS defense).
// Never exposed: no bucket value, seed, or family domain tag reaches cp3 user
// code, and no wyhash/xxhash name exists under std/hash or std/crypto.
// `std::hash` is deliberately NOT used: it is unseeded in libstdc++
// (deterministic for strings/integers across runs), so an attacker can
// precompute collision keys offline. Seeds differ per process, so collisions
// cannot be precomputed. Do not replace this with std::hash.
//
// Domain tags separate key families so string/number/bool bytes cannot be
// cross-predicted into one bucket layout:
//   string = 0x100000001, floating = 0x200000002, integer = 0x300000003,
//   bool = 0x400000004, other/fallback = 0x500000005.
template <class K>
struct KeyHash {
    std::size_t operator()(const K& key) const {
        if constexpr (std::is_floating_point_v<K>) {
            std::uint64_t bits = 0;
            if (key != key) {
                bits = 0x7ff8000000000000ULL;  // canonical NaN (double layout)
            } else if (key == 0) {
                bits = 0;  // canonical +0: -0 hashes like +0 (SameValueZero)
            } else {
                // Canonical width: float keys widen to double bits so
                // float(1.5) and double(1.5) address the same bucket when a
                // map mixes widths through codegen (number is double).
                const double d = static_cast<double>(key);
                static_assert(sizeof(d) == 8, "double must be 64-bit");
                std::memcpy(&bits, &d, sizeof(bits));
            }
            return static_cast<std::size_t>(
                detail::bucketHashBytes(&bits, sizeof(bits), 0x200000002ULL));
        } else if constexpr (std::is_same_v<K, bool>) {
            const unsigned char b = key ? 1u : 0u;
            return static_cast<std::size_t>(
                detail::bucketHashBytes(&b, sizeof(b), 0x400000004ULL));
        } else if constexpr (std::is_integral_v<K>) {
            // Normalize to the uint64 value bits: int -1 and uint max are
            // distinct keys under == and must not share a canonical encoding.
            // sign-extend signed types, zero-extend unsigned types.
            std::uint64_t v = 0;
            if constexpr (std::is_signed_v<K>) {
                v = static_cast<std::uint64_t>(static_cast<std::int64_t>(key));
            } else {
                v = static_cast<std::uint64_t>(key);
            }
            return static_cast<std::size_t>(
                detail::bucketHashBytes(&v, sizeof(v), 0x300000003ULL));
        } else if constexpr (std::is_same_v<K, std::string>) {
            return static_cast<std::size_t>(
                detail::bucketHashString(key));
        } else if constexpr (std::is_same_v<std::decay_t<K>, std::string_view>) {
            return static_cast<std::size_t>(
                detail::bucketHashString(key));
        } else if constexpr (std::is_same_v<K, std::nullptr_t>) {
            constexpr std::uint64_t tag = 0x600000006ULL;
            return static_cast<std::size_t>(
                detail::bucketHashBytes(&tag, sizeof(tag), tag));
        } else {
            // Handles (Ref/Cell/Ref-wrapped objects, arrays) and any other
            // key the fallback can hold: salt std::hash's pointer/identity
            // word with the per-process seed so HashDoS defense still holds
            // without exposing the raw pointer hash. Never leak `h` itself.
            const auto h = std::hash<K>{}(key);
            std::uint64_t mixed = static_cast<std::uint64_t>(h);
            return static_cast<std::size_t>(
                detail::bucketHashBytes(&mixed, sizeof(mixed), 0x500000005ULL));
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
    struct ValueEntry {
        V value;
        std::size_t orderIndex;
    };

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
        const std::size_t orderIndex = order_.size();
        const auto [it, inserted] =
            values_.try_emplace(key, ValueEntry{value, orderIndex});
        if (!inserted) {
            it->second.value = value;
            return;
        }
        try {
            order_.emplace_back(key);
        } catch (...) {
            values_.erase(it);
            throw;
        }
    }

    std::optional<V> get(const K& key) const {
        const auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        return it->second.value;
    }

    bool has(const K& key) const { return values_.find(key) != values_.end(); }

    // The stored value for `key`, or nullptr when the key is absent: a mutable
    // pointer so an update can run in place, one probe instead of a `get`
    // followed by a `set`. The index is node-based, so the pointer stays valid
    // across insertions of other keys.
    V* findRef(const K& key) {
        const auto it = values_.find(key);
        if (it == values_.end()) return nullptr;
        return &it->second.value;
    }

    // The stored value for `key`, inserting `fallback` first when the key is
    // absent. The fused update probes with `findRef` and reaches this only on a
    // miss, so the returned reference is the slot to store in place.
    V& getOrInsert(const K& key, const V& fallback) {
        const std::size_t orderIndex = order_.size();
        const auto [it, inserted] =
            values_.try_emplace(key, ValueEntry{fallback, orderIndex});
        if (!inserted) return it->second.value;
        try {
            order_.emplace_back(key);
            return it->second.value;
        } catch (...) {
            values_.erase(it);
            throw;
        }
    }

    // The cp3 surface calls this `delete`; `remove` avoids the keyword clash.
    bool remove(const K& key) {
        const auto it = values_.find(key);
        if (it == values_.end()) return false;
        order_[it->second.orderIndex].reset();
        values_.erase(it);
        ++tombstones_;
        compactOrderIfNeeded();
        return true;
    }

    void clear() {
        values_.clear();
        order_.clear();
        tombstones_ = 0;
    }

    // `size` is the std count width (`usize` in the language semantics);
    // codegen widens it to a cp3 `number` only in a TypeScript project.
    std::uint64_t size() const { return static_cast<std::uint64_t>(values_.size()); }

    Array<K> keys() const {
        std::vector<K> keys;
        keys.reserve(values_.size());
        for (const auto& key : order_) {
            if (key) keys.push_back(*key);
        }
        return makeArray<K>(std::move(keys));
    }

    Array<V> values() const {
        std::vector<V> values;
        values.reserve(values_.size());
        for (const auto& key : order_) {
            if (key) values.push_back(values_.at(*key).value);
        }
        return makeArray<V>(std::move(values));
    }

    Array<std::pair<K, V>> entries() const {
        std::vector<std::pair<K, V>> entries;
        entries.reserve(values_.size());
        for (const auto& key : order_) {
            if (key) entries.emplace_back(*key, values_.at(*key).value);
        }
        return makeArray<std::pair<K, V>>(std::move(entries));
    }

    // JS order: (value, key) per entry; codegen matches the callback types.
    template <class F>
    void forEach(F fn) const {
        for (const auto& key : order_) {
            if (key) fn(values_.at(*key).value, *key);
        }
    }

private:
    void compactOrderIfNeeded() {
        // Rebuild only after tombstones outnumber live entries, so the linear
        // compaction cost is amortized over the deletions that triggered it.
        if (tombstones_ <= values_.size() || order_.size() <= 64) return;
        std::vector<std::optional<K>> compact;
        compact.reserve(values_.size());
        for (const auto& key : order_) {
            if (!key) continue;
            auto it = values_.find(*key);
            if (it == values_.end()) continue;
            it->second.orderIndex = compact.size();
            compact.emplace_back(*key);
        }
        order_.swap(compact);
        tombstones_ = 0;
    }

    std::vector<std::optional<K>> order_;
    std::unordered_map<K, ValueEntry, KeyHash<K>, KeyEqual<K>> values_;
    std::size_t tombstones_ = 0;
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

// The backing store of `Set<T>`: insertion-ordered values (the `items_`
// vector is the iteration order, D44) plus a seeded hash index (`index_`,
// same bucket hash as Map/LRU) so `add`/`has` are O(1) instead of the old
// O(n) linear scan. Equality is SameValueZero via `sameKey`/`KeyEqual`:
// NaN dedupes with itself, -0 with +0, exactly as JS Set does. Handles
// fall back to the salted-identity branch of KeyHash (identity, JS parity).
template <class T>
class SetData;
template <class T>
using Set = Ref<SetData<T>>;

template <class T>
class SetData {
public:
    SetData() { index_.reserve(64); }

    void add(const T& value) {
        if (has(value)) return;
        const std::size_t idx = items_.size();
        try {
            items_.push_back(value);
        } catch (...) {
            throw;
        }
        index_.emplace(value, idx);
    }

    bool has(const T& value) const {
        return index_.find(value) != index_.end();
    }

    // The cp3 surface calls this `delete`; `remove` avoids the keyword clash.
    bool remove(const T& value) {
        const auto it = index_.find(value);
        if (it == index_.end()) return false;
        const std::size_t idx = it->second;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
        index_.erase(it);
        // O(n) re-index of the shifted tail: only entries after idx moved.
        // Order vector mutation is unavoidable without tombstones; for
        // remove-heavy sets this matches MapData's compaction tradeoff.
        for (std::size_t i = idx; i < items_.size(); ++i) {
            index_[items_[i]] = i;
        }
        return true;
    }

    void clear() {
        items_.clear();
        index_.clear();
    }

    std::uint64_t size() const { return static_cast<std::uint64_t>(items_.size()); }

    Array<T> values() const { return makeArray<T>(items_); }

    // Set algebra (ES2025): each returns a fresh set; `isSubsetOf` reports
    // whether every element appears in the other set. Order follows JS:
    // union keeps receiver order then new elements; intersection/difference
    // keep receiver order.
    Set<T> unionSet(const Set<T>& other) const {
        Set<T> out = makeRef<SetData<T>>();
        for (const auto& item : items_) out->add(item);
        for (const auto& item : other->items_) out->add(item);
        return out;
    }
    Set<T> intersectionSet(const Set<T>& other) const {
        Set<T> out = makeRef<SetData<T>>();
        for (const auto& item : items_) {
            if (other->has(item)) out->add(item);
        }
        return out;
    }
    Set<T> differenceSet(const Set<T>& other) const {
        Set<T> out = makeRef<SetData<T>>();
        for (const auto& item : items_) {
            if (!other->has(item)) out->add(item);
        }
        return out;
    }
    bool isSubsetOf(const Set<T>& other) const {
        for (const auto& item : items_) {
            if (!other->has(item)) return false;
        }
        return true;
    }
    // The constructor-from-iterable form: add every element (deduplicated).
    void addAll(const Array<T>& elements) {
        for (const auto& item : *elements) add(item);
    }

    // JS order: (value) per element; codegen matches the callback type.
    template <class F>
    void forEach(F fn) const {
        for (const auto& item : items_) fn(item);
    }

private:
    std::vector<T> items_;
    std::unordered_map<T, std::size_t, KeyHash<T>, KeyEqual<T>> index_;
};

// -----------------------------------------------------------------------------
// LRUCache
// -----------------------------------------------------------------------------

// The backing store of `LRUCache<K, V>`: a flat hash map from key to entry
// index plus an intrusive doubly-linked list stored as indices inside the
// entries (no pointer chasing, no per-node allocation). Head is the most
// recently used entry, tail the least recently used; `get` and `set` move
// the entry to the head, and inserting beyond capacity evicts the tail.
// Evicted and removed slots go on a free list and are reused by later
// inserts, so `entries_` holds at most `capacity` live slots (plus the slot
// being inserted) rather than one slot per insert ever made: a cache that
// only unlinked its evictions would retain every key and value ever stored.
// `size_` and `capacity_` are the exact `std::uint64_t` count width (the
// same width `size()`/`capacity()` return), never `size_t`, so a capacity
// above the 32-bit `size_t` range is preserved rather than truncated.
template <class K, class V>
class LRUData {
    struct Entry {
        K key;
        V value;
        std::int64_t prev = -1;
        std::int64_t next = -1;
    };

public:
    explicit LRUData(std::uint64_t capacity)
        : capacity_(capacity) {
        index_.reserve(capacity_ > 64 ? 64 : static_cast<std::size_t>(capacity_));
    }

    void set(const K& key, const V& value) {
        const auto it = index_.find(key);
        if (it != index_.end()) {
            const std::size_t idx = it->second;
            entries_[idx].value = value;
            moveToHead(idx);
            return;
        }
        std::size_t idx;
        if (!free_.empty()) {
            idx = free_.back();
            free_.pop_back();
            entries_[idx] = Entry{key, value, -1, head_};
        } else {
            idx = entries_.size();
            entries_.push_back(Entry{key, value, -1, head_});
        }
        index_.emplace(key, idx);
        if (head_ >= 0) entries_[static_cast<std::size_t>(head_)].prev =
                            static_cast<std::int64_t>(idx);
        head_ = static_cast<std::int64_t>(idx);
        if (tail_ < 0) tail_ = head_;
        ++size_;
        while (size_ > capacity_) evictTail();
    }

    std::optional<V> get(const K& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        moveToHead(it->second);
        return entries_[it->second].value;
    }

    bool has(const K& key) const { return index_.count(key) != 0; }

    bool remove(const K& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) return false;
        const std::size_t idx = it->second;
        unlink(idx);
        index_.erase(it);
        free_.push_back(idx);
        --size_;
        return true;
    }

    void clear() {
        entries_.clear();
        index_.clear();
        free_.clear();
        head_ = tail_ = -1;
        size_ = 0;
    }

    std::uint64_t size() const { return size_; }
    std::uint64_t capacity() const { return capacity_; }

    // Keys in most-recently-used-first order.
    Array<K> keys() const {
        std::vector<K> ks;
        ks.reserve(static_cast<std::size_t>(size_));
        for (std::int64_t i = head_; i >= 0;
             i = entries_[static_cast<std::size_t>(i)].next) {
            ks.push_back(entries_[static_cast<std::size_t>(i)].key);
        }
        return makeArray<K>(ks);
    }

private:
    void unlink(std::size_t idx) {
        const std::int64_t p = entries_[idx].prev;
        const std::int64_t n = entries_[idx].next;
        if (p >= 0) entries_[static_cast<std::size_t>(p)].next = n;
        else head_ = n;
        if (n >= 0) entries_[static_cast<std::size_t>(n)].prev = p;
        else tail_ = p;
        entries_[idx].prev = entries_[idx].next = -1;
    }

    void moveToHead(std::size_t idx) {
        if (head_ == static_cast<std::int64_t>(idx)) return;
        unlink(idx);
        entries_[idx].prev = -1;
        entries_[idx].next = head_;
        if (head_ >= 0) entries_[static_cast<std::size_t>(head_)].prev =
                            static_cast<std::int64_t>(idx);
        head_ = static_cast<std::int64_t>(idx);
        if (tail_ < 0) tail_ = head_;
    }

    void evictTail() {
        if (tail_ < 0) return;
        const std::size_t idx = static_cast<std::size_t>(tail_);
        index_.erase(entries_[idx].key);
        unlink(idx);
        free_.push_back(idx);
        --size_;
    }

    std::uint64_t capacity_;
    std::uint64_t size_ = 0;
    std::vector<Entry> entries_;
    std::vector<std::size_t> free_;
    std::unordered_map<K, std::size_t, KeyHash<K>, KeyEqual<K>> index_;
    std::int64_t head_ = -1;
    std::int64_t tail_ = -1;
};

template <class K, class V>
using LRUCache = Ref<LRUData<K, V>>;

template <class K, class V>
LRUCache<K, V> makeLRU(std::uint64_t capacity) {
    return makeRef<LRUData<K, V>>(capacity);
}

template <class T>
Set<T> makeSet() {
    return makeRef<SetData<T>>();
}

// `new Set<T>(elements)`: a set with every array element added (deduplicated).
template <class T>
Set<T> makeSetFromArray(const Array<T>& elements) {
    Set<T> out = makeRef<SetData<T>>();
    out->addAll(elements);
    return out;
}

// `new Map<K, V>(entries)`: a map with every `[key, value]` pair set.
// `entries()` returns `Array<std::pair<K, V>>`; array literals of entry
// tuples lower to the same pair spelling at this ABI boundary.
template <class K, class V>
Map<K, V> makeMapFromEntries(const Array<std::pair<K, V>>& entries) {
    Map<K, V> out = makeMap<K, V>();
    for (const auto& e : *entries) out->set(e.first, e.second);
    return out;
}

// `Map.groupBy(list, keyFn)`: groups the list items by the callback's key
// (insertion-ordered keys, like every other collection here). The grouping
// result is a `Map<K, Array<T>>`, matching the JS static's shape.
template <class K, class T, class F>
Map<K, Array<T>> groupBy(const Array<T>& list, F fn) {
    Map<K, Array<T>> out = makeMap<K, Array<T>>();
    for (std::size_t i = 0; i < list->size(); ++i) {
        const T& item = (*list)[i];
        // JS context (item, index, array): a callback may declare any
        // prefix, so pick the widest invocable call (as callArrayCallback
        // does for array methods); a 1-parameter lambda cannot be called
        // with extra arguments.
        K key;
        if constexpr (std::is_invocable_v<F&, const T&, double,
                                          const Array<T>&>) {
            key = fn(item, static_cast<double>(i), list);
        } else if constexpr (std::is_invocable_v<F&, const T&, double>) {
            key = fn(item, static_cast<double>(i));
        } else if constexpr (std::is_invocable_v<F&, const T&>) {
            key = fn(item);
        } else {
            key = fn();
        }
        Array<T> group = makeArray<T>(std::vector<T>{});
        if (auto existing = out->get(key)) group = *existing;
        group->push_back(item);
        out->set(key, group);
    }
    return out;
}

// -----------------------------------------------------------------------------
// Queue (folded in from the former queue module)
// -----------------------------------------------------------------------------

// The backing store of `Queue<T>`: a circular buffer over optional slots.
// `head_` is the index of the front element; the live window is the `count_`
// slots starting at `head_` (mod capacity). No equality, hashing or ordering
// of `T` is required - a queue only stores and returns its elements in FIFO
// order. FIFO (first in, first out): `enqueue` appends to the back,
// `dequeue` removes and returns the front, `peek` returns the front without
// removing it. `dequeue`/`peek` on an empty queue yield no value
// (`std::nullopt`); they never throw or read out of bounds.
template <class T>
class QueueData {
public:
    void enqueue(const T& value) {
        if (count_ == buffer_.size()) grow();
        buffer_[(head_ + count_) % buffer_.size()] = value;
        ++count_;
    }

    std::optional<T> dequeue() {
        if (count_ == 0) return std::nullopt;
        std::optional<T>& slot = buffer_[head_];
        T value = std::move(*slot);
        slot.reset();
        head_ = (head_ + 1) % buffer_.size();
        --count_;
        if (count_ == 0) head_ = 0;
        return value;
    }

    std::optional<T> peek() const {
        if (count_ == 0) return std::nullopt;
        return buffer_[head_];
    }

    void clear() {
        for (auto& slot : buffer_) slot.reset();
        head_ = 0;
        count_ = 0;
    }

    bool isEmpty() const { return count_ == 0; }

    std::uint64_t size() const { return static_cast<std::uint64_t>(count_); }

private:
    void grow() {
        const std::size_t capacity =
            buffer_.empty() ? 8 : buffer_.size() * 2;
        std::vector<std::optional<T>> grown(capacity);
        for (std::size_t i = 0; i < count_; ++i) {
            grown[i] = std::move(buffer_[(head_ + i) % buffer_.size()]);
        }
        buffer_.swap(grown);
        head_ = 0;
    }

    std::vector<std::optional<T>> buffer_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

template <class T>
using Queue = Ref<QueueData<T>>;

template <class T>
Queue<T> makeQueue() {
    return makeRef<QueueData<T>>();
}

// -----------------------------------------------------------------------------
// Stack (folded in from the former stack module)
// -----------------------------------------------------------------------------

// The backing store of `Stack<T>`: a vector whose back is the top of the
// stack. No equality, hashing or ordering of `T` is required - unlike a
// `Set`, a stack only stores and returns its elements in reverse order.
// LIFO (last in, first out): `push` appends to the top, `pop` removes and
// returns the top, `peek` returns the top without removing it.
// `pop`/`peek` on an empty stack yield no value (`std::nullopt`).
template <class T>
class StackData {
public:
    void push(const T& value) { items_.push_back(value); }

    std::optional<T> pop() {
        if (items_.empty()) return std::nullopt;
        T value = std::move(items_.back());
        items_.pop_back();
        return value;
    }

    std::optional<T> peek() const {
        if (items_.empty()) return std::nullopt;
        return items_.back();
    }

    void clear() { items_.clear(); }

    bool isEmpty() const { return items_.empty(); }

    std::uint64_t size() const { return static_cast<std::uint64_t>(items_.size()); }

private:
    std::vector<T> items_;
};

template <class T>
using Stack = Ref<StackData<T>>;

template <class T>
Stack<T> makeStack() {
    return makeRef<StackData<T>>();
}

// -----------------------------------------------------------------------------
// PriorityQueue (folded in from the former priority-queue module)
// -----------------------------------------------------------------------------

// The backing store of `PriorityQueue<T>`: a binary heap in one contiguous
// vector. Element 0 is the root; the children of i are 2i+1 and 2i+2, the
// parent is (i-1)/2. Ordering: `outranks(a, b)` means `a` pops before `b`.
// With no comparator the default is a MIN-heap on `<`: the smallest element
// pops first. A custom comparator returns true when its first argument
// outranks its second; a max-heap is `(a, b) => a > b`. `pop`/`peek` on an
// empty queue throw a cp3 `Error` (catchable by the usual `catch (e)`), never
// an undefined value; `peek` does not remove the element.
template <class T>
class PriorityQueueData {
public:
    using Comparator = std::function<bool(const T&, const T&)>;

    PriorityQueueData() = default;

    explicit PriorityQueueData(Comparator comparator)
        : comparator_(std::move(comparator)) {}

    void push(const T& value) {
        heap_.push_back(value);
        siftUp(heap_.size() - 1);
    }

    void push(T&& value) {
        heap_.push_back(std::move(value));
        siftUp(heap_.size() - 1);
    }

    T pop() {
        if (heap_.empty()) {
            throw Error("PriorityQueue.pop on an empty PriorityQueue");
        }
        T top = std::move(heap_.front());
        if (heap_.size() > 1) {
            heap_.front() = std::move(heap_.back());
        }
        heap_.pop_back();
        if (!heap_.empty()) siftDown(0);
        return top;
    }

    const T& peek() const {
        if (heap_.empty()) {
            throw Error("PriorityQueue.peek on an empty PriorityQueue");
        }
        return heap_.front();
    }

private:
    bool outranks(const T& a, const T& b) const {
        if (comparator_) return comparator_(a, b);
        return a < b;
    }

    void siftUp(std::size_t i) {
        while (i > 0) {
            const std::size_t parent = (i - 1) / 2;
            if (!outranks(heap_[i], heap_[parent])) break;
            std::swap(heap_[i], heap_[parent]);
            i = parent;
        }
    }

    void siftDown(std::size_t i) {
        const std::size_t n = heap_.size();
        for (;;) {
            const std::size_t left = 2 * i + 1;
            if (left >= n) break;
            const std::size_t right = left + 1;
            std::size_t best = left;
            if (right < n && outranks(heap_[right], heap_[left])) best = right;
            if (!outranks(heap_[best], heap_[i])) break;
            std::swap(heap_[best], heap_[i]);
            i = best;
        }
    }

    std::vector<T> heap_;
    Comparator comparator_;
};

template <class T>
using PriorityQueue = Ref<PriorityQueueData<T>>;

template <class T>
PriorityQueue<T> makePriorityQueue() {
    return makeRef<PriorityQueueData<T>>();
}

template <class T, class F>
PriorityQueue<T> makePriorityQueue(F comparator) {
    return makeRef<PriorityQueueData<T>>(
        typename PriorityQueueData<T>::Comparator(std::move(comparator)));
}

// -----------------------------------------------------------------------------
// RingBuffer (folded in from the former ring-buffer module)
// -----------------------------------------------------------------------------

// The backing store of `RingBuffer<T>`: a fixed-capacity FIFO ring in
// overwrite mode. The same ring as `Queue` (one contiguous vector, head index
// + live count) with the overwrite flag fixed on: `push` never grows or
// fails - on a full ring it overwrites the oldest element (the head advances
// one slot and the count stays at capacity). `pop`/`peek` on an empty ring
// yield no value (`std::nullopt`). Capacity zero is a degenerate ring: it
// holds nothing, `isFull` is true, and `push` is a no-op.
template <class T>
class RingData {
public:
    explicit RingData(std::uint64_t capacity)
        : capacity_(capacity), storage_(static_cast<std::size_t>(capacity)) {}

    void push(const T& value) {
        if (capacity_ == 0) return;
        if (isFull()) {
            storage_[head_] = value;
            head_ = next(head_);
            return;
        }
        storage_[tail()] = value;
        ++size_;
    }

    std::optional<T> pop() {
        if (size_ == 0) return std::nullopt;
        T value = storage_[head_];
        head_ = next(head_);
        --size_;
        return value;
    }

    std::optional<T> peek() const {
        if (size_ == 0) return std::nullopt;
        return storage_[head_];
    }

    void clear() {
        head_ = 0;
        size_ = 0;
        if (capacity_ != 0) storage_.assign(static_cast<std::size_t>(capacity_), T{});
    }

    std::uint64_t size() const { return size_; }
    std::uint64_t capacity() const { return capacity_; }
    bool isFull() const { return size_ == capacity_; }

private:
    std::size_t tail() const {
        return static_cast<std::size_t>((head_ + static_cast<std::size_t>(size_)) %
                                        static_cast<std::size_t>(capacity_));
    }
    std::size_t next(std::size_t index) const {
        return (index + 1) % static_cast<std::size_t>(capacity_);
    }

    std::uint64_t capacity_;
    std::uint64_t size_ = 0;
    std::vector<T> storage_;
    std::size_t head_ = 0;
};

template <class T>
using RingBuffer = Ref<RingData<T>>;

template <class T>
RingBuffer<T> makeRingBuffer(std::uint64_t capacity) {
    return makeRef<RingData<T>>(capacity);
}

// -----------------------------------------------------------------------------
// BitSet (folded in from the former bitset module)
// -----------------------------------------------------------------------------

// The backing store of `BitSet`: one contiguous word vector, 64 flags per
// word. `size_` is the number of addressable bits (the constructor's size,
// grown by `set`); `words_.size()` is always `ceil(size_ / 64)`, so a word
// lookup can never go out of bounds. `set(i)` grows the set when `i` is at
// or beyond the current size. `get(i)` returns false at or beyond the size.
// `clear(i)` beyond the size is a no-op. `count()` is a word-parallel
// popcount. `andSet`/`orSet`/`xorSet` return a fresh set sized to the larger
// operand; neither operand is modified.
class BitSetData;
using BitSet = ::cp3::runtime::Ref<BitSetData>;
class BitSetData {
public:
    explicit BitSetData(std::uint64_t size = 0)
        : size_(size), words_(wordsFor(size), 0) {}

    void set(std::uint64_t i) {
        growTo(i + 1);
        words_[wordOf(i)] |= bitOf(i);
    }

    bool get(std::uint64_t i) const {
        if (i >= size_) return false;
        return (words_[wordOf(i)] & bitOf(i)) != 0;
    }

    void clear(std::uint64_t i) {
        if (i >= size_) return;
        words_[wordOf(i)] &= ~bitOf(i);
    }

    std::uint64_t count() const {
        std::uint64_t total = 0;
        for (std::uint64_t word : words_) total += popcount(word);
        return total;
    }

    std::uint64_t size() const { return size_; }

    BitSet andSet(const BitSetData& other) const {
        return makeRef<BitSetData>(combine(other, [](std::uint64_t a, std::uint64_t b) {
            return a & b;
        }));
    }
    BitSet orSet(const BitSetData& other) const {
        return makeRef<BitSetData>(combine(other, [](std::uint64_t a, std::uint64_t b) {
            return a | b;
        }));
    }
    BitSet xorSet(const BitSetData& other) const {
        return makeRef<BitSetData>(combine(other, [](std::uint64_t a, std::uint64_t b) {
            return a ^ b;
        }));
    }
    BitSet andSet(const BitSet& other) const { return andSet(*other); }
    BitSet orSet(const BitSet& other) const { return orSet(*other); }
    BitSet xorSet(const BitSet& other) const { return xorSet(*other); }

private:
    static constexpr std::uint64_t kBitsPerWord = 64;

    static std::size_t wordsFor(std::uint64_t size) {
        return static_cast<std::size_t>((size + kBitsPerWord - 1) /
                                        kBitsPerWord);
    }

    static std::size_t wordOf(std::uint64_t i) {
        return static_cast<std::size_t>(i / kBitsPerWord);
    }

    static std::uint64_t bitOf(std::uint64_t i) {
        return std::uint64_t{1} << (i % kBitsPerWord);
    }

    static std::uint64_t popcount(std::uint64_t word) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<std::uint64_t>(__builtin_popcountll(word));
#else
        return static_cast<std::uint64_t>(std::popcount(word));
#endif
    }

    void growTo(std::uint64_t size) {
        if (size <= size_) return;
        size_ = size;
        words_.resize(wordsFor(size_), 0);
    }

    template <class Op>
    BitSetData combine(const BitSetData& other, Op op) const {
        const std::uint64_t size = std::max(size_, other.size_);
        BitSetData out(size);
        const std::size_t span = wordsFor(size);
        for (std::size_t w = 0; w < span; ++w) {
            const std::uint64_t a = w < words_.size() ? words_[w] : 0;
            const std::uint64_t b =
                w < other.words_.size() ? other.words_[w] : 0;
            out.words_[w] = op(a, b);
        }
        return out;
    }

    std::uint64_t size_;
    std::vector<std::uint64_t> words_;
};

using BitSet = Ref<BitSetData>;

inline BitSet makeBitSet(std::uint64_t size = 0) {
    return makeRef<BitSetData>(size);
}

// Codegen lowers `b.and(other)` / `b.or(other)` / `b.xor(other)` onto these
// member spellings (C++ alternative tokens cannot name a member, exactly
// like `delete` -> `remove` in Map/Set above). The expression-level return
// is a `BitSet` handle; the value-level ops above return `BitSetData` by
// value, and the `->` on the handle unwraps to the same call. Codegen emits
// the value-level spelling through the handle (see emitCollectionsMemberCall).

// -----------------------------------------------------------------------------
// MultiMap (folded in from the former multimap module)
// -----------------------------------------------------------------------------

// One value lives inline (no allocation, ever); further values live in a
// single spilling `std::vector<V>`. `Inline` is always 1 here: the ledger
// names SmallVector<V,1>, and the common multi-value key is still just one
// allocation, not one per pair. Handled as first + rest so any copyable V
// works without aligned-storage lifetime tricks.
template <class V, std::size_t Inline = 1>
class SmallVector {
    static_assert(Inline == 1, "this module instantiates SmallVector<V, 1>");

    std::optional<V> first_;
    std::vector<V> rest_;

public:
    SmallVector() = default;

    bool empty() const { return !first_.has_value(); }

    std::size_t size() const {
        return (first_.has_value() ? 1 : 0) + rest_.size();
    }

    void clear() {
        first_.reset();
        rest_.clear();
    }

    void push_back(const V& value) {
        if (!first_.has_value()) {
            first_ = value;
            return;
        }
        if (rest_.empty()) rest_.push_back(*first_);
        rest_.push_back(value);
    }

    void push_back(V&& value) {
        if (!first_.has_value()) {
            first_ = std::move(value);
            return;
        }
        if (rest_.empty()) rest_.push_back(*first_);
        rest_.push_back(std::move(value));
    }

    std::vector<V> toVector() const {
        std::vector<V> out;
        out.reserve(size());
        if (first_.has_value()) {
            out.push_back(*first_);
            for (const V& v : rest_) out.push_back(v);
        }
        return out;
    }
};

// The backing store of `MultiMap<K, V>`: open-addressed flat table
// (power-of-two, linear probing, tombstones) that stores each key's values
// inline in the slot. No `std::unordered_map` nodes and no per-pair
// allocation: the table owns one SmallVector per live key. `add(k, v)`
// appends `v` to `key`'s list; `get(k)` returns a defensive `Array<V>` copy
// (empty if absent) - it never returns `undefined`; `size` counts distinct
// live keys, not pairs. Keys use the same SameValueZero equality and seeded
// hash as `Map` (reusing the file-level `sameKey`/`KeyHash` above instead of
// a local `std::hash` copy); key order across different keys is NOT
// insertion-ordered, but value order within one key is add order.
template <class K, class V>
class MultiMapData {
    struct Entry {
        std::optional<K> key;
        SmallVector<V, 1> values;
        bool occupied = false;
        bool tombstone = false;
    };

public:
    MultiMapData() : buckets_(kMinCapacity) {}

    void add(const K& key, const V& value) {
        const std::size_t i = findOrInsertSlot(key);
        buckets_[i].values.push_back(value);
    }

    Array<V> get(const K& key) const {
        const std::size_t i = findSlot(key);
        if (i == kNpos) return makeArray<V>(std::vector<V>{});
        return makeArray<V>(buckets_[i].values.toVector());
    }

    bool has(const K& key) const { return findSlot(key) != kNpos; }

    bool remove(const K& key) {
        const std::size_t i = findSlot(key);
        if (i == kNpos) return false;
        Entry& e = buckets_[i];
        e.key.reset();
        e.values.clear();
        e.occupied = false;
        e.tombstone = true;
        --live_;
        ++tombstones_;
        return true;
    }

    void clear() {
        for (Entry& e : buckets_) {
            e.key.reset();
            e.values.clear();
            e.occupied = false;
            e.tombstone = false;
        }
        buckets_.assign(kMinCapacity, Entry{});
        live_ = 0;
        tombstones_ = 0;
    }

    std::uint64_t size() const { return live_; }

    std::uint64_t pairCount() const {
        std::uint64_t total = 0;
        for (const Entry& e : buckets_) {
            if (e.occupied) total += e.values.size();
        }
        return total;
    }

    template <class F>
    void forEach(F fn) const {
        for (const Entry& e : buckets_) {
            if (!e.occupied) continue;
            for (const V& v : e.values.toVector()) fn(v, *e.key);
        }
    }

private:
    static constexpr std::size_t kMinCapacity = 16;
    static constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

    void rehash(std::size_t cap) {
        std::vector<Entry> fresh(cap);
        for (const Entry& e : buckets_) {
            if (!e.occupied) continue;
            const std::size_t h = KeyHash<K>{}(*e.key);
            std::size_t i = h & (cap - 1);
            while (fresh[i].occupied) i = (i + 1) & (cap - 1);
            fresh[i].key = e.key;
            fresh[i].values = e.values;
            fresh[i].occupied = true;
        }
        buckets_ = std::move(fresh);
        tombstones_ = 0;
    }

    void growForInsertStep() {
        if ((live_ + tombstones_ + 1) * 10 > buckets_.size() * 7) {
            const std::size_t target =
                (tombstones_ > live_) ? buckets_.size() : buckets_.size() * 2;
            rehash(target);
        }
    }

    std::size_t findSlot(const K& key) const {
        const std::size_t mask = buckets_.size() - 1;
        std::size_t i = KeyHash<K>{}(key) & mask;
        for (std::size_t probed = 0; probed <= mask; ++probed) {
            const Entry& e = buckets_[i];
            if (e.occupied) {
                if (sameKey(*e.key, key)) return i;
            } else if (!e.tombstone) {
                return kNpos;
            }
            i = (i + 1) & mask;
        }
        return kNpos;
    }

    std::size_t findOrInsertSlot(const K& key) {
        growForInsertStep();
        const std::size_t mask = buckets_.size() - 1;
        std::size_t i = KeyHash<K>{}(key) & mask;
        std::size_t firstTomb = kNpos;
        for (std::size_t probed = 0; probed <= mask; ++probed) {
            Entry& e = buckets_[i];
            if (e.occupied) {
                if (sameKey(*e.key, key)) return i;
            } else if (e.tombstone) {
                if (firstTomb == kNpos) firstTomb = i;
            } else {
                const std::size_t target = (firstTomb != kNpos) ? firstTomb : i;
                Entry& slot = buckets_[target];
                slot.key = key;
                slot.values.clear();
                slot.occupied = true;
                slot.tombstone = false;
                if (target == firstTomb) --tombstones_;
                ++live_;
                return target;
            }
            i = (i + 1) & mask;
        }
        rehash(buckets_.size() * 2);
        return findOrInsertSlot(key);
    }

    std::vector<Entry> buckets_;
    std::uint64_t live_ = 0;
    std::size_t tombstones_ = 0;
};

template <class K, class V>
using MultiMap = Ref<MultiMapData<K, V>>;

template <class K, class V>
MultiMap<K, V> makeMultiMap() {
    return makeRef<MultiMapData<K, V>>();
}

// -----------------------------------------------------------------------------
// ConcurrentMap (folded in from the former concurrent-map module)
// -----------------------------------------------------------------------------

// The backing store of `ConcurrentMap<K, V>`: a plain hash map (the same
// file-level SameValueZero `KeyHash`/`KeyEqual` as `Map`) behind one mutex.
// Every public member locks exactly once and performs its whole lookup/update
// while holding it, so the check-then-act sequence `has` + `get` is two
// separate atomic steps (a key may be removed between them) - callers that
// need an atomic pair must synchronize externally. `get` copies the value
// while holding the lock so the returned copy never aliases mutable map
// state. There is no atomic get-or-insert, no snapshot iteration, and no
// cross-thread ordering guarantee; iteration order is bucket order and
// unspecified (unlike the insertion-ordered `Map`).
template <class K, class V>
class ConcurrentMapData {
public:
    void set(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[key] = value;
    }

    std::optional<V> get(const K& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        return it->second;
    }

    bool has(const K& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.find(key) != values_.end();
    }

    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.erase(key) != 0;
    }

    std::uint64_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<std::uint64_t>(values_.size());
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<K, V, KeyHash<K>, KeyEqual<K>> values_;
};

template <class K, class V>
using ConcurrentMap = Ref<ConcurrentMapData<K, V>>;

template <class K, class V>
ConcurrentMap<K, V> makeConcurrentMap() {
    return makeRef<ConcurrentMapData<K, V>>();
}

}  // namespace cp3::runtime::collections
