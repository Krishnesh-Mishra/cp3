---
title: Map and Set collections
chapter: Chapter 3 · Standard library
---
# Map and Set collections

`std/collections` provides generic `Map` and `Set` classes as named imports:

```ts
import { Map, Set } from "std/collections";

const counts = new Map<string, number>();
const seen = new Set<string>();
```

Both preserve insertion order when iterating. They are reference values managed by the runtime; see [Values and references](#02-runtime/01-value-model).

## Map

`Map<K, V>` associates keys of type `K` with values of type `V`.

- `set(key, value)` inserts or replaces an entry and returns `void`.
- `get(key)` returns `V | undefined` when the key is absent.
- `has(key)` reports whether a key exists.
- `delete(key)` removes a key and reports whether it was present.
- `clear()` removes all entries.
- `keys()`, `values()`, and `entries()` expose ordered contents.
- `forEach(callback)` visits entries in insertion order.
- `size` is the current entry count.

```ts
counts.set("apples", 3);
const apples = counts.get("apples"); // number | undefined
if (counts.has("apples")) console.log(apples);
```

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

## Iteration and ordering

The collection's order is insertion order. Deleting an entry and adding it again makes it a new insertion. Do not assume sorted order; sort a separate array of keys or values when ordering by value is needed.

## Key behavior

Map and Set key equality follows the runtime's equality semantics for their supported key values. If a program uses object-valued keys, use stable references and test the intended identity behavior. A Map is not a plain object and does not inherit JavaScript object property semantics.

## Concurrency

These collections are ordinary values, not synchronization primitives. Do not mutate one concurrently from worker threads without a supported synchronization protocol. The `std/collections` module exports Map and Set; concurrency primitives are separate language/runtime features.

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
| Both | `size` | Read-only `number` property |

`delete` reports whether an entry/value existed and was removed. `clear` always returns `void`. `get` distinguishes a missing key by returning `undefined`, so use `has` when the distinction between “missing” and a stored value matters.

## Ordering and snapshots

Map keys, values, entries, Set values, and their `forEach` callbacks follow insertion order. Replacing an existing Map value does not move that key to a new position. Removing and later re-adding a key/value makes it a new insertion. `keys()`, `values()`, and `entries()` return arrays, so treat them as snapshots of the collection contents at the time of the call rather than live iterators.

Collections are not sorted. Sort a separate array if output order should be lexical or numeric. Do not rely on the default object-key ordering rules from JavaScript plain objects.

## Equality and identity

Map and Set use the runtime's equality behavior for their supported key/value types. Primitive values compare by their supported value semantics; reference objects are distinct values unless the same reference is used. If object identity is part of a program contract, include a focused run case for insertion, lookup through an alias, and deletion.

Avoid using mutable objects as keys when their fields, rather than their identity, are intended to determine lookup. Prefer a stable primitive key or a serialized key chosen by the application.

## Threading and performance

A Map or Set is mutable shared state when multiple bindings hold its reference. It is not a lock or a concurrent collection. Do not mutate it from multiple worker or parallel threads without a synchronization mechanism supported by CP3.

For large traversals, call `keys()`/`entries()` only when the returned array is needed; each creates a result collection. For one-pass side effects, `forEach` avoids explicitly constructing that array. Measure hot paths rather than assuming collection operation costs from a different runtime.

## Tests and implementation

The standard-library collection tests cover generic construction, missing Map lookups, insertion order, replacement, deletion, clear, and iteration. Semantic checks and the closed member tables are in `src/std/collections/sema.cpp`; runtime storage and equality behavior are in the same module family. Unknown members are diagnosed at compile time.
