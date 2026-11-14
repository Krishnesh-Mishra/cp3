---
title: Mutability and immutability
chapter: Chapter 1 · The language
---
# Mutability and immutability

When reading a CP3 program, separate two questions:

1. May this **binding** be assigned a different value?
2. May the **value** currently referenced by the binding be changed?

`const` and `let` control reassignment of the binding. The type and operations of the value control what can be mutated through it. A `const` reference does not make an array, object, or class instance deeply immutable.

## Binding mutability: `const` and `let`

Use `const` when the name should stay bound to its initialized value:

```ts
const serviceName: string = "indexer";
const retryLimit = 4;
```

This is rejected because it assigns a new value to the binding:

```ts
const retries = 0;
retries = 1; // compile-time error
```

Use `let` when the binding itself must receive a new value:

```ts
let retries = 0;
retries = retries + 1;
```

The reassigned value must still satisfy the binding's declared or inferred type. `let title = "draft"` does not make `title` dynamically typed; assigning a number is a type error.

## A constant reference can refer to mutable data

This is valid because the array handle remains the same while the array's contents change:

```ts
const readings: number[] = [10, 20];
readings.push(30);
readings[0] = 11;
```

The following is a different operation and is rejected because it rebinds the name:

```ts
const readings: number[] = [10, 20];
// readings = [30, 40]; // const binding cannot be reassigned
```

The same distinction applies to class references and object values. A constant name prevents replacing its reference; it does not freeze every field or method reachable through the value.

## Mutability is not deep immutability

CP3's `const` is not a recursive freeze operation. Given a reference to a mutable value, code may call a mutating method or assign to a writable member if the type and access rules permit it. For example, these actions have different targets:

```ts
const config = { retries: 2 };
config.retries = 3;       // changes a member of the referenced object
// config = { retries: 4 }; // rebinds the constant name
```

Whether a particular object member can be assigned also depends on the object's structural type, class declaration, access modifiers, and whether the property itself is declared writable. `const` alone does not answer that question.

## Captures preserve the same rule

A closure that captures a binding can read it later. Capturing a `const` does not permit reassignment through the closure:

```ts
const threshold = 12;
const isLarge = (value: number): boolean => value > threshold;
```

A closure capturing a mutable `let` observes the binding's current value according to the capture semantics implemented by CP3. The compiler may need a shared capture cell for mutable captures, which can affect allocation and lifetime. See [Functions and closures](../02-functions/01-functions-and-closures.md) and [Closure storage and lifetime](../../02-runtime/04-closures.md).

```ts
let total = 0;
const add = (value: number): undefined => {
  total = total + value;
};
add(5);
```

Use a clear owner for mutable state. Capturing a name does not make the state thread-safe; the [thread-safety guide](../../02-runtime/10-thread-safety.md) explains the limits of shared state across worker or parallel execution.

## Fields and array elements

There are three separate assignment cases:

| Source operation | What changes | Controlled by |
| --- | --- | --- |
| `name = other` | The binding's value/reference | `const` versus `let`/`var` |
| `record.count = 4` | A field on the referenced object | The field's declared type and access rules |
| `items[index] = value` | An element in the referenced array | The element type and array/index semantics |

The array example below mutates an element without rebinding the `const` handle:

```ts
const scores: number[] = [7, 8];
scores[1] = 9;
```

Typed-array elements add numeric conversion rules and out-of-range behavior. Do not transfer ordinary-array assumptions to typed arrays; see [Typed arrays and DataView](../../03-standard-library/16-typed-arrays.md).

## Choosing a declaration

| Need | Declaration |
| --- | --- |
| The name is initialized once and never replaced | `const` |
| The name is reassigned later | `let` |
| Existing code relies on legacy function-scoped hoisting | `var` |
| A referenced array or object changes, but its handle does not | Usually `const` |
| A value may be absent and later set | An explicit optional/union type and `let` |

Prefer the narrowest binding mutability that matches the code. It helps both the analyzer and reviewers see which operations are intended.

## Common misconceptions

- **“`const` means immutable object.”** It means the binding cannot be reassigned.
- **“`let` makes a value dynamically typed.”** The value is still checked against the inferred or annotated type.
- **“A captured value is copied.”** Capture behavior depends on the value shape and mutability; inspect the closure rules.
- **“A `const` array cannot change.”** Methods such as `push` mutate the array contents without replacing the handle.
- **“Immutability automatically makes sharing thread-safe.”** It does not establish ownership, synchronization, or safe access to mutable children.

## What the tests pin

The variable cases cover rejected reassignment of constants, permitted array and object-member mutation through a constant handle, reassignment of `let`, type mismatch on reassignment, and captured constant assignment. These checks live under `tests/cases/variables`; use them to distinguish binding rules from deeper object mutability.

## Related pages

- [Bindings and declarations](./01-bindings.md)
- [Types and inference](./02-types.md)
- [Scope and initialization](./04-scope-and-initialization.md)
- [Escape analysis](../../02-runtime/02-escape-analysis.md)
