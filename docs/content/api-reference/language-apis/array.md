---
title: Array
---
# Array

Arrays are built-in, typed, growable sequences: `T[]` and `Array<T>` are the same type. Assignments and method arguments are checked against the element type; an empty array usually needs an annotation. See the [arrays guide](../../language/04-values-and-objects/04-arrays.md) and [arrays and objects](../../language/04-values-and-objects/01-arrays-and-objects.md) for semantics.

## Construction

| Form | Signature | Notes |
| --- | --- | --- |
| `[...]` | `const a: T[] = [e1, e2]` | Element type inferred from a non-empty literal; annotate empty arrays. |
| `Array<T>(n)` / `new Array<T>(n)` | `Array<T>(length: number): T[]` | Length form; the type argument is required. Elements of a default-initializable type (number, string, boolean, nullable, unions of those) start at their default value; reference-shaped elements must be filled (`.fill(...)`). A bare `new Array(n)` is rejected (`CP3401`) without an annotation. |
| `Array.of` | `Array.of(...items: T[]): T[]` | Creates an array from its arguments. |
| `Array.from` | `Array.from(source: T[] \| TypedArray): T[]` | Copies an ordinary array or a supported typed array; general iterables are not accepted. |
| `Array.isArray` | `Array.isArray(value): boolean` | Compile-time-constant boolean. |
| `Array` (bare) | — | A single non-number argument is never a length; rejected. |

`readonly number[]` (and `readonly [number, number]`) is accepted only on array and tuple annotations; it adds no runtime behavior.

## Properties and access

| Member | Signature | Notes |
| --- | --- | --- |
| `length` | `length: number` | Current number of elements. |
| `a[i]` | read → `T`, write checked against `T` | Zero-based. Ordinary arrays are bounds-checked: an out-of-range index is a checked runtime failure, not a read of garbage and not `undefined`. (Typed arrays differ — see [Typed arrays](./typed-arrays.md).) |
| `for...of` | `for (const x of a)` | Iterates elements in order. Walks arrays and strings only — iterate `map.values()` / `map.entries()` / `set.values()` arrays for collections, or spread (`[...set]`, `[...map]`). |
| spread | `[...a, ...b]`, `[...set]`, `[...map]` | `[...a]` copies the elements; `[...set]` yields values; `[...map]` yields `[K, V]` entries; `[...typedArray]` yields numbers. |
| `at` | `at(index: number): T \| undefined` | Negative indices count from the end; out-of-range returns `undefined`. |

## Mutating methods (receiver changed in place)

| Method | Signature | Notes |
| --- | --- | --- |
| `push` | `push(...items: T[]): number` | Appends; returns new length. |
| `unshift` | `unshift(...items: T[]): number` | Inserts at the front; returns new length. |
| `pop` | `pop(): T \| undefined` | Removes and returns the last element, or `undefined` when empty. |
| `shift` | `shift(): T \| undefined` | Removes and returns the first element, or `undefined` when empty. |
| `sort` | `sort(comparator?: (a: T, b: T) => number): T[]` | Sorts the receiver in place; stable; type-aware default ordering (numbers numeric, strings lexicographic); comparator returns negative/zero/positive. |
| `reverse` | `reverse(): T[]` | Reverses in place. |
| `splice` | `splice(start: number, deleteCount: number, ...items: T[]): T[]` | Removes `deleteCount` at `start`, inserts `items`, returns removed elements. |
| `copyWithin` | `copyWithin(target: number, start: number, end?: number): T[]` | Copies a slice onto `target` in place; overlap-safe. |
| `fill` | `fill(value: T, start?: number, end?: number): T[]` | Writes `value` to the selected range. |
| `unique` | `unique(): T[]` | Removes **adjacent** duplicates in place; sort first if you need global dedup. |
| `shuffle` | `shuffle(): T[]` | Reorders in place with the runtime's seeded PRNG; not for keys, tokens, or cryptographic use. |

A `const` binding can call these — `const` prevents rebinding, not mutation. See [mutability](../../language/01-variables-and-bindings/03-mutability.md).

## Non-mutating methods

