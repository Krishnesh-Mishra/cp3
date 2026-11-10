---
title: Collections
chapter: Chapter 3 · Standard library
---
# Collections

`std/collections` is the single module for every collection type. Import all
of them from `"std/collections"`; there are no per-type module specifiers:

```ts
import { Map, Set, LRUCache, Queue, Stack, PriorityQueue, RingBuffer, BitSet, MultiMap, ConcurrentMap } from "std/collections";

const counts = new Map<string, number>();
const seen = new Set<string>();
const queue = new Queue<number>();
const stack = new Stack<number>();
const heap = new PriorityQueue<number>();
const ring = new RingBuffer<number>(16);
const flags = new BitSet();
const grouped = new MultiMap<string, number>();
const shared = new ConcurrentMap<string, number>();
const cache = new LRUCache<string, number>(128);
```

Both preserve insertion order when iterating. They are reference values managed by the runtime; see [Values and references](../02-runtime/01-value-model.md).

## Map

`Map<K, V>` associates keys of type `K` with values of type `V`.

- `set(key, value)` inserts or replaces an entry and returns `void`.
- `get(key)` returns `V | undefined` when the key is absent.
- `has(key)` reports whether a key exists.
- `delete(key)` removes a key and reports whether it was present.
- `clear()` removes all entries.
- `keys()`, `values()`, and `entries()` expose ordered contents.
- `forEach(callback)` visits entries in insertion order.
- `groupBy(list, callback)` (static, called as `Map.groupBy(...)`) groups an array into a `Map<K, T[]>`.
- `size` is the current entry count.

```ts
const grouped = Map.groupBy(["apple", "artichoke", "banana"], (word) => word.charAt(0));
// Map { "a" => ["apple", "artichoke"], "b" => ["banana"] }

const counts = new Map<string, number>();
counts.set("apples", 3);
const apples = counts.get("apples"); // number | undefined
if (counts.has("apples")) console.log(apples);
```

`Map.groupBy(list, callback)` takes an array, not a general iterable. The callback may declare any prefix of `(item, index, array)`; the result is a fresh `Map<K, T[]>` whose keys appear in first-seen order and whose groups preserve input order. `K` follows the callback's return type — it is not restricted to strings (use `Object.groupBy` for the string-key record form).

Because `get` can return `undefined`, check for absence before using a value that must exist. A key already present keeps its original insertion position when its value is replaced.

## Set

`Set<T>` stores unique values:

- `add(value)` inserts the value if it is not already present and returns `void`.
- `has(value)` reports membership.
- `delete(value)` removes a value and reports whether it was present.
- `clear()` removes all values.
- `values()` returns values in insertion order.
- `forEach(callback)` visits values in insertion order.
- `size` is the number of distinct values.

```ts
seen.add("request-42");
if (seen.has("request-42")) console.log("already processed");
```

### Set operations

`Set` also has algebra members. Each of `union`, `intersection`, and `difference` returns a fresh `Set<T>` and leaves both operands unchanged; order follows the receiver — `union` keeps receiver order followed by values new to the other set, and `intersection`/`difference` keep receiver order:

- `union(other)` — every value of both sets.
- `intersection(other)` — the values present in both sets.
- `difference(other)` — the values of this set not present in `other`.
- `isSubsetOf(other)` — true when every value of this set is in `other`.

```ts
const a = new Set<number>();
a.add(1); a.add(2); a.add(3);
const b = new Set<number>();
b.add(2); b.add(3); b.add(4);
a.union(b);        // Set { 1, 2, 3, 4 }
a.intersection(b); // Set { 2, 3 }
a.difference(b);   // Set { 1 }
a.isSubsetOf(b);   // false
```

## Iteration and ordering

The collection's order is insertion order. Deleting an entry and adding it again makes it a new insertion. Do not assume sorted order; sort a separate array of keys or values when ordering by value is needed.

## Key behavior

Map and Set key equality follows the runtime's equality semantics for their supported key values. If a program uses object-valued keys, use stable references and test the intended identity behavior. A Map is not a plain object and does not inherit JavaScript object property semantics.

## Concurrency

Except for `ConcurrentMap`, these collections are ordinary values, not synchronization primitives. Do not mutate one concurrently from worker threads without a supported synchronization protocol. `ConcurrentMap` (below) is mutex-guarded and thread-safe; every other concurrency primitive is a separate language/runtime feature.

## Constructor types are required

Supply generic type arguments when constructing collections so keys, values, and set elements have a checked type:

