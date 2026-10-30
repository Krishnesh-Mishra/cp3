---
title: Arrays
chapter: Chapter 3 · Standard library
---
# Arrays

Arrays are built-in typed, growable sequences. They do not require importing a standard module. An annotation such as `number[]` describes an array whose elements are numbers.

```ts
const scores: number[] = [8, 10, 9];
scores.push(11);
```

Array methods follow CP3's static type rules. Mutating methods change the array handle's referenced contents; a `const` binding may still call them because `const` prevents reassignment of the binding, not mutation of the object.

## Size and mutation

- `length` reads the number of elements.
- `push(...items)` appends one or more values.
- `pop()` removes and returns the last value, or `undefined` for an empty array.
- `unshift(...items)` prepends values.
- `shift()` removes and returns the first value, or `undefined` when empty.
- `slice(start, end?)` returns a selected range.
- `join(separator?)` creates a string from the elements.

## Searching and predicates

- `includes(value)` tests whether a value is present.
- `indexOf(value)` returns an index or `-1`.
- `find(predicate)` returns the first matching element or `undefined`.
- `findIndex(predicate)` returns the matching index or `-1`.
- `some(predicate)` is true if at least one element matches.
- `every(predicate)` is true if all elements match.
- `binarySearch(value)` searches an array that is already sorted.

Do not call `binarySearch` on unsorted values; its result is meaningful only when the ordering precondition holds.

## Transform and combine

- `map(callback)` returns a new array of transformed values.
- `filter(callback)` returns a new array containing matching values.
- `reduce(callback, initial)` folds an array into one value.
- `flat()` flattens the supported nested array level(s).

`reduce` accepts an optional initial value. Without it, the first element is the starting accumulator; therefore empty arrays need an explicit initial value if a result is required. This also matters when reducing to a type different from the element type.

## Ordering and uniqueness

`sort()` sorts the array in place using CP3's type-aware stable ordering. Because the operation mutates, use a copy when the original order must remain:

```ts
const sorted = values.slice().sort();
```

`unique()` removes adjacent duplicates in place and returns the array; sort first when duplicates are not already adjacent. `shuffle()` mutates the array into a randomized order and returns it. The runtime uses a seeded pseudo-random generator, not a cryptographic source.

## Callback behavior and performance

Array callbacks execute CP3 code for each selected element. Prefer a clear loop when control flow or early exit matters. The compiler's optimization rules are conservative; do not assume every callback or loop is automatically vectorized.

## Method inventory

`length`, `push`, `unshift`, `pop`, `shift`, `join`, `sort`, `includes`, `filter`, `find`, `findIndex`, `some`, `every`, `map`, `reduce`, `indexOf`, `binarySearch`, `slice`, `flat`, `unique`, and `shuffle` are the documented core operations. Read method return values carefully: several methods mutate in place while others create a result array.
