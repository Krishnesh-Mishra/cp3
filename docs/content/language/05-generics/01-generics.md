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

Eleven utility type names are built in and resolve structurally - there is no mapped-type syntax in the language:

| Utility | Meaning |
| --- | --- |
| `Partial<T>` | Every field of the shape becomes optional |
| `Required<T>` | Every field of the shape becomes required |
| `Readonly<T>` | Every field of the shape becomes read-only; the result is a distinct but mutually assignable type |
| `Pick<T, K>` | Keeps only the fields named by the literal union `K` |
| `Omit<T, K>` | Drops the fields named by `K` |
| `Record<K, V>` | `Record<string, V>` canonicalizes to `Map<string, V>`; the closed form `Record<"a" \| "b", V>` is the object shape with exactly those fields, each of type `V` |
| `Parameters<F>` | A tuple of the parameter types of the function type `F` |
| `ReturnType<F>` | The return type of the function type `F` |
| `ConstructorParameters<C>` | A tuple of the constructor parameter types of `C` |
| `InstanceType<C>` | The instance type produced by the constructor `C` |
| `Awaited<T>` | Recursively unwraps a `Promise` chain; a non-`Promise` type passes through unchanged |

The five function and constructor queries are documented with examples in [Type-level queries](#type-level-queries) below.

```ts
interface Point { x: number; y: number; }

const p: Partial<Point> = { x: 1 };
const move: Pick<Point, "x"> = { x: 3 };
const scores: Record<string, number> = new Map<string, number>();
```

Notes and limits:

- `Pick` and `Omit` require string (or integral-number) literal keys - a union of literals is accepted; `string` without a literal is not. `Record`'s open form requires a string key - `Record<number, V>` is rejected (a pointer to `Map<K, V>` is reported); its closed form (`Record<"a" | "b", V>`) accepts string or integral-number literal unions. A single literal key is not closed form and canonicalizes to `Map<string, V>`.
- `Partial`/`Required`/`Readonly` over a tuple do not add per-element optionality; `Pick`/`Omit` over a tuple or an open shape have no field set to select from.
- A user-declared `Partial` (alias, interface, or class) shadows the builtin like any other type name; the same shadowing rule applies to every utility type in this table.
- `Parameters`, `ReturnType`, `ConstructorParameters`, `InstanceType`, and `Awaited` query function, constructor, and promise types instead of shapes; see [Type-level queries](#type-level-queries) for examples.
- Utility types resolve structurally at compile time; the resolved shape is what the compiler reads, so they carry no runtime metadata and emit no C++.

## Type-level queries

Type-level queries let you name and compute types without writing values, and `as const` lets you keep a literal's exact type. Everything in this section is resolved entirely during semantic analysis: these features emit no C++, vanish before code generation, and have zero runtime cost. The value-position operators you already know keep working exactly as before - these are type-position features only.

> **`keyof` and `T[K]` require a statically known, closed shape.** They resolve only over an `interface`, a `class`, a `type` alias, or an exact object literal - a shape whose complete set of members is known at compile time. On `object`, on `unknown`, or on a shape with an open index signature, they fail with a diagnostic in the `CP3620`-`CP3629` family, and the messages carry the word `closed`. There is no `any` fallback anywhere in these features; the compiler never guesses a member set.

### `typeof` in type position

In a type annotation, `typeof value` is a type query: it resolves to the inferred type of the expression. In a value position, `typeof x` remains the runtime operator that produces a string such as `"number"` or `"string"`. Which meaning applies is determined by where `typeof` appears; the two never overlap.

```ts
const config = { host: "localhost", port: 8080 };
type Config = typeof config;   // { host: string, port: number }

function connect(host: string, port: number): undefined {
  console.log(host, port);
}

type ConnectFn = typeof connect;   // (host: string, port: number) => undefined
```

Value position is unchanged:

```ts
if (typeof config.port === "number") {
  // runtime string comparison, as before
}
```

### `keyof T`

`keyof T` resolves to a union of string literal types naming the exact, known keys of a closed shape:

```ts
interface User {
  id: number;
  name: string;
}

type UserKey = keyof User;    // "id" | "name"
const key: UserKey = "id";    // ok
```

### `T[K]`

`T[K]` is an indexed access type. `K` must be a subtype of `keyof T`; the result is the property type, or a union of property types when `K` is a union:

```ts
interface User {
  id: number;
  name: string;
  active: boolean;
}

type UserName = User["name"];       // string
type UserValue = User[keyof User];  // number | string | boolean
```

An indexed access whose `K` is not a subtype of `keyof T` is a diagnostic, not a fallback type. Both queries follow the closed-shape rule above: `keyof object`, `keyof unknown`, `T[K]` over a shape with an open index signature, and similar cases all report a `CP3620`-`CP3629` diagnostic whose message carries the word `closed`. Interfaces, classes, type aliases, and exact object literals are closed shapes; a shape with an index signature is open.

### `as const`

`expr as const` asserts the literal type of an object or array literal: properties and elements keep their literal types instead of widening to the base primitive, and the shape becomes readonly-like.

```ts
const route = { method: "GET", path: "/health" } as const;
// method is "GET" (not string); path is "/health" (not string)
// the shape is readonly-like: assigning to route.method is a compile-time diagnostic
route.method = "POST";   // error

const tags = ["a", "b"] as const;
// a readonly-like array of the literals "a" and "b", not string[]
```

Without `as const`, a literal initializer widens to its base family (`let message = "ok";` infers `string`); with it, the asserted literal type is preserved. There is no runtime wrapper - the assertion is erased entirely, so `as const` costs nothing at run time. Combine it with a type query to name the keys of the literal shape, for example `type RouteKey = keyof typeof route;`, which resolves to `"method" | "path"`.

### Function and constructor queries

Five utility types query the signature of a function or constructor instead of a shape: `Parameters<F>`, `ReturnType<F>`, `ConstructorParameters<C>`, `InstanceType<C>`, and `Awaited<T>`. They resolve at compile time like every other utility type, and they carry no runtime metadata.

```ts
function connect(host: string, port: number): undefined {
  console.log(host, port);
}

type ConnectArgs = Parameters<typeof connect>;     // [string, number]
type ConnectResult = ReturnType<typeof connect>;   // undefined
```

```ts
class Request {
  url: string;
  retries: number;
  constructor(url: string, retries: number) {
    this.url = url;
    this.retries = retries;
  }
}

// a class named in value position stands for its constructor
type RequestArgs = ConstructorParameters<typeof Request>;  // [string, number]
type RequestShape = InstanceType<typeof Request>;          // Request
```

```ts
async function fetchLabel(id: number): Promise<string> {
  return `label-${id}`;
}

type LabelPromise = ReturnType<typeof fetchLabel>;           // Promise<string>
type LabelAwaited = Awaited<ReturnType<typeof fetchLabel>>;  // string
type DeepAwaited = Awaited<Promise<Promise<number>>>;        // number
type PassThrough = Awaited<string>;                          // string
```

- `Parameters<F>` and `ConstructorParameters<C>` produce tuple types, like `type Pair<T> = [T, T]` elsewhere in the language.
- `ReturnType<F>` resolves to the declared result type of the function type.
- `InstanceType<C>` resolves to the instance type the constructor produces.
- `Awaited<T>` unwraps `Promise<...>` recursively; a non-`Promise` type passes through unchanged.
- None of these features falls back to `any`; a shape they cannot use is rejected with a diagnostic rather than papered over.

## Supported surface and current limits

Generic methods on function-local classes are rejected (`CP3707`). Type-level queries are supported as described in [Type-level queries](#type-level-queries): type-position `typeof`, `keyof`, `T[K]`, `as const`, and the utility types in the table above. Conditional and mapped types, and higher-kinded types, are not available. The accepted syntax is defined by the parser, analyzer, and `tests/cases` corpus.

## Related pages

- [Types and inference](../01-variables-and-bindings/02-types.md)
- [Classes and interfaces](../04-values-and-objects/02-classes-and-interfaces.md)
- [Mutability and immutability](../01-variables-and-bindings/03-mutability.md)
- [Control flow and narrowing](../03-control-flow/01-conditions-and-loops.md)
