---
title: Arrays and object literals
chapter: Chapter 1 · The language
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

## Indexed access

```ts
const first: number = scores[0];
```

The index expression is checked and lowered through the array family’s runtime rules. For ordinary arrays, an absent element may be represented as `undefined` in supported operations such as `pop` and `shift`; do not confuse that with an in-range indexed value. Typed arrays and `DataView` have their own bounds and conversion rules.

## Array methods

CP3 implements a broad array method surface, including `push`, `pop`, `shift`, `unshift`, `map`, `filter`, `reduce`, `find`, `some`, `every`, `slice`, `sort`, `includes`, and `indexOf`. Exact callbacks and return types are defined by the compiler’s array member table. See [Array methods](#03-standard-library/14-arrays) for the full reference.

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

The analyzer checks missing and excess properties in the source forms covered by its structural object checks. CP3 rejects open index-signature declarations; use a supported `Map<K, V>` when keys are dynamic.

## Aliasing and mutation

Assigning an array or object reference usually copies a handle, not the whole graph. Two bindings may therefore observe a mutation through the same underlying value. The language does not promise copy-on-write or deep immutability for `const` references.

```ts
const original = [1];
const alias = original;
alias.push(2);
// original now observes the added element as well
```

This sharing model matters to escape analysis and lifetime. See [Values and references](#02-runtime/01-value-model).

## Choosing a container

- Use `T[]` for ordered, indexable sequences.
- Use `Map<K, V>` for key/value lookup.
- Use `Set<T>` for distinct values.
- Use typed arrays for fixed-width numeric storage and binary interfaces.
- Use `Buffer` for byte strings and encoding conversion.

These types have different element, bounds, and mutation behavior; they are not interchangeable merely because JavaScript can represent them all as objects.
