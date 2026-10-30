---
title: Scope, hoisting, and initialization
chapter: Chapter 1 · The language
---
# Scope, hoisting, and initialization

Scope determines where a binding can be named. Initialization determines when it has a usable value. These are separate rules; do not assume that a name being visible means it is safe to read before its declaration executes.

## Block scope

`let` and `const` declarations are scoped to their enclosing block or function. An inner block can declare a local name without making it visible after the block:

```ts
function example(): number {
  let result = 1;
  {
    const temporary = 2;
    result = result + temporary;
  }
  return result;
}
```

The inner name `temporary` is not available after its block closes. A nested function may capture names from an outer scope; see the closure page for the lifetime implications.

## `var` scope

`var` follows the compiler’s legacy function-scoping rules, rather than the block scoping of `let` and `const`. A `var` declared inside a nested block is associated with the surrounding function scope. Use it only when compatibility with existing source requires that behavior.

## Declaration before use

For `let` and `const`, reading a binding before its declaration is rejected. This prevents a source-order mistake from being silently interpreted as a value. Keep initialization close to declaration and avoid relying on textual hoisting.

```ts
function example(): number {
  // return amount; // rejected before the declaration
  let amount = 4;
  return amount;
}
```

CP3 also has tests for `var` hoisting. Hoisting does not mean the variable has already received its initializer. A read before the initializer can observe the language’s uninitialized/undefined behavior or be rejected according to the exact form. The test corpus is the compatibility contract for edge cases.

## Shadowing

A nested scope can shadow an outer binding when the semantic rules permit it. Shadowing is legal in supported cases, but it can make review harder. Prefer a distinct name when both values are used in the same function.

```ts
const value = 1;
function inner(): number {
  const value = 2;
  return value;
}
```

The `value` returned from `inner` is the inner binding. It does not reassign the outer constant.

## Definite initialization

Initialize values along every control-flow path before reading them. An explicit optional type is often the clearest representation when a value may not yet exist:

```ts
let answer: number | undefined = undefined;
if (ready()) {
  answer = 42;
}
```

The `undefined` arm is part of the type and must be handled where required. Do not use an uninitialized local as a substitute for an explicit state.

## Module-level declarations

Module declarations have module scope. Exported values form part of the module interface and have longer lifetimes than local temporaries. That lifetime can change whether an optimization is safe: the compiler must not replace an exported object with a frame-local object.

## Related tests

The conformance cases under `tests/cases/variables` cover `let`, `var`, reassignment, shadowing, use before declaration, and primitive/reference behavior. If observed behavior and prose disagree, fix the prose or the implementation together; do not rely on assumptions from Node.
