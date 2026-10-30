---
title: Generics
chapter: Chapter 1 · The language
---
# Generics

Generics let functions, classes, interfaces, and supported type aliases preserve relationships between types. CP3 checks generic arguments and constraints before generating concrete native code.

## Generic functions

```ts
function identity<T>(value: T): T {
  return value;
}

const id = identity<number>(7);
```

The type parameter `T` connects the parameter and result. The call may provide the type argument explicitly; inference is available where the implementation can determine it reliably.

## Generic classes

```ts
class Box<T> {
  value: T;
  constructor(value: T) { this.value = value; }
  get(): T { return this.value; }
}

const box = new Box<number>(5);
```

The concrete type argument is part of the instance’s static type. CP3 lowers supported generic uses into concrete generated forms. See [Generics and specialization](#04-compiler-cli/04-generics) for how that affects code size and performance.

## Generic interfaces and aliases

```ts
interface PairLike<T> {
  first: T;
  second: T;
}

type Pair<T> = [T, T];
```

Type aliases can name supported type expressions, including generic tuple, array, and union expressions. An alias does not add a wrapper object at runtime.

## Constraints

Some generic declarations use constraints to limit which types may be supplied. The analyzer reports a constraint error at the instantiation that violates the declared bound. Use constraints to state the operation a generic body depends on instead of relying on an unchecked dynamic property lookup.

## Supported surface and current limits

Generic local methods and unsupported type argument shapes have dedicated diagnostics. Not every TypeScript conditional, mapped, indexed-access, or higher-kinded type feature is available. The accepted syntax is defined by the parser, analyzer, and `tests/cases/generics` corpus.

## Performance expectations

Monomorphization can remove dynamic dispatch and make concrete operations visible to the C++ optimizer. It can also increase generated code size, and it does not automatically remove allocations or reference-count operations. Measure the workload and inspect generated C++ before attributing speed to a generic abstraction.
