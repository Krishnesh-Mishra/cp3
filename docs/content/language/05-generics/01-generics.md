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

The concrete type argument is part of the instance’s static type. CP3 lowers supported generic uses into concrete generated forms. See [Generics and specialization](../../cli-and-compiler/04-generics.md) for how that affects code size and performance.

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

## Built-in utility types

Six utility type names are built in and resolve structurally — there is no mapped-type syntax or runtime component:

| Utility | Meaning |
| --- | --- |
| `Partial<T>` | Every field of the shape becomes optional |
| `Required<T>` | Every field of the shape becomes required |
| `Readonly<T>` | Every field of the shape becomes read-only; the result is a distinct but mutually assignable type |
| `Pick<T, K>` | Keeps only the fields named by the string (or number) literal union `K` |
| `Omit<T, K>` | Drops the fields named by `K` |
| `Record<K, V>` | `Record<string, V>` canonicalizes to `Map<string, V>`; the closed form `Record<"a" \| "b", V>` is the object shape with exactly those fields, each of type `V` |

```ts
interface Point { x: number; y: number; }

const p: Partial<Point> = { x: 1 };
const move: Pick<Point, "x"> = { x: 3 };
const scores: Record<string, number> = new Map<string, number>();
```

Notes and limits:

- `Pick` and `Omit` require string (or integral-number) literal keys — a union of literals is accepted; `string` without a literal is not. `Record`'s open form requires a string key — `Record<number, V>` is rejected with a pointer to `Map<K, V>`; its closed form (`Record<"a" | "b", V>`) accepts string or integral-number literal unions. A single literal key is not closed form and canonicalizes to `Map<string, V>`.
- `Partial`/`Required`/`Readonly` over a tuple do not add per-element optionality; `Pick`/`Omit` over a tuple or an open shape have no field set to select from.
- A user-declared `Partial` (alias, interface, or class) shadows the builtin like any other type name.
- Utility types resolve structurally at compile time; the resolved shape is what the compiler emits, so they carry no runtime cost beyond the shape they produce.

## Supported surface and current limits

Generic local methods and unsupported type argument shapes have dedicated diagnostics. Not every TypeScript conditional, mapped, indexed-access, or higher-kinded type feature is available. The accepted syntax is defined by the parser, analyzer, and `tests/cases/generics` corpus.

## Performance expectations

Monomorphization can remove dynamic dispatch and make concrete operations visible to the C++ optimizer. It can also increase generated code size, and it does not automatically remove allocations or reference-count operations. Measure the workload and inspect generated C++ before attributing speed to a generic abstraction.