```ts
const totals = new Map<string, number>();
const visited = new Set<string>();
```

A Map needs two type arguments (`K`, `V`); a Set needs one (`T`). The constructor does not infer a safe key/value pair from an empty collection. The type checker validates later `set`, `get`, and `add` calls against these arguments.

## Method signatures and callback order

| Type | Member | Signature |
| --- | --- | --- |
| Map | `set` | `set(key: K, value: V): void` |
| Map | `get` | `get(key: K): V | undefined` |
| Map | `has`, `delete` | `(key: K): boolean` |
| Map | `clear` | `clear(): void` |
| Map | `keys`, `values`, `entries` | `K[]`, `V[]`, `[K, V][]` |
| Map | `forEach` | `(value: V, key: K) => void` callback, in that argument order |
| Set | `add` | `add(value: T): void` |
| Set | `has`, `delete` | `(value: T): boolean` |
| Set | `clear` | `clear(): void` |
| Set | `values` | `values(): T[]` |
| Set | `forEach` | `(value: T) => void` callback |
| Map | `groupBy` | `groupBy(list: T[], callback: (item: T, index: number) => K): Map<K, T[]>` (static, array input only) |
| Set | `union` | `union(other: Set<T>): Set<T>` (fresh set) |
| Set | `intersection` | `intersection(other: Set<T>): Set<T>` (fresh set) |
| Set | `difference` | `difference(other: Set<T>): Set<T>` (fresh set) |
| Set | `isSubsetOf` | `isSubsetOf(other: Set<T>): boolean` |
| Both | `size` | Read-only count property |

`delete` reports whether an entry/value existed and was removed. `clear` always returns `void`. `get` distinguishes a missing key by returning `undefined`, so use `has` when the distinction between “missing” and a stored value matters.

Numeric collection values follow the project vocabulary. A `typescript` project keeps `number`; a `rust` or `cpp` project uses the exact standard-library count width: `size` (Map/Set/LRUCache) and LRUCache's `capacity` are `usize` (`size_t` in C++), so counts never pass through a floating-point value.

## Ordering and snapshots

Map keys, values, entries, Set values, and their `forEach` callbacks follow insertion order. Replacing an existing Map value does not move that key to a new position. Removing and later re-adding a key/value makes it a new insertion. `keys()`, `values()`, and `entries()` return arrays, so treat them as snapshots of the collection contents at the time of the call rather than live iterators.

Collections are not sorted. Sort a separate array if output order should be lexical or numeric. Do not rely on the default object-key ordering rules from JavaScript plain objects.

## Equality and identity

Map and Set use the runtime's equality behavior for their supported key/value types. Primitive values compare by their supported value semantics; reference objects are distinct values unless the same reference is used. If object identity is part of a program contract, include a focused run case for insertion, lookup through an alias, and deletion.

Avoid using mutable objects as keys when their fields, rather than their identity, are intended to determine lookup. Prefer a stable primitive key or a serialized key chosen by the application.

## Threading and performance

A Map or Set is mutable shared state when multiple bindings hold its reference. It is not a lock or a concurrent collection. Do not mutate it from multiple worker or parallel threads without a synchronization mechanism supported by CP3.

For large traversals, call `keys()`/`entries()` only when the returned array is needed; each creates a result collection. For one-pass side effects, `forEach` avoids explicitly constructing that array. Measure hot paths rather than assuming collection operation costs from a different runtime.

## LRUCache

`LRUCache<K, V>` is a capacity-bounded map. `new LRUCache<K, V>(capacity)`
fixes the maximum entry count. `set` and `get` mark a key most recently used;
inserting past `capacity` evicts the least recently used entry. `has` does
not affect eviction order. `size` and `capacity` are read-only properties, and
`keys()` returns keys most-recently-used first.

```ts
const cache = new LRUCache<string, number>(2);
cache.set("a", 1);
cache.set("b", 2);
cache.get("a"); // refresh "a"
cache.set("c", 3); // evicts "b"
```

## Queue

`Queue<T>` is a first-in-first-out queue: `new Queue<T>()`. `enqueue(value)`
appends to the back, `dequeue()` removes and returns the front value (or
`undefined` when empty), `peek()` reads the front without removing it (or
`undefined` when empty), `clear()` empties the queue, `isEmpty()` reports
whether it holds no values, and `size` is the value count.

```ts
const q = new Queue<number>();
q.enqueue(1);
q.enqueue(2);
q.dequeue(); // 1
```

