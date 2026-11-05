// =============================================================================
// src/std/collections/sema.cpp - `std/collections` `Map`/`Set` typing,
// plus the folded-in `Queue`/`Stack`/`PriorityQueue`/`RingBuffer`/`BitSet`/
// `MultiMap`/`ConcurrentMap` (from the former queue/stack/priority-queue/
// ring-buffer/bitset/multimap/concurrent-map modules). ROADMAP ledger A: all
// live in `std/collections`.
//
// OWNERSHIP: the std/collections family (src/std/collections/**). `Map<K, V>`
// and `Set<T>` are nominal `TypeKind::Class` types carrying their type
// arguments (parts == { K, V } / { T }), the same shape as `Promise<T>` (spec
// §14); the constructor grammar is `new Map<string, number>()`.
//
// The member tables below ARE the docs/04 surface: they drive member typing
// and the CP3305 candidate list at once (`reportMemberMiss`, wired from
// member_check.cpp's `memberOfProbe` Class branch), so the two cannot drift
// apart. `entries()` carries the canonical `$mapEntry` tuple label so codegen
// recognizes the entries array whose element has no C++ spelling.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/std_integer.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The canonical label of a `Map` entry tuple `[K, V]` (the element of
// `entries()`). The tuple has no C++ spelling of its own; codegen recognizes
// the label to lower `.length` on the runtime entries array.
constexpr const char* kMapEntryLabel = "$mapEntry";

Type mapEntryType(const Type& key, const Type& value) {
    Type t(TypeKind::Object, kMapEntryLabel);
    t.parts.push_back(key);
    t.parts.push_back(value);
    return t;
}

// The docs/04 surface, in table order; the names drive the CP3305 candidates.
const char* const kMapMemberNames[] = {"set",     "get",     "has",     "delete",
                                       "clear",   "keys",    "values",  "entries",
                                       "groupBy", "forEach", "size"};
const char* const kSetMemberNames[] = {"add",   "has",   "delete",  "clear",
                                       "values", "forEach", "size",
                                       "union", "intersection", "difference",
                                       "isSubsetOf"};
const char* const kLruMemberNames[] = {"set",     "get",    "has",    "delete",
                                       "clear",   "size",   "capacity", "keys"};

// The documented members of a `Map` (docs/standard-library/04, Map / Set).
const DocRow kMapDocRows[] = {
    {"set", "set(key: K, value: V): void",
     "Stores `value` under `key`, replacing an earlier value."},
    {"get", "get(key: K): V | undefined",
     "Returns the value stored under `key`, or `undefined` when the key is "
     "not in the map."},
    {"has", "has(key: K): boolean",
     "True when `key` is in the map."},
    {"delete", "delete(key: K): boolean",
     "Removes `key` from the map and returns whether a key was actually "
     "removed."},
    {"clear", "clear(): void", "Removes every entry from the map."},
    {"keys", "keys(): K[]",
     "Returns the keys of the map, in insertion order."},
    {"values", "values(): V[]",
     "Returns the values of the map, in insertion order."},
    {"entries", "entries(): [K, V][]",
     "Returns the `[key, value]` pairs of the map, in insertion order."},
    {"groupBy", "groupBy(list: T[], callback: (item: T, index: number) => K): Map<K, T[]>",
     "The static `Map.groupBy`: groups the items by the callback's key."},
    {"forEach", "forEach(callback: (value: V, key: K) => void): void",
     "Calls `callback` for every entry, with the value first and the key "
     "second, in insertion order."},
    {"size", "size: number", "The number of entries in the map."},
};

// The documented members of a `Set` (docs/standard-library/04, Map / Set).
const DocRow kSetDocRows[] = {
    {"add", "add(value: T): void",
     "Adds `value` to the set. A value that is already in the set keeps its "
     "first position."},
    {"has", "has(value: T): boolean", "True when `value` is in the set."},
    {"delete", "delete(value: T): boolean",
     "Removes `value` from the set and returns whether a value was actually "
     "removed."},
    {"clear", "clear(): void", "Removes every value from the set."},
    {"values", "values(): T[]",
     "Returns the values of the set, in insertion order."},
    {"union", "union(other: Set<T>): Set<T>",
     "Returns a fresh set with every value of both sets."},
    {"intersection", "intersection(other: Set<T>): Set<T>",
     "Returns a fresh set with the values present in both sets."},
    {"difference", "difference(other: Set<T>): Set<T>",
     "Returns a fresh set with the values not present in `other`."},
    {"isSubsetOf", "isSubsetOf(other: Set<T>): boolean",
     "True when every value of this set is in `other`."},
    {"forEach", "forEach(callback: (value: T) => void): void",
     "Calls `callback` for every value, in insertion order."},
    {"size", "size: number", "The number of values in the set."},
};

