---
title: Collections
---
# Collections

The collection types are TypeScript-style ambient builtins — `Map`, `Set`, `LRUCache`, `Queue`, `Stack`, `PriorityQueue`, `RingBuffer`, `BitSet`, `MultiMap`, `ConcurrentMap` resolve in value and type positions with no import. `import { Map, Set, … } from "std/collections"` remains valid for source compatibility and narrows identically; there are no per-type modules and **no** `std/collections/types` facade (ambient collections need no annotation facade). Collections are reference values — assigning a binding shares the collection, and they are not synchronization primitives (except `ConcurrentMap`).

Numeric widths: a `typescript` project keeps `number`; a `rust`/`cpp` project uses `usize` for `size` and LRUCache's `capacity`. Key equality: primitives by value, references by identity; prefer stable primitive or serialized keys.

## `Map<K, V>`

| Member | Signature | Notes |
| --- | --- | --- |
| `new Map<K, V>()` | `Map<K, V>` | Both type arguments required |
| `new Map<K, V>(entries)` | `Map<K, V>` | Accepts an array of `[K, V]` pairs (e.g. `Object.entries`); not a general iterable |
| `set` | `set(key: K, value: V): void` | Inserts/replaces; replaced key keeps its insertion position |
| `get` | `get(key: K): V \| undefined` | `undefined` distinguishes missing from stored |
| `has` / `delete` | `(key: K) => boolean` | `delete` reports whether the key was present |
| `clear` | `clear(): void` | |
| `keys` / `values` / `entries` | `(): K[]`, `(): V[]`, `(): [K, V][]` | Insertion order; snapshot arrays, `entries()` is storable/iterable/spreadable |
| `forEach` | `forEach(callback: (value: V, key: K) => void): void` | Arg order is `(value, key)` |
| `Map.groupBy` | `Map.groupBy(list: T[], callback: (item: T, index: number) => K): Map<K, T[]>` | Static; array input only; keys first-seen order; groups preserve input order; `K` follows the callback's return type |
| `size` | `usize` (count) | Read-only |

## `Set<T>`

| Member | Signature | Notes |
| --- | --- | --- |
| `new Set<T>()` | `Set<T>` | One type argument required |
| `new Set<T>(elements)` | `Set<T>` | Accepts an array of elements |
| `add` | `add(value: T): void` | Inserts if absent; returns `void` |
| `has` / `delete` | `(value: T) => boolean` | |
| `clear` | `clear(): void` | |
| `values` | `values(): T[]` | Insertion order; snapshot |
| `forEach` | `forEach(callback: (value: T) => void): void` | Insertion order |
| `union` | `union(other: Set<T>): Set<T>` | Fresh set; receiver order, then values new to `other` |
| `intersection` | `intersection(other: Set<T>): Set<T>` | Fresh set; receiver order |
| `difference` | `difference(other: Set<T>): Set<T>` | Fresh set; receiver order |
| `isSubsetOf` | `isSubsetOf(other: Set<T>): boolean` | Every receiver value in `other` |
| `size` | `usize` (count) | Read-only |

`for...of` walks arrays and strings, not bare collections; iterate `values()`/`entries()`. `Record<string, V>` canonicalizes to `Map<string, V>` at runtime.

## Extended collections

| Class | Constructor | Members |
| --- | --- | --- |
| `LRUCache<K, V>` | `new LRUCache<K, V>(capacity: number)` | `set(key: K, value: V): void` (marks MRU); `get(key: K): V \| undefined` (marks MRU); `has(key: K): boolean` (no recency); `delete(key: K): boolean`; `clear(): void`; `keys(): K[]` (MRU first); read-only `size`, `capacity`; inserting past `capacity` evicts the LRU entry |
| `Queue<T>` | `new Queue<T>()` | `enqueue(value: T): void`; `dequeue(): T \| undefined`; `peek(): T \| undefined`; `clear(): void`; `isEmpty(): boolean`; `size` |
| `Stack<T>` | `new Stack<T>()` | `push(value: T): void`; `pop(): T \| undefined`; `peek(): T \| undefined`; `clear(): void`; `isEmpty(): boolean`; `size` |
| `PriorityQueue<T>` | `new PriorityQueue<T>()` or `new PriorityQueue<T>(comparator: (a: T, b: T) => boolean)` | Default min-heap on `<`; comparator true when `a` outranks `b`; `push(value: T): void`; `pop(): T`; `peek(): T` — `pop`/`peek` **throw `Error` when empty**; equal-priority order unspecified |
| `RingBuffer<T>` | `new RingBuffer<T>(capacity: number)` | Fixed capacity; full overwrites the oldest; `push(value: T): void`; `pop(): T \| undefined` (oldest first); `peek(): T \| undefined`; `clear(): void`; read-only `size`, `capacity`, `isFull` |
| `BitSet` | `new BitSet()` or `new BitSet(size: number)` | No type arguments; indices are `number`; `set(index: number): void` (grows); `clear(index: number): void`; `get(index: number): boolean`; `count(): number`; `and(other: BitSet): BitSet`, `or(...)`, `xor(...)` — fresh sets |
| `MultiMap<K, V>` | `new MultiMap<K, V>()` | `add(key: K, value: V): void`; `get(key: K): V[]` (add order; empty array when absent); `has(key: K): boolean`; `delete(key: K): boolean`; `clear(): void`; `size` (distinct keys) |
| `ConcurrentMap<K, V>` | `new ConcurrentMap<K, V>()` | Mutex-guarded, thread-safe; members atomic w.r.t. each other on the instance (`set`/`get`/`has`/`delete`/`size`); no cross-thread ordering guarantee; `has` is not atomic with a following `get` |

Insertion order governs iteration; replacing a Map value does not move its key. The collections are not sorted — sort a separate array for value-based order. Map/Set do not inherit plain-object property semantics. `import { Map, Set } from "std/collections"` is source-compatible with the ambient names.

Behavior details: [guide](../../standard-library/12-collections.md)