## Stack

`Stack<T>` is a last-in-first-out stack: `new Stack<T>()`. `push(value)` adds
to the top, `pop()` removes and returns the top value (or `undefined` when
empty), `peek()` reads the top without removing it (or `undefined` when
empty), `clear()` empties the stack, `isEmpty()` reports whether it holds no
values, and `size` is the value count.

```ts
const s = new Stack<number>();
s.push(1);
s.push(2);
s.pop(); // 2
```

## PriorityQueue

`PriorityQueue<T>` is a binary heap: `new PriorityQueue<T>()` or
`new PriorityQueue<T>(comparator)`. The default is a min-heap on `<`: the
smallest element pops first. A comparator is `(a: T, b: T) => boolean`, true
when `a` has higher priority than `b` (pops first); a max-heap is
`(a, b) => a > b`. `push(value)` inserts, `pop()` removes and returns the
highest-priority element, and `peek()` returns it without removing it. Both
`pop()` and `peek()` throw an `Error` when the queue is empty. Among elements
neither of which outranks the other, pop order is unspecified (a heap, not a
stable sort).

```ts
const heap = new PriorityQueue<number>();
heap.push(5);
heap.push(1);
heap.pop(); // 1

const maxHeap = new PriorityQueue<number>((a, b) => a > b);
maxHeap.push(5);
maxHeap.push(1);
maxHeap.pop(); // 5
```

## RingBuffer

`RingBuffer<T>` is a fixed-capacity ring with overwrite mode:
`new RingBuffer<T>(capacity)` takes exactly one argument. `push(value)`
appends as the newest element; when the ring is full the oldest element is
overwritten and the ring never grows. `pop()` removes and returns the oldest
element (or `undefined` when empty), `peek()` reads the oldest value without
removing it (or `undefined` when empty), `clear()` empties the ring, and
`size`, `capacity`, and `isFull` are read-only properties. `isFull` is true
when the ring holds `capacity` elements and the next `push` will overwrite
the oldest one.

```ts
const ring = new RingBuffer<number>(3);
ring.push(1);
ring.push(2);
ring.push(3);
ring.push(4); // overwrites 1
ring.pop(); // 2
```

## BitSet

`BitSet` has no type arguments, and its constructor is `new BitSet()` or
`new BitSet(size)` with an optional size in bits. Flags are addressed by
`number` index: `set(index)` sets a flag (growing the set when needed),
`get(index)` reports a flag (false beyond the size), `clear(index)` clears a
flag (a no-op beyond the size), and `count()` returns the number of set flags.
`and(other)`, `or(other)`, and `xor(other)` each return a fresh `BitSet` and
leave both operands unchanged.

```ts
const flags = new BitSet();
flags.set(0);
flags.set(2);
flags.count(); // 2
flags.and(other);
```

## MultiMap

`MultiMap<K, V>` maps each key to a list of values: `new MultiMap<K, V>()`.
`add(key, value)` appends a value to the list under `key`, `get(key)` returns
all values under `key` in add order (an empty array when the key is absent),
`has(key)` reports whether the key exists, `delete(key)` removes the key and
all its values and reports whether a key was removed, `clear()` removes every
entry, and `size` is the number of distinct keys.

```ts
const grouped = new MultiMap<string, number>();
grouped.add("a", 1);
grouped.add("a", 2);
grouped.get("a"); // [1, 2]
```

## ConcurrentMap

`ConcurrentMap<K, V>` is a thread-safe map: `new ConcurrentMap<K, V>()`. Every
member is atomic with respect to the others on the same instance (mutex-guarded).
`set(key, value)` inserts or replaces, `get(key)` returns `V | undefined`, `has(key)`
reports presence, `delete(key)` removes and reports whether a key was removed, and
`size` is the entry count. There is no cross-thread ordering guarantee, and `has`
is not atomic with a following `get`.

```ts
const shared = new ConcurrentMap<string, number>();
shared.set("hits", 1);
shared.get("hits"); // 1
```

## Full API table

