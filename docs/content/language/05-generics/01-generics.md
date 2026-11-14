---
title: Generics
---
# Generics

Generics let functions, classes, interfaces, and supported type aliases preserve relationships between types. CP3 checks generic arguments and constraints during analysis, before the program is built.

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

The concrete type argument is part of the instance's static type. A class instantiation without the explicit type arguments defaults the missing ones from declared defaults, or to `undefined` when none exist (`new Box()` ≡ `new Box<undefined>()` when no argument or class default exists); excess arguments remain an error (`CP3418`).

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

Type parameters accept `extends` constraints, checked at instantiation (a violated bound is `CP3412`):

```ts
interface Shape {
  area(): number;
}

function areaOf<T extends Shape>(s: T): number {
  return s.area();
}
```

The analyzer reports a constraint error at the instantiation that violates the declared bound. Use constraints to state the operation a generic body depends on instead of relying on an unchecked dynamic property lookup. Generic parameters also accept `=` defaults: `type WithDefault<T = number> = { value: T };` fills a missing `T` with `number`; a default that mentions an earlier parameter (`<T, U extends T = T>`) is substituted accordingly. A default may appear without a constraint.

## Built-in utility types

Six utility type names are built in and resolve structurally — there is no mapped-type syntax in the language:

| Utility | Meaning |
| --- | --- |
| `Partial<T>` | Every field of the shape becomes optional |
| `Required<T>` | Every field of the shape becomes required |
| `Readonly<T>` | Every field of the shape becomes read-only; the result is a distinct but mutually assignable type |
| `Pick<T, K>` | Keeps only the fields named by the literal union `K` |
| `Omit<T, K>` | Drops the fields named by `K` |
| `Record<K, V>` | `Record<string, V>` canonicalizes to `Map<string, V>`; the closed form `Record<"a" \| "b", V>` is the object shape with exactly those fields, each of type `V` |

```ts
interface Point { x: number; y: number; }

const p: Partial<Point> = { x: 1 };
const move: Pick<Point, "x"> = { x: 3 };
const scores: Record<string, number> = new Map<string, number>();
```

Notes and limits:

- `Pick` and `Omit` require string (or integral-number) literal keys — a union of literals is accepted; `string` without a literal is not. `Record`'s open form requires a string key — `Record<number, V>` is rejected (a pointer to `Map<K, V>` is reported); its closed form (`Record<"a" | "b", V>`) accepts string or integral-number literal unions. A single literal key is not closed form and canonicalizes to `Map<string, V>`.
- `Partial`/`Required`/`Readonly` over a tuple do not add per-element optionality; `Pick`/`Omit` over a tuple or an open shape have no field set to select from.
- A user-declared `Partial` (alias, interface, or class) shadows the builtin like any other type name.
- Utility types resolve structurally at compile time; the resolved shape is what the compiler reads, so they carry no runtime metadata.

## Supported surface and current limits

Generic methods on function-local classes are rejected (`CP3707`). Not every TypeScript conditional, mapped, indexed-access, or higher-kinded type feature is available. The accepted syntax is defined by the parser, analyzer, and `tests/cases/generics` corpus.

## Related pages

- [Types and inference](../01-variables-and-bindings/02-types.md)
- [Classes and interfaces](../04-values-and-objects/02-classes-and-interfaces.md)
- [Control flow and narrowing](../03-control-flow/01-conditions-and-loops.md)
