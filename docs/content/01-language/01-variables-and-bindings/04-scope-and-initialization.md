---
title: Scope, hoisting, and initialization
chapter: Chapter 1 · The language
---
# Scope, hoisting, and initialization

Scope answers where a binding can be named. Initialization answers what value it has at a particular point in execution. They are separate rules: a `var` name can be visible before its source declaration while still holding `undefined` because its initializer has not run.

## Block scope for `let` and `const`

`let` and `const` belong to the block in which they are declared. A nested block can use a local name without making it available after the block:

```ts
function calculate(): number {
  let result = 1;
  {
    const temporary = 2;
    result = result + temporary;
  }
  return result;
}
```

The name `temporary` is not visible after its closing block. A `for` loop's `let` initializer also has loop scope; a test confirms that its counter does not leak after the loop.

## Read before declaration: lexical bindings

CP3 diagnoses a read of a `let` or `const` before its declaration. The source name is not available as a usable initialized binding before that declaration:

```ts
console.log(value); // compile-time diagnostic
let value = 3;
```

Keep declarations before their uses and initialize a value near its declaration. This catches source-order mistakes during `cp3 check` rather than silently looking up some later initializer.

## `var` function scope

`var` is hoisted to the enclosing function scope (or the module's outer scope). Its declaration is discovered before normal statement checking, including when it is nested inside a block or loop. Hoisting does not move the initializer.

```ts
console.log(value); // prints "undefined"
var value = 5;
```

The compiler predeclares the binding with type `undefined`. When execution reaches the declaration, the initializer assigns the value. Before that point the variable has not become `5`.

Hoisting scans nested blocks, condition branches, loops, `try`/`catch`/`finally`, and switch bodies, but does not descend into a nested function. A `var` inside a function belongs to that function, not the function that created it.

## Missing initializer

For a `let` declaration with no initializer, CP3 initializes the local to `undefined`:

```ts
let value;
console.log(value); // undefined
```

If the variable will later hold a different type, annotate the complete state explicitly:

```ts
let selected: number | undefined;
if (hasSelection()) {
  selected = 12;
}
```

The `undefined` arm is part of the type and remains relevant when reading `selected`. A declaration without an initializer is not a way to opt out of type checking.

## Shadowing

A nested lexical scope can declare a name that shadows an outer name in supported cases:

```ts
const label = "outer";
function show(): string {
  const label = "inner";
  return label;
}
```

Inside `show`, `label` refers to the inner binding. The outer constant remains unchanged. Shadowing is useful for short scopes but can obscure which value a closure or later statement sees; distinct names are often clearer.

Redeclaring `let` or `const` in the same scope is rejected. A nested block is a separate scope, so a local shadow is not the same as a same-scope redeclaration.

## Function and module scope

Function bodies introduce their own local scope. Nested functions do not inherit the caller's local scope merely because they are called from inside it; they capture names from the lexical scope where they were defined.

Module-level bindings belong to the module. An exported binding can be observed by importers and therefore has a wider lifetime than an ordinary local. The module's names and exports are described in [Modules and exports](#01-language/06-modules-and-errors/01-modules).

## Scope does not imply storage duration

Lexical scope describes name visibility in CP3 source. It does not guarantee a particular C++ storage duration. A local reference can point to heap storage, a closure can extend the lifetime of a captured value, and compiler escape analysis may choose local storage only for patterns it proves safe.

Do not infer “stack allocated” from “declared inside a function.” See [Escape analysis](#02-runtime/02-escape-analysis) for the current proof limits and generated-code verification steps.

## Shadowing and closure examples

```ts
function makeCounter(): () => number {
  let count = 0;
  return () => {
    count = count + 1;
    return count;
  };
}
```

The returned closure outlives the call that created it, so it must retain its captured `count` state. A local block ending does not end a captured binding's lifetime while a closure that uses it remains reachable.

This is one reason scope and ownership are related but not identical topics. Source scope controls lookup; closure storage controls lifetime. Read [Functions and closures](#01-language/02-functions/01-functions-and-closures) and [Closure storage and lifetime](#02-runtime/04-closures).

## Common mistakes

- **Assuming `var`'s initializer is hoisted.** Only the binding is predeclared; the assignment happens at the declaration.
- **Assuming a `let` loop counter leaks.** The loop binding is scoped to the loop.
- **Using a no-initializer declaration for a dynamically typed state.** The type remains checked; annotate a union when multiple states are intended.
- **Treating a returned closure's capture as a short-lived local.** The closure can keep it alive after the outer function returns.
- **Treating source block exit as proof of stack allocation.** Code generation must prove that no use escapes.

## Tests and related pages

The `tests/cases/variables` cases cover block scope, loop-scope non-leakage, same-scope redeclaration, lexical read-before-declaration, `var` hoisting, uninitialized locals, and shadowing. `tests/cases/functions-closures` covers captured values and lifetime behavior.

- [Bindings and declarations](#01-language/01-variables-and-bindings/01-bindings)
- [Mutability](#01-language/01-variables-and-bindings/03-mutability)
- [Destructuring declarations](#01-language/01-variables-and-bindings/05-destructuring)
- [Closure storage](#02-runtime/04-closures)