| Type | Constructor | Member | Signature |
| --- | --- | --- | --- |
| Map | `new Map<K, V>()` | `set` | `set(key: K, value: V): void` |
| Map | | `get` | `get(key: K): V \| undefined` |
| Map | | `has`, `delete` | `(key: K): boolean` |
| Map | | `clear` | `clear(): void` |
| Map | | `keys`, `values`, `entries` | `K[]`, `V[]`, `[K, V][]` |
| Map | | `forEach` | `(value: V, key: K) => void` callback, in that argument order |
| Map | `Map.groupBy(...)` (static) | `groupBy` | `groupBy(list: T[], callback: (item: T, index: number) => K): Map<K, T[]>` (array input only) |
| Map | | `size` | Read-only count property |
| Set | `new Set<T>()` | `add` | `add(value: T): void` |
| Set | | `has`, `delete` | `(value: T): boolean` |
| Set | | `clear` | `clear(): void` |
| Set | | `values` | `values(): T[]` |
| Set | | `forEach` | `(value: T) => void` callback |
| Set | | `union` | `union(other: Set<T>): Set<T>` (fresh set) |
| Set | | `intersection` | `intersection(other: Set<T>): Set<T>` (fresh set) |
| Set | | `difference` | `difference(other: Set<T>): Set<T>` (fresh set) |
| Set | | `isSubsetOf` | `isSubsetOf(other: Set<T>): boolean` |
| Set | | `size` | Read-only count property |
| LRUCache | `new LRUCache<K, V>(capacity)` | `set` | `set(key: K, value: V): void` |
| LRUCache | | `get` | `get(key: K): V \| undefined` |
| LRUCache | | `has`, `delete` | `(key: K): boolean` |
| LRUCache | | `clear` | `clear(): void` |
| LRUCache | | `keys` | `keys(): K[]`, most-recently-used first |
| LRUCache | | `size`, `capacity` | Read-only count properties |
| Queue | `new Queue<T>()` | `enqueue` | `enqueue(value: T): void` |
| Queue | | `dequeue`, `peek` | `(): T \| undefined` |
| Queue | | `clear` | `clear(): void` |
| Queue | | `isEmpty` | `isEmpty(): boolean` |
| Queue | | `size` | Read-only count property |
| Stack | `new Stack<T>()` | `push` | `push(value: T): void` |
| Stack | | `pop`, `peek` | `(): T \| undefined` |
| Stack | | `clear` | `clear(): void` |
| Stack | | `isEmpty` | `isEmpty(): boolean` |
| Stack | | `size` | Read-only count property |
| PriorityQueue | `new PriorityQueue<T>()` or `new PriorityQueue<T>(comparator)` | `push` | `push(value: T): void` |
| PriorityQueue | | `pop`, `peek` | `(): T` (throws an `Error` when empty) |
| RingBuffer | `new RingBuffer<T>(capacity)` | `push` | `push(value: T): void` |
| RingBuffer | | `pop`, `peek` | `(): T \| undefined` |
| RingBuffer | | `clear` | `clear(): void` |
| RingBuffer | | `size`, `capacity`, `isFull` | Read-only properties |
| BitSet | `new BitSet()` or `new BitSet(size)` | `set`, `clear` | `(index: number): void` |
| BitSet | | `get` | `(index: number): boolean` |
| BitSet | | `count` | `count(): number` |
| BitSet | | `and`, `or`, `xor` | `(other: BitSet): BitSet` (fresh set) |
| MultiMap | `new MultiMap<K, V>()` | `add` | `add(key: K, value: V): void` |
| MultiMap | | `get` | `get(key: K): V[]` |
| MultiMap | | `has`, `delete` | `(key: K): boolean` |
| MultiMap | | `clear` | `clear(): void` |
| MultiMap | | `size` | Read-only count property (distinct keys) |
| ConcurrentMap | `new ConcurrentMap<K, V>()` | `set` | `set(key: K, value: V): void` (thread-safe) |
| ConcurrentMap | | `get` | `get(key: K): V \| undefined` (thread-safe) |
| ConcurrentMap | | `has`, `delete` | `(key: K): boolean` (thread-safe) |
| ConcurrentMap | | `size` | Read-only count property (thread-safe, point-in-time) |

All types are imported from `"std/collections"`:
`import { Map, Set, LRUCache, Queue, Stack, PriorityQueue, RingBuffer, BitSet, MultiMap, ConcurrentMap } from "std/collections";`

## Tests and implementation

The standard-library collection tests cover generic construction, missing Map lookups, insertion order, replacement, deletion, clear, and iteration, plus deterministic Queue FIFO, Stack LIFO, PriorityQueue min/max order, RingBuffer overwrite, BitSet `set`/`get`/`count`/`and`/`or`/`xor`, MultiMap `add`/`get`, ConcurrentMap `set`/`get`, and LRU eviction cases — all importing from `"std/collections"`. Unknown members are diagnosed at compile time.
