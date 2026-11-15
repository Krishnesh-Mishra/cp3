---
title: Collections
---
# Collections

`Map`, `Set`, and the extended collection classes are ambient builtins — no import, mirroring TypeScript. `import { Map, Set } from "std/collections"` remains valid for source compatibility and narrows identically. Collections are reference values: assigning a binding copies the handle, not the entries, and they are not synchronization primitives. See the [collections guide](../../standard-library/12-collections.md) for behavior, ordering, and key-equality rules.

## Map

| Member | Signature | Notes |
| --- | --- | --- |
| `new Map<K, V>()` | `Map<K, V>` | Supply both type arguments; the constructor does not infer an empty map's key/value pair. |
| `new Map<K, V>(entries)` | `Map<K, V>` | Accepts an array of `[K, V]` pairs (e.g. the result of `Object.entries` or `.entries()`). Not a general iterable. |
| `set` | `set(key: K, value: V): void` | Inserts or replaces; a replaced key keeps its original insertion position. Returns `void`. |
| `get` | `get(key: K): V \| undefined` | `undefined` distinguishes missing from stored; check with `has` when the distinction matters. |
| `has` | `has(key: K): boolean` | |
| `delete` | `delete(key: K): boolean` | True when the key was present. |
| `clear` | `clear(): void` | |
| `size` | `size: usize`-width count | Read-only; exact count width (`usize`; `number` in TypeScript projects). |
| `keys` | `keys(): K[]` | In insertion order; a snapshot array, not a live view. |
| `values` | `values(): V[]` | Insertion order; snapshot. |
| `entries` | `entries(): [K, V][]` | Insertion order; snapshot. Storable, iterable, spreadable. |
| `forEach` | `forEach(callback: (value: V, key: K) => void): void` | Note the argument order: value first, then key. |
| `Map.groupBy` | `Map.groupBy(list: T[], callback: (item: T, index: number) => K): Map<K, T[]>` | Static; array input only; `K` follows the callback's return type (not restricted to strings — use `Object.groupBy` for the string-key record form); groups preserve input order; keys appear in first-seen order. |
| `map[k]` | read → `V \| undefined`, write → `V` | `Record<string, V>` subscript: string keys (or string-literals / type parameters) only. Read behaves like `get(k)` — a missing key reads `undefined`; the key must be a string. Other key types are outside the specified subscript surface. |

```ts
const counts = new Map<string, number>();
counts.set("apples", 3);
const apples = counts.get("apples"); // number | undefined
for (const [k, v] of counts.entries()) console.log(`${k}=${v}`);
```

## Set

| Member | Signature | Notes |
| --- | --- | --- |
| `new Set<T>()` | `Set<T>` | One type argument required. |
| `new Set<T>(elements)` | `Set<T>` | Array of elements. |
| `add` | `add(value: T): void` | Inserts if absent; returns `void`. |
| `has` | `has(value: T): boolean` | |
| `delete` | `delete(value: T): boolean` | True when the value was present. |
| `clear` | `clear(): void` | |
| `size` | count width | Read-only. |
| `values` | `values(): T[]` | Insertion order; snapshot. |
| `forEach` | `forEach(callback: (value: T) => void): void` | |
| `union` | `union(other: Set<T>): Set<T>` | Fresh set; receiver order followed by values new to `other`. |
| `intersection` | `intersection(other: Set<T>): Set<T>` | Fresh set; receiver order. |
| `difference` | `difference(other: Set<T>): Set<T>` | Fresh set; receiver order. |
| `isSubsetOf` | `isSubsetOf(other: Set<T>): boolean` | True when every value of this set is in `other`. |

## Spreading and iterating

- `[...set]` yields the set's values; `[...map]` yields `[K, V]` pairs.
- `for...of` walks arrays and strings only — not bare `Map`/`Set` values. Iterate the snapshot arrays: `for (const [k, v] of map.entries())`, `for (const v of map.values())`, `for (const v of set.values())`.
- Insertion order is the iteration order. Deleting and re-adding makes a new insertion; replacing a value does not move its key. Sort a separate array when order must be value-based.

## `Record<string, V>`

`Record<string, V>` canonicalizes to `Map<string, V>` — the two are the same type at runtime. `Object.fromEntries` builds the map from `[string, V][]`; `Object.groupBy` is the string-key grouping static; `Object.entries` on a known object shape produces the pairs. See [Object](./object.md).

## Extended collections (also ambient)

These resolve with no import as well:

| Class | Constructor | Notes |
| --- | --- | --- |
| `LRUCache<K, V>` | `new LRUCache<K, V>(capacity)` | Capacity-bounded map. `set`/`get` mark a key most recently used; inserting past `capacity` evicts the least recently used entry; `has` does not refresh recency. `keys()` returns keys most-recently-used first; `size` and `capacity` are read-only. |
| `Queue<T>` | `new Queue<T>()` | FIFO. `enqueue(value: T): void`; `dequeue(): T \| undefined`; `peek(): T \| undefined`; `clear(): void`; `isEmpty(): boolean`; `size`. |
| `Stack<T>` | `new Stack<T>()` | LIFO. `push(value: T): void`; `pop(): T \| undefined`; `peek(): T \| undefined`; `clear(): void`; `isEmpty(): boolean`; `size`. |
| `PriorityQueue<T>` | `new PriorityQueue<T>()` / `new PriorityQueue<T>(comparator)` | Binary heap; default min-heap on `<`. Comparator is `(a: T, b: T) => boolean`, true when `a` outranks `b`. `push(value: T): void`; `pop(): T` and `peek(): T` **throw an `Error` when empty**. Pop order among equal-priority elements is unspecified (a heap, not a stable sort). |
| `RingBuffer<T>` | `new RingBuffer<T>(capacity)` | Fixed-capacity ring; full state overwrites the oldest element. `push(value: T): void`; `pop(): T \| undefined` (oldest first); `peek(): T \| undefined`; `clear(): void`; read-only `size`, `capacity`, `isFull`. |
| `BitSet` | `new BitSet()` / `new BitSet(size)` | No type arguments; indices are `number`. `set(index)`, `clear(index)`, `get(index): boolean`, `count(): number`; `and`/`or`/`xor(other: BitSet): BitSet` return fresh sets. |
| `MultiMap<K, V>` | `new MultiMap<K, V>()` | Each key maps to a list of values. `add(key, value)`; `get(key): V[]` (empty array when absent); `has`/`delete(key): boolean`; `clear()`; `size` (distinct keys). |
| `ConcurrentMap<K, V>` | `new ConcurrentMap<K, V>()` | Mutex-guarded, thread-safe; no cross-thread ordering guarantee; `has` is not atomic with a following `get`. Every member is atomic with respect to the others on the same instance. |

Numeric collection values follow the project type-system vocabulary: `size` and count/capacity read-only properties are `usize` (`size_t` in C++); a `typescript` project keeps `number`.

Key equality uses the runtime's supported equality semantics: primitives compare by value; reference objects compare by identity. Avoid mutable objects as keys — prefer a stable primitive or a serialized key.

## Related pages

- [Collections guide](../../standard-library/12-collections.md)
- [Object](./object.md) — `Object.fromEntries`, `Object.groupBy`
- [Spreading](./array.md) — `[...map]`, `[...set]` into arrays