// The documented members of an `LRUCache` (capacity-bounded, evicting).
const DocRow kLruDocRows[] = {
    {"set", "set(key: K, value: V): void",
     "Stores `value` under `key` as most recently used, evicting the least "
     "recently used entry when over capacity."},
    {"get", "get(key: K): V | undefined",
     "Returns the value stored under `key` and marks it most recently used, "
     "or `undefined` when the key is not in the cache."},
    {"has", "has(key: K): boolean",
     "True when `key` is in the cache (does not affect eviction order)."},
    {"delete", "delete(key: K): boolean",
     "Removes `key` from the cache and returns whether it was present."},
    {"clear", "clear(): void", "Removes every entry from the cache."},
    {"size", "size: number", "The number of entries in the cache."},
    {"capacity", "capacity: number", "The maximum number of entries."},
    {"keys", "keys(): K[]",
     "Returns the keys of the cache, most recently used first."},
};

// The documented members of a `Queue` (folded in from the former queue module).
const DocRow kQueueDocRows[] = {
    {"enqueue", "enqueue(value: T): void",
     "Appends `value` to the back of the queue."},
    {"dequeue", "dequeue(): T | undefined",
     "Removes and returns the front value of the queue, or `undefined` when "
     "the queue is empty."},
    {"peek", "peek(): T | undefined",
     "Returns the front value of the queue without removing it, or "
     "`undefined` when the queue is empty."},
    {"clear", "clear(): void", "Removes every value from the queue."},
    {"isEmpty", "isEmpty(): boolean", "True when the queue holds no values."},
    {"size", "size: number", "The number of values in the queue."},
};

// The documented members of a `Stack` (folded in from the former stack module).
const DocRow kStackDocRows[] = {
    {"push", "push(value: T): void",
     "Pushes `value` onto the top of the stack."},
    {"pop", "pop(): T | undefined",
     "Removes and returns the top value of the stack, or `undefined` when "
     "the stack is empty."},
    {"peek", "peek(): T | undefined",
     "Returns the top value of the stack without removing it, or "
     "`undefined` when the stack is empty."},
    {"clear", "clear(): void", "Removes every value from the stack."},
    {"isEmpty", "isEmpty(): boolean", "True when the stack holds no values."},
    {"size", "size: number", "The number of values in the stack."},
};

// The documented members of a `PriorityQueue` (folded in from the former
// priority-queue module; ROADMAP ledger A). Default (no comparator) = min-heap on
// `<`; a custom comparator is `(a: T, b: T) => boolean`, true when `a` has
// higher priority than `b`.
const DocRow kPriorityQueueDocRows[] = {
    {"push", "push(value: T): void",
     "Inserts `value` into the queue, keeping the highest-priority element "
     "at the front."},
    {"pop", "pop(): T",
     "Removes and returns the highest-priority element. Throws an `Error` "
     "when the queue is empty. Among elements neither of which outranks the "
     "other, the pop order is unspecified (a heap, not a stable sort)."},
    {"peek", "peek(): T",
     "Returns the highest-priority element without removing it. Throws an "
     "`Error` when the queue is empty."},
};

// The documented members of a `RingBuffer` (folded in from the former
// ring-buffer module; fixed capacity, overwrite mode). `size`/`capacity`/
// reads, mirroring `Map`/`Set` `.size`.
const DocRow kRingBufferDocRows[] = {
    {"push", "push(value: T): void",
     "Appends `value` as the newest element. When the ring is full the oldest "
     "element is overwritten; the ring never grows."},
    {"pop", "pop(): T | undefined",
     "Removes and returns the oldest element, or `undefined` when the ring is "
     "empty."},
    {"peek", "peek(): T | undefined",
     "Returns the oldest element without removing it, or `undefined` when the "
     "ring is empty."},
    {"clear", "clear(): void", "Removes every element from the ring."},
    {"size", "size: number", "The number of elements in the ring."},
    {"capacity", "capacity: number",
     "The fixed maximum number of elements. Set by the constructor."},
    {"isFull", "isFull: boolean",
     "True when the ring holds `capacity` elements and the next `push` will "
     "overwrite the oldest one."},
};

