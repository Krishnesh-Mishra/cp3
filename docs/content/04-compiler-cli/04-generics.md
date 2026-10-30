---
title: Generics and specialization
chapter: Chapter 4 · Compiler and tools
---
# Generics and specialization

CP3 supports generic functions, classes, interfaces, and type aliases where the parser and analyzer accept type parameters. Type arguments are checked at compile time; generic code is not a dynamically typed escape hatch.

```ts
function identity<T>(value: T): T {
  return value;
}

const label: string = identity<string>("ready");
```

## Inference and explicit type arguments

The analyzer can infer type arguments from call arguments in supported contexts. Add explicit arguments when inference is ambiguous or when the public API reads more clearly with them. Generic constraints and unsupported inference forms are diagnosed during semantic analysis.

## Native lowering

Generic instantiations are lowered through the compiler's C++ generation strategy. The generated source may contain specialized template or helper forms depending on the construct. This is an implementation detail, not a guarantee that every generic has zero runtime cost.

## Performance interpretation

Generic code can still allocate, copy references, or call runtime helpers. Specialization alone does not prove escape analysis, stack allocation, devirtualization, or SIMD. Inspect generated C++ and benchmark the target operation.

## Compatibility boundary

CP3's generic syntax resembles TypeScript in common cases, but the accepted grammar and semantic rules are those of the CP3 compiler. A TypeScript construct that is not implemented should be expected to produce a CP3 diagnostic rather than pass through to C++.

## Type argument resolution

A generic declaration introduces type parameters that are substituted when the compiler checks a use. Calls can provide explicit type arguments or rely on inference from arguments in supported cases:

```ts
function identity<T>(value: T): T {
  return value;
}

const name = identity<string>("Ada");
const count = identity(3);
```

Use an explicit argument when inference is ambiguous or when it makes a public boundary easier to read. An explicit argument still has to satisfy the declaration's supported constraints and argument types.

## Supported declarations

CP3 supports generic functions, classes, interfaces, and type aliases where the parser and checker implement the form. Generic parameters flow into parameter types, result types, member signatures, and collection element types. Inference behavior is tied to those supported type positions; it is not the complete TypeScript inference algorithm.

Unsupported TypeScript features include arbitrary conditional, mapped, indexed-access, and higher-kinded type operations. A familiar angle-bracket syntax does not establish support for every TypeScript type expression.

## Generic classes and collections

Provide type arguments for generic classes whose empty constructor gives no evidence about their element types:

```ts
const lookup = new Map<string, number>();
const names = new Set<string>();
```

The compiler checks method calls against the declared arguments. A missing or incompatible type argument can fail before code generation. See [Collections](#03-standard-library/12-collections) for the generic Map/Set API.

## Generated-code cost

The compiler lowers generic uses into concrete generated forms according to its implementation strategy. Do not assume every use creates a distinct native type, that every generic is monomorphized exactly like Rust, or that generics are always zero-cost. Generated templates, helper calls, reference copies, and allocations can still occur.

For a performance claim, inspect the generated C++ for the instantiation and profile the executed path. Specialization can improve type knowledge while leaving allocation or call overhead unchanged.

## Generic design guidance

Keep constraints narrow and express them in parameter/result types the checker understands. Prefer one useful generic interface over a conditional-type metaprogramming pattern CP3 does not implement. Test inference, explicit types, invalid substitutions, empty collections, and imported generic functions.

## Tests and implementation

Generic call checking lives under `src/sema/src/checks/call/generic_check.cpp`; type substitution and codegen helpers have separate owners. Tests appear under `tests/cases/generics` and `tests/cases/modules` for generic imports. When changing generic behavior, cover both front-end acceptance and run-mode behavior for runtime instantiations.
