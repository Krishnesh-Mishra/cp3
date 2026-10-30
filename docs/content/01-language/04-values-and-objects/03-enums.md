---
title: Enums
chapter: Chapter 1 · The language
---
# Enums

CP3 supports enum declarations with the backing forms accepted by the parser and analyzer. Enum members are named values; the checker prevents mixing incompatible backing kinds within one declaration.

## Declaring and using an enum

```ts
enum Direction {
  North,
  South,
  East,
  West,
}

const heading: Direction = Direction.North;
```

Use the declared enum type at API boundaries. This prevents unrelated numbers or strings from being passed where a particular enum is expected.

## Backing values

The compiler supports enum member forms with consistent string or numeric backing as implemented in the semantic checker. A declaration that mixes backing kinds is rejected. Auto-numbering and printed representation should be verified against the exact form and conformance cases rather than assumed from TypeScript.

## Type checking

Enum members have enum-associated types. The checker reports invalid member access, mixed backing, and incompatible assignments using the stable diagnostic catalog. The current test corpus contains ambiguous markers for some enum printing questions; those behaviors are not stated here as guarantees.

Use enums for a finite, named set of values. Use a string union when the API is specifically defined as a union of string literals and the type checker supports that shape.

## Numeric members and automatic values

A numeric enum can omit initializers. In that form, CP3 assigns the first value `0` and increments subsequent members:

```ts
enum Direction { North, South, East, West }
// Direction.North is 0; subsequent members increase by one.
```

A numeric member may also use a constant expression. The checker accepts supported literal and enum-member expressions; it does not execute an arbitrary function at compile time:

```ts
enum Color {
  Red = 1,
  Crimson = Red,
  Deep = Red + 10,
}
```

The `tests/cases/enums` fixtures cover omitted values, explicit arithmetic, references to earlier members, and rejection of non-constant initialization. Do not assume every C++ constant expression or TypeScript enum initializer is accepted.

## String-backed members

A string-backed enum uses string literal values consistently:

```ts
enum Status {
  Ready = "ready",
  Failed = "failed",
}
```

A string member without an explicit string initializer is rejected, and a declaration cannot mix string- and number-backed members. Use one backing kind for the whole enum.

## Compatibility and comparison

The checker preserves the declared enum type in annotated bindings and rejects assignment from a different enum. Numeric enum members have numeric backing behavior, but do not treat every raw number as a validated enum member: the checker and tests define which assignments are allowed. Compare two values using their enum members rather than depending on an undocumented display format.

Member lookup is checked. A misspelled member produces a diagnostic with candidate enum members. Runtime printing and implicit conversion behavior are separate questions; consult the specific run-mode fixtures instead of assuming TypeScript reverse maps or C++ enum formatting.

## When to use an enum

Use an enum when values form a closed named set and the backing representation is useful. Use a string union when a function's public contract is exactly a small set of text literals and that form is supported by the checker. Use a class or interface when each value carries fields or behavior rather than being one choice from a fixed set.

## Test and implementation map

`tests/cases/enums` includes declaration, member access, arithmetic initializer, assignment, equality, switch matching, and invalid backing cases. Semantic validation is in `src/sema/src/checks/type/enum_check.cpp`; code generation must stay consistent with its accepted constant expressions. When adding a rule, include a check-mode rejection case and a run-mode case for the observable backing value.