// The documented members of a `BitSet` (folded in from the former bitset
// module; 64 flags per word with word-parallel bulk operations).
const DocRow kBitSetDocRows[] = {
    {"set", "set(index: number): void",
     "Sets the flag at `index`, growing the set when needed."},
    {"get", "get(index: number): boolean",
     "True when the flag at `index` is set; false beyond the size."},
    {"clear", "clear(index: number): void",
     "Clears the flag at `index`; beyond the size it is a no-op."},
    {"count", "count(): number",
     "The number of set flags (a word-parallel popcount)."},
    {"and", "and(other: BitSet): BitSet",
     "A fresh set with the flags set in both sets; neither operand changes."},
    {"or", "or(other: BitSet): BitSet",
     "A fresh set with the flags set in either set; neither operand changes."},
    {"xor", "xor(other: BitSet): BitSet",
     "A fresh set with the flags set in exactly one set; neither operand "
     "changes."},
};

// The documented members of a `MultiMap` (folded in from the former multimap module).
// `get` returns an array (empty when absent), never `undefined`; `size` is
// a distinct-key count.
const DocRow kMultiMapDocRows[] = {
    {"add", "add(key: K, value: V): void",
     "Appends `value` to the list stored under `key`."},
    {"get", "get(key: K): V[]",
     "Returns all values stored under `key`, in add order, or an empty array "
     "when the key is not in the map."},
    {"has", "has(key: K): boolean", "True when `key` is in the map."},
    {"delete", "delete(key: K): boolean",
     "Removes `key` and all its values, returning whether a key was actually "
     "removed."},
    {"clear", "clear(): void", "Removes every entry from the map."},
    {"size", "size: number", "The number of distinct keys in the map."},
};

// The documented members of a `ConcurrentMap` (folded in from the former
// concurrent-map module). Every member is atomic with respect to the others on
// the same instance (mutex-guarded); there is no cross-thread ordering
// guarantee.
const DocRow kConcurrentMapDocRows[] = {
    {"set", "set(key: K, value: V): void",
     "Stores `value` under `key`, replacing an earlier value. Thread-safe."},
    {"get", "get(key: K): V | undefined",
     "Returns a copy of the value stored under `key`, or `undefined` when "
     "the key is not in the map. Thread-safe."},
    {"has", "has(key: K): boolean",
     "True when `key` is in the map. Thread-safe (not atomic with a "
     "following `get`)."},
    {"delete", "delete(key: K): boolean",
     "Removes `key` from the map and returns whether a key was actually "
     "removed. Thread-safe."},
    {"size", "size: number",
     "The number of entries in the map. Thread-safe (point-in-time)."},
};

Type bitSetType() { return Type(TypeKind::Class, "BitSet"); }

template <std::size_t N>
std::vector<std::string> candidateNames(const char* const (&names)[N]) {
    std::vector<std::string> out;
    out.reserve(N);
    for (const char* name : names) out.emplace_back(name);
    return out;
}

}  // namespace

// =============================================================================
// The family model (shared with scopes.h and member_check.cpp)
// =============================================================================
bool isCollectionsClass(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == "Map" || t.name == "Set" || t.name == "LRUCache" ||
            t.name == "Queue" || t.name == "Stack" ||
            t.name == "PriorityQueue" || t.name == "RingBuffer" ||
            t.name == "BitSet" || t.name == "MultiMap" ||
            t.name == "ConcurrentMap");
}

