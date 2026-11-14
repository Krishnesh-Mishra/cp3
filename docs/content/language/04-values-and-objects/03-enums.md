---
title: Enums
---
# Enums

CP3 supports enum declarations with number- or string-backed members. The checker enforces one backing kind per declaration, validates member access and assignment, and treats number-backed enums as numbers in arithmetic and relational expressions.

## Declaring and using an enum

```ts
enum Direction {
  North,
  South,
  East,
  West,
}

const heading: Direction = Direction.North;
console.log(heading); // 0
```

Use the declared enum type at API boundaries. This prevents unrelated numbers or strings from being passed where a particular enum is expected.

## Backing values

A numeric enum with no initializer auto-numbers: the first member is `0`, and each later member is the previous value plus one. A numeric member may carry a constant expression — a literal, a unary sign or bitwise-not over a constant, `+`/`-`/`*`/`/`/`%` over constants, or a link to an earlier member of the same enum:

```ts
enum Color {
  Red = 1,
  Crimson = Red,
  Deep = Red + 10,
}

enum Level {
  Low = 10,
  Mid,   // 11
  High,  // 12
}
```

The checker accepts only compile-time constants; it does not execute an arbitrary function at compile time. A member whose initializer is not a constant expression is rejected, with code `CP3401`.

## String-backed members

A string-backed enum uses string literal values consistently:

```ts
enum Status {
  Ready = "ready",
  Failed = "failed",
}
```

Rules for declaration shape:

| Form | Result |
| --- | --- |
| Mixed number and string members | rejected: `CP3706` |
| A member with no initializer after a string-backed member | rejected: `CP3401` (a string-backed member has no value to inherit) |
| A member initialized with a non-constant expression | rejected: `CP3401` |

## Arithmetic and comparisons

Number-backed enums read as numbers in `+`, `-`, `*`, `/`, `%`, `**`, `&`, `|`, `^`, `<<`, `>>`, `>>>`, and in relational operators (`<`, `<=`, `>`, `>=`, `===`, `!==` and the loose forms). Any pair of number-backed enums participates — including members of two different number-backed enums. The result of arithmetic is `number`:

```ts
enum Color { Red, Green, Blue }
enum Size { Small, Large }

const total = Color.Green + Size.Large; // number
const ordered = Color.Red < Color.Blue; // true
```

A number-backed enum is assignable to `number`:

```ts
const n: number = Color.Blue; // 2
```

String-backed enums are not numbers: arithmetic and relational operators over them are rejected (`CP3411` / `CP3406`), and a string-backed enum is not assignable to `number`. Assigning a plain `number` to an enum binding is rejected (`CP3401`) — the numericness rules never flow a raw number into an enum implicitly.

Enum members compare with `===`/`!==` against each other and against the same member type. Number-backed members also compare against numbers: `Color.Red === 0` is `true`.

## Printed representation

A number-backed enum prints its underlying integer; a string-backed enum prints its member's string value:

```ts
const c: Color = Color.Green;
console.log(c);        // 1
console.log(`${c}`);   // 1
```

## Member access and diagnostics

Member lookup is checked. An unknown member produces a diagnostic with the enum's members as candidates (`CP3305`); assigning a member of one enum to another enum's binding is rejected (`CP3401`):

```ts
const c: Color = Color.Green;
// const bad = Color.Chartreuse; // CP3305: did you mean 'Green'?
// const wrong: Size = Color.Red; // CP3401
```

## When to use an enum

Use an enum when values form a closed named set and the backing representation is useful. Use a string union when a function's public contract is exactly a small set of text literals and that form is supported by the checker — a union of all-literal members of one string family spells itself as the base family at assignability sites (`"a" | "b"` behaves as `string`). Use a class or interface when each value carries fields or behavior rather than being one choice from a fixed set.

## Related pages

- [Types and inference](../01-variables-and-bindings/02-types.md)
- [Control flow and narrowing](../03-control-flow/01-conditions-and-loops.md)
- [Classes and interfaces](./02-classes-and-interfaces.md)
