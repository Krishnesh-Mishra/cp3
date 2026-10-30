---
title: Bindings and declarations
chapter: Chapter 1 · The language
---
# Bindings and declarations

A binding associates a name with a value. CP3 has `const`, `let`, and `var` declarations. Choose the declaration by asking whether the name itself will receive a different value later; changing a field or array element is a separate operation.

## `const`: a binding that stays put

```ts
const greeting: string = "Hello";
const limit = 100;
```

The type annotation after the name is optional when the initializer gives the analyzer enough information. Here, `greeting` has type `string` and `limit` has type `number`.

Reassigning a constant is an error:

```ts
const port: number = 8080;
// port = 9000; // compile-time error
```

`const` restricts the binding, not the complete object graph reachable through it. A constant array reference can still call `push` or assign an element when the array and element types permit it. See [Mutability and immutability](#01-language/01-variables-and-bindings/03-mutability).

## `let`: a reassignable binding

```ts
let count: number = 0;
count = count + 1;
```

Assignments must remain compatible with the declared or inferred type:

```ts
let title = "Draft"; // inferred as string
title = "Published"; // valid
// title = 42;         // type mismatch
```

Use `let` when a later control-flow path assigns a new value to the name. If the referenced object changes but the handle does not, `const` may still be the more accurate declaration.

## `var`: legacy function-scoped declaration

CP3 accepts `var` for code that depends on legacy function-level scope and hoisting:

```ts
function example(): number {
  var total = 0;
  total = total + 2;
  return total;
}
```

`var` does not have the block-local visibility of `let` and `const`. Its declaration is hoisted within the containing function (or module-level function context), but its initializer still executes at its source position. A read before the initializer is not a read of the later initialized value. See [Scope, hoisting, and initialization](#01-language/01-variables-and-bindings/04-scope-and-initialization).

Prefer `const` or `let` for new code unless `var` behavior is specifically required. The tests under `tests/cases/variables` pin CP3's behavior for use before declaration and reassignment.

## Annotation syntax

The binding name comes first, followed by a colon and a type:

```ts
const enabled: boolean = true;
let bytes: number[] = [1, 2, 3];
let maybeName: string | undefined = undefined;
```

Annotations are useful at interfaces and boundaries, for empty arrays, for variables without an initializer, and whenever a union or native numeric representation should be explicit.

The annotation is checked against the initializer. A mismatch is diagnosed during semantic analysis, before C++ generation:

```ts
// const attempts: number = "three"; // type mismatch
```

## Inference and later assignments

Inference saves repeated type text but does not make a binding dynamic:

```ts
let answer = 42; // number
answer = 43;     // still a number
```

If a variable intentionally holds values from two types, declare the supported union explicitly:

```ts
let result: number | undefined = undefined;
result = 12;
```

An empty collection does not reveal an element type by inspection. Annotate it:

```ts
const names: string[] = [];
```

For more about union compatibility, inference, and project-dependent native type names, see [Types and inference](#01-language/01-variables-and-bindings/02-types).

## Initializers and absent values

An accepted declaration with no initializer is initialized to CP3's `undefined` value. The inferred state is still constrained by its type; it does not make the binding dynamically typed or automatically widen it to every type you may assign later.

When absence is an intended state, represent it in the type and initialize the value:

```ts
let selected: string | undefined;
if (hasSelection()) {
  selected = "north";
}
```

This makes the possible states visible to the checker and readers. The `undefined` arm allows the initial absent state and the later string value. For `var` hoisting and destructuring declarations without an initializer, see the dedicated pages.

## Multiple declarations

When several declarations are placed together, each binding still has its own initializer and type:

```ts
const width: number = 40;
const height: number = 25;
let area = width * height;
```

Separate declarations are often easier to annotate, debug, and adjust than a long comma-separated statement. Keep expressions with side effects on their own line so evaluation order is clear.

## Destructuring declarations

Array and object patterns introduce one or more local bindings:

```ts
const [x, y] = [4, 9];
const { name: displayName } = { name: "Ada" };
```

The extracted names obey the same `const`/`let`/`var` reassignment rules as ordinary identifiers. The source shape is also checked. Read [Destructuring declarations](#01-language/01-variables-and-bindings/05-destructuring) for tuple rest, object rest, parameter forms, and errors.

## Binding lifetime and captured names

Local bindings exist within their lexical scope. Closures can capture values from an outer scope, which can extend the lifetime of captured state beyond the block that created it. Mutable captures can require a runtime capture cell. This affects representation but not the source-level rule that a `const` binding cannot be reassigned.

See [Functions and closures](#01-language/02-functions/01-functions-and-closures) and [Closure storage and lifetime](#02-runtime/04-closures) before using a closure to retain large objects or shared mutable state.

## Practical choice table

| Situation | Declaration |
| --- | --- |
| Name is assigned once and should stay associated with that value | `const` |
| Name is assigned a new compatible value later | `let` |
| Existing code deliberately relies on function-scope hoisting | `var` |
| An empty array needs a known element type | Add a type annotation, commonly with `const` |
| A value may be absent | Use an explicit `undefined` or `null` union |
| A local name comes from an object/tuple pattern | Use destructuring with the appropriate declaration keyword |

## Compiler feedback

The analyzer checks initializer compatibility, assignments, redeclarations, and reads against declarations. Common failures include:

- assigning a different type to an inferred `let`;
- reassigning a `const` name;
- redeclaring a lexical binding in the same scope;
- reading a `let` or `const` before its declaration;
- leaving a value in an unsupported uninitialized state;
- destructuring a source without the requested property or shape.

The exact diagnostic code is emitted by the compiler. The per-error help page ranks related guide sections, but the code and message remain the authoritative explanation for that build.

## Related pages and tests

- [Types and inference](#01-language/01-variables-and-bindings/02-types)
- [Mutability and immutability](#01-language/01-variables-and-bindings/03-mutability)
- [Scope and initialization](#01-language/01-variables-and-bindings/04-scope-and-initialization)
- [Destructuring](#01-language/01-variables-and-bindings/05-destructuring)
- Test family: `tests/cases/variables` and `tests/cases/destructuring`