// =============================================================================
// `new Map<K, V>()` / `new Set<T>()`
// =============================================================================
Type Analyzer::inferCollectionsConstruct(const std::string& name,
                                         TypeList& typeArgs) {
    // The one-part family: `Set<T>`, `Queue<T>`, `Stack<T>`,
    // `PriorityQueue<T>`, `RingBuffer<T>`. The two-part family: `Map<K, V>`,
    // `MultiMap<K, V>`, `ConcurrentMap<K, V>`, `LRUCache<K, V>`. `BitSet`
    // carries no type arguments (parts == {}). The optional constructor
    // arguments (LRUCache capacity, PriorityQueue comparator, RingBuffer
    // capacity, BitSet size) are value arguments, never type arguments, so
    // they cannot ride in parts. Missing annotations stay unknown; codegen
    // reports CP3401 when the constructor has no complete type argument
    // list to lower.
    const bool isOnePart = name == "Set" || name == "Queue" ||
                           name == "Stack" || name == "PriorityQueue" ||
                           name == "RingBuffer";
    const bool isTwoPart = name == "Map" || name == "MultiMap" ||
                           name == "ConcurrentMap" || name == "LRUCache";
    if (name == "BitSet") {
        return bitSetType();
    }
    if (!isOnePart && !isTwoPart) return Type::unknown();
    // Map/LRUCache/MultiMap/ConcurrentMap carry { K, V }; Set/Queue/Stack/
    // PriorityQueue/RingBuffer carry { T }.
    const std::size_t want = isOnePart ? 1 : 2;
    std::vector<Type> parts;
    parts.reserve(want);
    for (std::size_t i = 0; i < want; ++i) {
        if (i < typeArgs.size() && typeArgs[i]) {
            parts.push_back(resolveType(*typeArgs[i]));
        } else {
            parts.push_back(Type::unknown());
        }
    }
    Type t(TypeKind::Class, name);
    t.parts = std::move(parts);
    return t;
}

