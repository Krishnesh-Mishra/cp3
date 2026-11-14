---
title: Arrays and object literals
---
# Arrays and object literals

Arrays and object values carry reference semantics. The compiler still checks their element and property shapes where a declared type provides that information.

## Array literals and element types

```ts
const scores: number[] = [8, 13, 21];
const names = ["Ada", "Grace"];
```

The element type can be inferred from a non-empty literal. Give an empty array an annotation because there is no element from which to infer a useful type:

```ts
const pending: string[] = [];
```

`Array<T>` is the equivalent generic spelling in an annotation, and `Array<T>(n)` (or `new Array<T>(n)`) constructs a length-form array. The type argument is required for the length form; a bare `new Array(n)` is rejected (`CP3401`) unless the element type is pinned by an annotation or a `.fill(...)` chain. See [Arrays](./04-arrays.md) for the method surface.

An array annotation may carry the `readonly` qualifier — `readonly number[]` or `readonly [number, number]`. It is accepted only on array and tuple types; it does not add any runtime behavior.

## Index signatures in object type literals

Interfaces and object type literals accept an index signature `[name: string]: T` (or `[n: number]: T`):

```ts
interface Dict {
  [name: string]: number;
}
```

Member access resolves to the index value type, and an object literal carrying extra properties is accepted against a shape with an index signature even if it does not mention every named member. Use `Map<K, V>` when the key set is fully dynamic; CP3 does not offer index signatures on arbitrary classes.

## Indexed access

```ts
const first: number = scores[0];
```

The index expression is checked against the array's element type. In supported operations an absent element reads as `undefined` — such as `pop` and `shift` on an empty array; do not confuse that with an in-range indexed value. Typed arrays and `DataView` have their own bounds and conversion rules.

## Array methods

CP3 implements a broad array method surface, including `push`, `pop`, `shift`, `unshift`, `map`, `filter`, `reduce`, `find`, `some`, `every`, `slice`, `sort`, `includes`, and `indexOf`. Exact callbacks and return types are listed on the arrays page. See [Array methods](./04-arrays.md) for the full reference.

Some operations mutate the existing array (`push`, `pop`, `sort`, `unique`, `shuffle`); others return a new array (`map`, `filter`, `slice`). Read the individual method description before assuming JS behavior or a copy boundary.

## Object literals

```ts
const user = {
  name: "Asha",
  active: true,
};
```

Object property values are inferred from their initializers. An interface can declare an expected object shape:

```ts
interface User {
  name: string;
  active: boolean;
}

const user: User = { name: "Asha", active: true };
```

The analyzer checks missing and excess properties in the source forms covered by its structural object checks. Interfaces and object type literals accept an index signature (`[name: string]: T`); use a supported `Map<K, V>` when keys are fully dynamic at runtime.

## Object spread

An object literal may spread a source: `{ ...base, extra: 1 }`. When the source has a statically known closed shape, its fields are contributed to the literal's shape; an unknown, computed, or open source keeps the literal open rather than inventing fields. The object is assembled by assignment in source order, so a later property overrides a key contributed by an earlier spread — `{ ...base, y: 3 }` produces an object whose `y` is `3`.

```ts
const base = { x: 1, y: 2 };
const extended = { ...base, y: 3 }; // { x: number, y: number }
extended.y; // 3
```

Object spread is not a runtime deep clone: the result is built field by field, so nested objects stay shared references. A spread of a general array (e.g. `add(...xs)`) is also accepted in call position for a fixed arity. To build a new shape from a reused object, prefer spread; `Object.assign(target, source)` mutates the target in place instead — and with a fresh literal target it produces the merged shape (JS key order, later sources win), which is the difference to know when printing or enumerating the result. See [Global builtins](../99-global-builtins.md) for the full `Object.*` surface.

## Aliasing and mutation

Assigning an array or object reference usually copies a handle, not the whole graph. Two bindings may therefore observe a mutation through the same underlying value. The language does not promise copy-on-write or deep immutability for `const` references.

```ts
const original = [1];
const alias = original;
alias.push(2);
// original now observes the added element as well
```

This sharing model is part of CP3's lifetime rules for reference-shaped values. See [Values and references](../../internal-engineering/02-no-garbage-collector.md).

## Choosing a container

- Use `T[]` for ordered, indexable sequences.
- Use `Map<K, V>` for key/value lookup.
- Use `Set<T>` for distinct values.
- Use typed arrays for fixed-width numeric storage and binary interfaces.
- Use `Buffer` for byte strings and encoding conversion.

These types have different element, bounds, and mutation behavior; they are not interchangeable merely because JavaScript can represent them all as objects.

## Related pages

- [Arrays](./04-arrays.md)
- [Classes and interfaces](./02-classes-and-interfaces.md)
- [Global builtins](../99-global-builtins.md)
- [Types and inference](../01-variables-and-bindings/02-types.md)
