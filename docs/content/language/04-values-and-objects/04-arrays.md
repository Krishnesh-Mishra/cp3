---
title: Arrays
---
# Arrays

Arrays are built-in typed, growable sequences. They do not require importing a standard module. An annotation such as `number[]` describes an array whose elements are numbers.

```ts
const scores: number[] = [8, 10, 9];
scores.push(11);
```

The array has an element type. Assignments and method arguments are checked against it. An empty array often needs an annotation because there is no element from which to infer the intended type:

```ts
const names: string[] = [];
```

## Construction and copying

Array literals use bracket syntax. `Array.of(...)` creates an array from its arguments, while `Array.from(arrayOrTypedArray)` copies the supported source into an ordinary array.

```ts
const direct: number[] = [2, 4, 6];
const fromArgs = Array.of(2, 4, 6);
const copy = Array.from(direct);
```

`Array.from` currently accepts an ordinary array or a supported typed array. It does not accept every general JavaScript iterable. The analyzer reports unsupported inputs rather than treating any value with an iterator as a valid source.

The call form `Array(n)` creates a length-form array for supported default-initializable element values. Reference-shaped element types generally need values supplied explicitly; a class instance cannot be fabricated by zero-initializing a vector slot.

## Element access and length

`length` is the current number of elements. Array indexing is zero-based:

```ts
const first = scores[0];
const count = scores.length;
```

The type checker uses the array's element type when checking an indexed read or write. Bounds behavior for ordinary arrays follows the runtime's checked access rules; do not infer that arbitrary indices are valid from the static element type. Typed arrays have their own distinct out-of-range contract.

## Mutating operations

| Method | Signature | Behavior |
| --- | --- | --- |
| `push` | `push(...items: T[]): number` | Appends items and returns the resulting length. |
| `unshift` | `unshift(...items: T[]): number` | Inserts items at the front and returns the resulting length. |
| `pop` | `pop(): T \| undefined` | Removes and returns the last item, or `undefined` when empty. |
| `shift` | `shift(): T \| undefined` | Removes and returns the first item, or `undefined` when empty. |
| `sort` | `sort(comparator?): T[]` | Sorts the receiver in place and returns it. |
| `reverse` | `reverse(): T[]` | Reverses the receiver in place and returns it. |
| `splice` | `splice(start: number, deleteCount: number, ...items: T[]): T[]` | Removes `deleteCount` elements at `start`, inserts `items`, and returns the removed elements. |
| `copyWithin` | `copyWithin(target: number, start: number, end?: number): T[]` | Copies a slice of the array onto `target` in place (overlap-safe) and returns the receiver. |
| `fill` | `fill(value, start?, end?): T[]` | Writes the supplied value to a selected range and returns the receiver. |
| `unique` | `unique(): T[]` | Removes adjacent duplicate values in place and returns the receiver. |
| `shuffle` | `shuffle(): T[]` | Reorders the receiver in place and returns it. |

These operations mutate the array object. A `const` binding can call them because `const` prevents rebinding the name; it does not deeply freeze the array. See [Mutability and immutability](../01-variables-and-bindings/03-mutability.md).

`unique()` removes adjacent duplicates; it does not sort first. If equal values are separated, they remain separated unless you sort or otherwise group them before calling `unique()`.

`shuffle()` uses the runtime's seeded pseudo-random generator. It is useful for ordinary randomized ordering, not for keys, tokens, or cryptographic security.

## Non-mutating and result-producing operations

| Method | Result | Notes |
| --- | --- | --- |
| `slice(start?, end?)` | `T[]` | Copies the selected range into another array. |
| `at(index)` | `T \| undefined` | Element at `index`; a negative index counts from the end, and out-of-range returns `undefined`. |
| `toSorted(comparator?)` | `T[]` | Sorted copy of the receiver; the receiver is unchanged. Uses the same type-aware default ordering and stable comparator contract as `sort`. |
| `toReversed()` | `T[]` | Reversed copy; the receiver is unchanged. |
| `concat(...items)` | `T[]` | New array with the receiver's elements followed by the arguments; arrays are spread. |
| `toSpliced(start, deleteCount, ...items)` | `T[]` | Like `splice` but returns a new array and leaves the receiver untouched. |
| `with(index, value)` | `T[]` | Copy with the element at `index` replaced; negative indices count from the end. |
| `map(callback)` | `U[]` | Applies a callback and collects each returned value. |
| `flatMap(callback)` | `U[]` | Maps every element, then flattens the returned arrays by one level. The callback must return an array. |
| `filter(predicate)` | `T[]` | Returns the elements for which the predicate is true. |
| `flat()` | `T[]` | Flattens the supported nested array level. |
| `join(separator?)` | `string` | Converts elements into a delimited string. |

`map` infers the callback result type. `filter` preserves the receiver's element type. These methods allocate a result collection; on a hot path, compare their clarity with a loop and measure the generated program rather than assuming callbacks disappear.

## Search operations