// =============================================================================
// The member surface
// =============================================================================
Type Analyzer::collectionsMember(const Type& recv, const std::string& prop,
                                 const SourceSpan& span) {
    if (recv.name == "LRUCache") {
        const Type first =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        const Type second =
            recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
        // The count/capacity surface is the std count width: `usize` in a
        // Rust project (`size_t` in a C++ one; number in TypeScript).
        if (prop == "size" || prop == "capacity") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "set") {
            return Type::function({first, second}, Type::voidType());
        }
        if (prop == "get") {
            return Type::function(
                {first}, Type::unionOf({second, Type::undefined()}));
        }
        if (prop == "has" || prop == "delete") {
            return Type::function({first}, Type::boolean());
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        if (prop == "keys") return Type::function({}, Type::array(first));
        reportMemberMiss("LRUCache", prop, span,
                         candidateNames(kLruMemberNames));
        return Type::unknown();
    }
    if (recv.name == "Queue") {
        const Type elem =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        if (prop == "size") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "enqueue") {
            return Type::function({elem}, Type::voidType());
        }
        if (prop == "dequeue" || prop == "peek") {
            return Type::function({}, Type::unionOf({elem, Type::undefined()}));
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        if (prop == "isEmpty") return Type::function({}, Type::boolean());
        reportMemberMiss("Queue", prop, span, docRowNames(kQueueDocRows));
        return Type::unknown();
    }
    if (recv.name == "Stack") {
        const Type elem =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        if (prop == "size") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "push") {
            return Type::function({elem}, Type::voidType());
        }
        if (prop == "pop" || prop == "peek") {
            return Type::function({}, Type::unionOf({elem, Type::undefined()}));
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        if (prop == "isEmpty") return Type::function({}, Type::boolean());
        reportMemberMiss("Stack", prop, span, docRowNames(kStackDocRows));
        return Type::unknown();
    }
    if (recv.name == "PriorityQueue") {
        const Type elem =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        if (prop == "push") return Type::function({elem}, Type::voidType());
        if (prop == "pop" || prop == "peek") {
            return Type::function({}, elem);
        }
        reportMemberMiss("PriorityQueue", prop, span,
                         docRowNames(kPriorityQueueDocRows));
        return Type::unknown();
    }
    if (recv.name == "RingBuffer") {
        const Type elem =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        if (prop == "size" || prop == "capacity") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "isFull") return Type::boolean();
        if (prop == "push") return Type::function({elem}, Type::voidType());
        if (prop == "pop" || prop == "peek") {
            return Type::function({}, Type::unionOf({elem, Type::undefined()}));
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        reportMemberMiss("RingBuffer", prop, span,
                         docRowNames(kRingBufferDocRows));
        return Type::unknown();
    }
    if (recv.name == "BitSet") {
        // The index/count surface is the std count width: `usize` in a Rust
        // project (`size_t` in a C++ one; number in TypeScript).
        const Type index =
            stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        if (prop == "set" || prop == "clear") {
            return Type::function({index}, Type::voidType());
        }
        if (prop == "get") return Type::function({index}, Type::boolean());
        if (prop == "count") return Type::function({}, index);
        if (prop == "and" || prop == "or" || prop == "xor") {
            return Type::function({bitSetType()}, bitSetType());
        }
        reportMemberMiss("BitSet", prop, span, docRowNames(kBitSetDocRows));
        return Type::unknown();
    }
    if (recv.name == "MultiMap") {
        const Type key =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        const Type value =
            recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
        if (prop == "size") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "add") {
            return Type::function({key, value}, Type::voidType());
        }
        if (prop == "get") return Type::function({key}, Type::array(value));
        if (prop == "has" || prop == "delete") {
            return Type::function({key}, Type::boolean());
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        reportMemberMiss("MultiMap", prop, span,
                         docRowNames(kMultiMapDocRows));
        return Type::unknown();
    }
    if (recv.name == "ConcurrentMap") {
        const Type first =
            recv.parts.empty() ? Type::unknown() : recv.parts.front();
        const Type second =
            recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
        if (prop == "size") {
            return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
        }
        if (prop == "set") {
            return Type::function({first, second}, Type::voidType());
        }
        if (prop == "get") {
            return Type::function(
                {first}, Type::unionOf({second, Type::undefined()}));
        }
        if (prop == "has" || prop == "delete") {
            return Type::function({first}, Type::boolean());
        }
        reportMemberMiss("ConcurrentMap", prop, span,
                         docRowNames(kConcurrentMapDocRows));
        return Type::unknown();
    }
    const bool isMap = recv.name == "Map";
    const Type first =
        recv.parts.empty() ? Type::unknown() : recv.parts.front();
    const Type second =
        recv.parts.size() >= 2 ? recv.parts[1] : Type::unknown();
    if (prop == "size") {
        return stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
    }
    if (isMap) {
        if (prop == "set") {
            return Type::function({first, second}, Type::voidType());
        }
        if (prop == "get") {
            // A missing key yields `undefined` (docs/04).
            return Type::function({first},
                                  Type::unionOf({second, Type::undefined()}));
        }
        if (prop == "has" || prop == "delete") {
            return Type::function({first}, Type::boolean());
        }
        if (prop == "keys") return Type::function({}, Type::array(first));
        if (prop == "values") return Type::function({}, Type::array(second));
        if (prop == "entries") {
            return Type::function({}, Type::array(mapEntryType(first, second)));
        }
        if (prop == "forEach") {
            // JS callback order: (value, key).
            const Type cb = Type::function({second, first}, Type::voidType());
            return Type::function({cb}, Type::voidType());
        }
        if (prop == "clear") return Type::function({}, Type::voidType());
        reportMemberMiss("Map", prop, span, candidateNames(kMapMemberNames));
        return Type::unknown();
    }
    if (prop == "add") return Type::function({first}, Type::voidType());
    if (prop == "has" || prop == "delete") {
        return Type::function({first}, Type::boolean());
    }
    if (prop == "values") return Type::function({}, Type::array(first));
    if (prop == "union" || prop == "intersection" || prop == "difference") {
        return Type::function({recv}, recv);
    }
    if (prop == "isSubsetOf") {
        return Type::function({recv}, Type::boolean());
    }
    if (prop == "forEach") {
        const Type cb = Type::function({first}, Type::voidType());
        return Type::function({cb}, Type::voidType());
    }
    if (prop == "clear") return Type::function({}, Type::voidType());
    reportMemberMiss("Set", prop, span, candidateNames(kSetMemberNames));
    return Type::unknown();
}

StdMemberDoc collectionsMemberDoc(const std::string& family,
                                   const std::string& prop) {
    if (family == "Map") return memberDoc(kMapDocRows, prop);
    if (family == "Set") return memberDoc(kSetDocRows, prop);
    if (family == "LRUCache") return memberDoc(kLruDocRows, prop);
    if (family == "Queue") return memberDoc(kQueueDocRows, prop);
    if (family == "Stack") return memberDoc(kStackDocRows, prop);
    if (family == "PriorityQueue") {
        return memberDoc(kPriorityQueueDocRows, prop);
    }
    if (family == "RingBuffer") return memberDoc(kRingBufferDocRows, prop);
    if (family == "BitSet") return memberDoc(kBitSetDocRows, prop);
    if (family == "MultiMap") return memberDoc(kMultiMapDocRows, prop);
    if (family == "ConcurrentMap") {
        return memberDoc(kConcurrentMapDocRows, prop);
    }
    return StdMemberDoc{};
}

