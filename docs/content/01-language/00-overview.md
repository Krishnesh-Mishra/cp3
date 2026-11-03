---
title: Chapter 1 · The language
chapter: Chapter 1 · The language
---
# Chapter 1 · The language

This chapter describes CP3 source rules: how names are declared, how values are typed, how control flow works, and how the compiler treats functions, objects, modules, and errors. It assumes basic programming experience. Readers coming from JS/TS can start with the [migration guide](../00-welcome/03-javascript-typescript-developers.md); the pages below define CP3 behavior precisely.

## A useful reading order

1. [Bindings and declarations](01-variables-and-bindings/01-bindings.md): `const`, `let`, `var`, annotations, and initialization.
2. [Types and inference](01-variables-and-bindings/02-types.md): primitives, arrays, unions, inference, and native type vocabularies.
3. [Mutability](01-variables-and-bindings/03-mutability.md): binding reassignment versus mutation through a reference.
4. [Scope and initialization](01-variables-and-bindings/04-scope-and-initialization.md): lexical scope, hoisting, shadowing, and reads before declaration.
5. [Functions](02-functions/01-functions-and-closures.md) and [control flow](03-control-flow/01-conditions-and-loops.md).
6. [Arrays and objects](04-values-and-objects/01-arrays-and-objects.md), [classes and interfaces](04-values-and-objects/02-classes-and-interfaces.md), then [enums](04-values-and-objects/03-enums.md).
7. [Generics](05-generics/01-generics.md), [modules](06-modules-and-errors/01-modules.md), and [errors](06-modules-and-errors/02-errors.md).

## Language map

| Topic | What to learn |
| --- | --- |
| Bindings | Whether a name can be reassigned and where it is visible. |
| Types | Which values an expression or binding can hold, and which operations are accepted. |
| Functions | Parameter types, return types, callbacks, closures, and async signatures. |
| Control flow | Conditions, loops, switch forms, labels, and flow narrowing. |
| Values | Array element types, object shapes, tuples, classes, interfaces, and enums. |
| Modules | Import resolution, exports, and the boundary between a module and package. |
| Errors | Throwing, catching, `finally`, and error values. |
| Native scalar names | Project-selected Rust/C++ type vocabulary and explicitly prefixed types. |

## Static checking and inference

The analyzer uses annotations and inferred expression types to accept or reject operations before C++ generation. An annotation is part of the program's checked contract, not a comment for an editor. Local inference can reduce repetition, but it does not make a binding dynamically typed.

For example, a `let` initialized with a string remains a string binding unless its declared type explicitly permits another value:

```ts
let mode = "safe";
mode = "fast";
// mode = 3; // type mismatch
```

`cp3 check` runs the front end and reports these errors without building the native executable. See [the compilation pipeline](../04-compiler-cli/02-compilation-pipeline.md) for the boundary between static analysis and native compilation.

## TypeScript resemblance and its limits

CP3 intentionally uses familiar syntax, but it is not the complete TypeScript language. The type system is closed around supported forms. It does not silently accept every dynamic feature, index signature, declaration syntax, or third-party type definition. A feature that type-checks in TypeScript may require a rewrite or explicit supported API in CP3.

Use the compiler diagnostic and tests as evidence. If a source form is rejected, do not assume the native backend can still compile it; unsupported source never reaches code generation through the ordinary pipeline.

## Project-selected native scalar types

The `typeSystem` property in the nearest `package.json` can be `typescript`, `cpp`, or `rust`. This selects how supported unprefixed native scalar names are resolved. It does not switch the generated backend from C++ to Rust or C++ source code: CP3 still compiles through its C++ backend.

Explicit prefixed forms such as `r_...`, `c_...`, and `ts_...` select a vocabulary at a type annotation site. These are closed scalar types. Pointer, reference, array, and arbitrary user-defined native type spellings are not general CP3 reference or FFI declarations. See [Project type-system selection](07-native-types/01-project-type-system.md), [Prefixed types](07-native-types/02-prefixed-types.md), and [Native type limitations](07-native-types/04-limitations.md).

## Source types versus runtime representation

Source types express which operations are valid. They do not expose all details of the generated C++ representation. `number` is represented as a floating-point value; native integer annotations can use integer machine types with distinct arithmetic and conversion rules. Arrays and class instances use runtime representations described in Chapter 2.

Do not infer stack allocation, non-atomic reference counting, or vectorization from a source-level type. These are compiler/runtime implementation questions and are documented separately in the [runtime chapter](../02-runtime/00-overview.md) and [optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md).

## Test corpus as the executable contract

The compiler test cases are grouped by language area under `tests/cases`: for example `variables`, `types`, `functions-closures`, `control-flow`, `classes`, `generics`, `modules`, and `errors`. Positive cases pin accepted programs and output; negative cases pin diagnostics. When documenting an edge case, find the matching test and implementation before stating the rule.

## Chapter pages

- [Bindings and declarations](01-variables-and-bindings/01-bindings.md)
- [Types and inference](01-variables-and-bindings/02-types.md)
- [Functions and closures](02-functions/01-functions-and-closures.md)
- [Conditions and loops](03-control-flow/01-conditions-and-loops.md)
- [Arrays and object literals](04-values-and-objects/01-arrays-and-objects.md)
- [Classes and interfaces](04-values-and-objects/02-classes-and-interfaces.md)
- [Enums](04-values-and-objects/03-enums.md)
- [Generics](05-generics/01-generics.md)
- [Modules and exports](06-modules-and-errors/01-modules.md)
- [Errors and exceptions](06-modules-and-errors/02-errors.md)
- [Native type-system selection](07-native-types/01-project-type-system.md)