| Method | Result | Meaning |
| --- | --- | --- |
| `includes(value, fromIndex?)` | `boolean` | Whether an equal element occurs in the searched range. |
| `indexOf(value, fromIndex?)` | `number` | First matching position, or `-1`. |
| `lastIndexOf(value, fromIndex?)` | `number` | Last matching position, or `-1`. |
| `find(predicate)` | `T \| undefined` | First matching element, or `undefined`. |
| `findIndex(predicate)` | `number` | First matching position, or `-1`. |
| `findLast(predicate)` | `T \| undefined` | Last matching element, or `undefined`. |
| `findLastIndex(predicate)` | `number` | Last matching position, or `-1`. |
| `some(predicate)` | `boolean` | Whether at least one element matches. |
| `every(predicate)` | `boolean` | Whether all elements match. |
| `binarySearch(target)` | `number` | Searches a sorted array and returns the matching position, or `-1` when absent. |

`binarySearch` requires sorted input; searching an unsorted array is a caller error and the result is not meaningful. Sorting order depends on element type or an explicit comparator. Use the same ordering convention for sorting and searching.

## Reduce and accumulator types

`reduce` combines elements into one result. With an initial value, the initial value determines the accumulator type:

```ts
const values: number[] = [2, 3, 4];
const total = values.reduce((sum, value) => sum + value, 0);
```

The initial value also makes reduction of an empty array meaningful. Without it, reduction begins from the first element and keeps the element type as the accumulator type:

```ts
const total = values.reduce((sum, value) => sum + value);
```

For an empty array, provide an initial value when a result is required. The analyzer checks callback arity and types; a reducer without an initial value cannot change to a different accumulator type.

`reduceRight` folds from the last element to the first instead of the first to the last: its signature is `reduceRight(combine, initial?)`, with the same callback-context rules as `reduce` and the same initial-value behavior.

## Callback typing

`map`, `filter`, `reduce`, `reduceRight`, `find`, `findIndex`, `findLast`, `findLastIndex`, `some`, `every`, `flatMap`, and `forEach` check callbacks using the array element type. Inline arrows receive contextual parameter types:

```ts
const words: string[] = ["maple", "ash", "oak"];
const longWords = words.filter(word => word.length > 3);
```

Named function values are also accepted when their signatures match the method. `filter`, `some`, and `every` callbacks must produce a boolean-compatible result. `map`'s result type comes from the callback return type. `flatMap`'s callback must return an array; `forEach` returns `undefined` and its callback's return value is ignored.

Callbacks execute once per element visited, subject to the method's early-exit behavior. A callback can have side effects, so replacing a method chain with a fused loop is only valid when the compiler can prove the transformation preserves them.

## Sorting and ordering

`sort()` uses CP3's type-aware ordering and is stable. For strings, ordering is lexicographic. For numbers, it is numeric rather than JavaScript's default string conversion. A comparator returns a negative number when the first value should appear earlier, a positive number when it should appear later, and zero when they compare equal:

```ts
const descending = scores.slice().sort((left, right) => right - left);
```

The receiver is modified. Use `slice()` first when the original order must be retained. Keep comparator behavior consistent and avoid side effects inside comparisons.

## Iteration views

`keys()` returns the indices of the array as `number[]`. `values()` returns a copy of the elements as `T[]`. `entries()` returns `[number, T][]` pairs of `[index, value]`. All three return new arrays; they are not lazy views, and mutating them does not affect the receiver.

## Aliasing and copies

Assigning an array to another binding copies the reference to the same array object, not all of its elements:

```ts
const first = [1, 2];
const alias = first;
alias.push(3);
// first now also observes the third element
```

Use `slice()` or `Array.from()` when a separate array container is needed. A shallow array copy still contains the same referenced objects as the original; it does not recursively clone their members.

## Cost and performance

Arrays are growable, so appends may increase capacity and move stored values. `push` is usually the natural append operation. Front insertion/removal can shift later elements and is more costly for large arrays. Creating intermediate arrays with `map`, `filter`, or `slice` uses additional storage. Measure a program that is actually hot before reaching for manual tuning.

## Method inventory

The implemented regular-array method table includes `length`, `push`, `unshift`, `pop`, `shift`, `join`, `sort`, `includes`, `filter`, `find`, `findLast`, `findIndex`, `findLastIndex`, `some`, `every`, `map`, `flatMap`, `forEach`, `reduce`, `reduceRight`, `indexOf`, `lastIndexOf`, `binarySearch`, `slice`, `at`, `toSorted`, `toReversed`, `reverse`, `splice`, `toSpliced`, `concat`, `with`, `copyWithin`, `keys`, `values`, `entries`, `flat`, `unique`, `shuffle`, and `fill`. Static construction helpers include `Array.of`, `Array.from`, and `Array.isArray` (a compile-time constant boolean).

The method names resemble JavaScript, but use the signatures and semantics described in this guide. Check the compiler's diagnostics when an overload or general iterable form is not accepted.

## Related pages and tests

- [Strings](./05-strings.md)
- [Typed arrays, buffers, and DataView](../../standard-library/16-typed-arrays.md)
- [Arrays and object literals](./01-arrays-and-objects.md)
- Array behavior is covered across the `arrays-strings`, `perf-array`, and general test families.