std::vector<std::string> collectionsMemberNames(const std::string& family) {
    if (family == "Map") return docRowNames(kMapDocRows);
    if (family == "Set") return docRowNames(kSetDocRows);
    if (family == "LRUCache") return docRowNames(kLruDocRows);
    if (family == "Queue") return docRowNames(kQueueDocRows);
    if (family == "Stack") return docRowNames(kStackDocRows);
    if (family == "PriorityQueue") return docRowNames(kPriorityQueueDocRows);
    if (family == "RingBuffer") return docRowNames(kRingBufferDocRows);
    if (family == "BitSet") return docRowNames(kBitSetDocRows);
    if (family == "MultiMap") return docRowNames(kMultiMapDocRows);
    if (family == "ConcurrentMap") return docRowNames(kConcurrentMapDocRows);
    return {};
}

// `Map.groupBy(list, callback)`: static; the result `Map<K, T[]>` follows
// the callback's return type, and the callback gets the item/index/array
// context exactly like the array callback methods (the signature is
// published so codegen lowers the arrow with matching parameter types).
bool Analyzer::tryInferCollectionsStaticCall(CallExpr& call, MemberExpr& m,
                                             Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Map" ||
        m.property != "groupBy") {
        return false;
    }
    Binding mapBinding;
    if (lookup("Map", mapBinding) && !mapBinding.isAmbient) {
        return false;  // a user `Map` shadows the std family
    }
    if (call.arguments.size() != 2) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: Map.groupBy expects exactly 2 arguments (the "
               "list and the key callback), got " +
                   std::to_string(call.arguments.size()));
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    Type items = call.arguments.front() ? inferExpr(*call.arguments.front())
                                        : Type::unknown();
    if (items.kind != TypeKind::Array) {
        report(diagcode::kTypeMismatch, call.arguments.front()->span,
               "type mismatch: Map.groupBy's first argument must be an array");
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    const Type elem = elemOf(items);
    Type key = Type::unknown();
    Expr* cb = call.arguments[1] ? call.arguments[1].get() : nullptr;
    if (cb != nullptr && cb->kind == NodeKind::ArrowFunction) {
        auto& arrow = static_cast<ArrowFunction&>(*cb);
        const std::vector<Type> context = {elem, Type::number(),
                                           Type::array(elem)};
        key = widen(inferArrowReturn(arrow, context));
        // Publish the inferred signature for codegen's parameter types.
        if (arrow.nodeId != 0) {
            FuncSig as;
            as.ret = key;
            as.declNodeId = arrow.nodeId;
            for (std::size_t i = 0; i < arrow.params.size(); ++i) {
                const Parameter* p = arrow.params[i].get();
                as.params.push_back(i < context.size() ? context[i]
                                                       : Type::unknown());
                as.optional.push_back(p != nullptr &&
                                      (p->optional || p->defaultValue != nullptr));
            }
            publishFunction(as, "");
            arrowSigs_[arrow.nodeId] = std::move(as);
        }
    } else if (cb != nullptr) {
        const Type fnType = inferExpr(*cb);
        if (fnType.kind == TypeKind::Function && !fnType.parts.empty()) {
            key = widen(fnType.parts.back());
        }
    }
    // Do NOT call inferArgs(call) here: it re-infers the callback arrow via
    // inferArrow with ambient (untyped) parameters, overwriting the signature
    // published above (out_.functions / arrowSigs_) with
    // `(ambient) => ambient` and re-checking the body (e.g. `w[0]` /
    // `w.charAt(0)`) against the wrong parameter type. The first argument
    // was already inferred above and a non-arrow callback was inferred in
    // the branch above, so only the arrow's own node type remains to set.
    // (The array callback methods in src/std/arrays/sema_methods.cpp follow
    // the same pattern: inferArrowReturn + publish, then return.)
    if (cb != nullptr && cb->kind == NodeKind::ArrowFunction) {
        const std::vector<Type> context = {elem, Type::number(),
                                           Type::array(elem)};
        withType(*cb, Type::function(context, key));
    }
    if (key.isVoid()) {
        report(diagcode::kTypeMismatch,
               call.arguments[1] ? call.arguments[1]->span : call.span,
               "type mismatch: Map.groupBy callback must return a key, "
               "got 'void'");
        out = withType(call, Type::unknown());
        return true;
    }
    Type result(TypeKind::Class, "Map");
    result.parts = {key, Type::array(elem)};
    out = withType(call, result);
    return true;
}

}  // namespace cp3