| Method | Signature | Notes |
| --- | --- | --- |
| `slice` | `slice(start?: number, end?: number): T[]` | Copies the selected range. |
| `concat` | `concat(...items: (T \| T[])[]): T[]` | New array; arrays are spread. |
| `at` | `at(index: number): T \| undefined` | See above. |
| `toSorted` | `toSorted(comparator?: (a: T, b: T) => number): T[]` | Sorted copy; receiver unchanged. Same ordering and stability contract as `sort`. |
| `toReversed` | `toReversed(): T[]` | Reversed copy. |
| `toSpliced` | `toSpliced(start: number, deleteCount: number, ...items: T[]): T[]` | Like `splice` but returns a new array. |
| `with` | `with(index: number, value: T): T[]` | Copy with the element at `index` replaced; negative indices count from the end. |
| `map` | `map<U>(callback: (element: T, index: number, array: T[]) => U): U[]` | Applies the callback and collects results. |
| `flatMap` | `flatMap<U>(callback: (element: T, index: number, array: T[]) => U[]): U[]` | Maps, then flattens returned arrays by one level; callback must return an array. |
| `filter` | `filter(predicate: (element: T, index: number, array: T[]) => boolean): T[]` | Keeps elements the predicate accepts; preserves the element type. |
| `flat` | `flat(): T[]` | Flattens the supported nested array level; a fresh, flattened array is returned. |
| `join` | `join(separator?: string): string` | Delimited string; comma by default. |
| `forEach` | `forEach(callback: (element: T, index: number, array: T[]) => void): void` | Callback invoked once per element; return value ignored. Returns `undefined`. |
| `reduce` | `reduce<U>(combine: (accumulator: U, element: T, index: number, array: T[]) => U, initial?: U): U` | See below. |
| `reduceRight` | `reduceRight<U>(combine: (accumulator: U, element: T, index: number, array: T[]) => U, initial?: U): U` | Like `reduce` but folds last-to-first. |
| `keys` | `keys(): number[]` | New array of indices; not a lazy view. |
| `values` | `values(): T[]` | New array copy of the elements. |
| `entries` | `entries(): [number, T][]` | New array of `[index, value]` pairs. |

`reduce` accumulator: with `initial`, its type determines `U` (and makes reducing an empty array meaningful); without `initial`, reduction starts from the first element and keeps the element type as the accumulator type — an empty array then has no meaningful result. Callback arity and types are checked; a reducer without an initial value cannot switch accumulator type.

## Searching methods

| Method | Signature | Notes |
| --- | --- | --- |
| `includes` | `includes(value: T, fromIndex?: number): boolean` | True when an equal element occurs in the searched range. |
| `indexOf` | `indexOf(value: T, fromIndex?: number): number` | First matching position, or `-1`. |
| `lastIndexOf` | `lastIndexOf(value: T, fromIndex?: number): number` | Last matching position, or `-1`. |
| `find` | `find(predicate: (element: T, index: number, array: T[]) => boolean): T \| undefined` | First matching element, or `undefined`. |
| `findIndex` | `findIndex(predicate: (element: T, index: number, array: T[]) => boolean): number` | First matching position, or `-1`. |
| `findLast` | `findLast(predicate: (element: T, index: number, array: T[]) => boolean): T \| undefined` | Last matching element, or `undefined`. |
| `findLastIndex` | `findLastIndex(predicate: (element: T, index: number, array: T[]) => boolean): number` | Last matching position, or `-1`. |
| `some` | `some(predicate: (element: T, index: number, array: T[]) => boolean): boolean` | True when at least one element matches. |
| `every` | `every(predicate: (element: T, index: number, array: T[]) => boolean): boolean` | True when all elements match. |
| `binarySearch` | `binarySearch(target: T): number` | Position of `target` in a **sorted** array, or `-1`. Caller must sort first — searching an unsorted array is a caller error (the result is not meaningful; debug builds panic). Use the same ordering convention for sorting and searching. |

## Missing elements and aliasing

- `pop`/`shift` on an empty array return `undefined`. `find*` return `undefined` or `-1`. An in-range indexed value is never `undefined`.
- Array literals may omit elements (`[1, , 3]`); the array is dense and an absent position holds the element type's default value.
- Assigning an array copies the handle, not the elements — two bindings observe the same object. Use `slice()` or `Array.from()` for a separate container; copies are shallow.

## Related pages

- [Arrays guide](../../language/04-values-and-objects/04-arrays.md)
- [Typed arrays](./typed-arrays.md)
- [Collections](./collections.md) — `Map`, `Set`
