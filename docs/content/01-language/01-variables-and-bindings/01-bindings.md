---
title: Bindings and declarations
chapter: Chapter 1 · The language
---
# Bindings and declarations

A binding gives a name to a value. CP3 has `const`, `let`, and `var` declarations. Choose the declaration form by asking whether the name may be assigned again, then use a type annotation when inference is not enough to make the boundary clear.

## `const`: a binding that is not reassigned

```ts
const greeting: string = "Hello";
const limit = 100;
```

The annotation after the name is optional when the initializer has a type the analyzer can infer. In this example, `greeting` is a string and `limit` is a number.

Assigning a different value to a `const` name is an error:

```ts
const port: number = 8080;
port = 9000; // diagnostic: const binding cannot be reassigned
```

This rule concerns the binding. If a `const` name refers to a mutable reference value, the reference may still expose operations that change the referenced object. For example, a `const` array handle can still refer to an array whose `push` method changes the array.

## `let`: a binding that can be reassigned

```ts
let count: number = 0;
count = count + 1;
```

The type is checked at each assignment. A `number` binding cannot later receive a `string` just because JavaScript would allow the operation.

```ts
let title = "Draft"; // inferred as string
title = "Published"; // valid
title = 42;           // type mismatch
```

Use `let` when the program actually assigns a new value. A value that changes inside a referenced object does not by itself require the binding to use `let`.

## `var`: legacy function-scoped binding

CP3 accepts `var` for code that depends on JS's legacy function-scoping and hoisting behavior. Prefer `let` or `const` in new code because their block-scoped rules are easier to read locally.

```ts
function example(): number {
  var total = 0;
  total = total + 2;
  return total;
}
```

`var` is not simply another spelling of `let`. Its visibility before the textual declaration follows CP3’s hoisting and initialization rules; see [Scope, hoisting, and initialization](#01-language/01-variables-and-bindings/04-scope-and-initialization).

## Annotation syntax

The binding name appears first, followed by a colon and a type:

```ts
const enabled: boolean = true;
let bytes: number[] = [1, 2, 3];
let maybeName: string | undefined = undefined;
```

Annotations are especially valuable for function parameters, public module exports, mutable variables initialized with an ambiguous expression, and empty collections whose element type cannot be inferred from values.

## Initializers and missing values

The front end may allow declarations without an initializer when the type and control flow permit it. Do not assume that every variable starts with JavaScript's implicit `undefined`; use an explicit `undefined` union or an optional parameter when that is the intended state.

```ts
let result: number | undefined = undefined;
result = 12;
```

An uninitialized binding can interact with definite assignment checks and runtime initialization. Initialize a local at its declaration when possible. This makes both the expected value and the generated flow easier to inspect.

## Destructuring declarations

CP3 supports the destructuring forms implemented by the parser and semantic analyzer. Destructuring declarations follow the same binding and type rules as ordinary names. A destructuring declaration that omits a required initializer is rejected; see the destructuring cases in `tests/cases/destructuring` for the supported patterns.

## A practical choice guide

| Situation | Use |
| --- | --- |
| The name is assigned once and should remain bound to that value | `const` |
| The name is assigned a new value later | `let` |
| Existing code intentionally relies on function scope or hoisting | `var` |
| A public signature needs to be clear or inference is ambiguous | An explicit annotation |
| The value may be absent | An explicit `undefined`/`null` union or supported optional form |

## Related diagnostics

Type mismatch errors commonly use the `CP34xx` range. Reassignment of a constant has its own diagnostic. The compiler-generated help page includes the exact message, source excerpt, suggested changes, and the three guide pages with the closest text match.
