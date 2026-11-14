---
title: Types and inference
---
# Types and inference

CP3 checks values before it generates the native program. A type describes which values a binding can hold and which operations are valid. The analyzer uses explicit annotations and inferred types together; inference saves repetition but does not make the program dynamically typed.

## Common value types

| Type | Meaning | Example |
| --- | --- | --- |
| `number` | CP3's general numeric value; represented as a 64-bit floating-point number | `let price: number = 12.5;` |
| `string` | Text | `const name: string = "Mira";` |
| `boolean` | `true` or `false` | `let ready: boolean = false;` |
| `undefined` | The absent-value sentinel | `let x: number \| undefined = undefined;` |
| `null` | A separate null value, distinct from `undefined` | `let x: string \| null = null;` |
| `object` | An untyped value: it stores whatever the initializer holds, but has no members | `let data: object = response;` |

The distinction between `null` and `undefined` is intentional. A union such as `number | undefined` does not accept `null`, and a union such as `number | null` does not accept `undefined`.

## Inference

The analyzer infers a local type from an initializer when the result is unambiguous:

```ts
let count = 4;       // number
let message = "ok";  // string
let active = true;   // boolean
```

The type remains checked on reassignment. Inference does not mean the binding changes type as different values are assigned.

Annotate when the initializer is absent, when an empty collection gives no element type, when a boundary is easier to understand with a signature, or when inference would hide an important union:

```ts
let values: number[] = [];
let selected: string | undefined = undefined;
function findName(id: number): string | undefined { ... }
```

Under `"typeSystem": "cpp"` or `"rust"`, an un-annotated binding initialized with a plain integer literal infers a native integer instead of `number`; see [Un-annotated literal bindings](#un-annotated-literal-bindings-take-the-native-default) below.

## Arrays and function types

An array type writes the element type followed by `[]`; `Array<T>` is the equivalent generic spelling:

```ts
const ids: number[] = [3, 5, 8];
const labels: Array<string> = ["north", "south"];
```

Function types describe parameter types and a result type. They can appear in an interface or object shape:

```ts
interface Formatter {
  format(value: number): string;
}
```

CP3 also supports named type aliases, including generic aliases and tuple forms:

```ts
type UserId = number;
type MaybeName = string | undefined;
type Pair<T> = [T, T];
```

An alias gives a name to a type expression; it does not create a new runtime wrapper value.

## Tuples

A tuple describes a fixed sequence of element types. It can appear anywhere an array type can, including as a return type:

```ts
type Point = [number, number];
const origin: Point = [0, 0];
const [x, y] = origin;
```

Tuples are positional; element access and destructuring are checked against each position's type. A general array has one element type rather than one per position. Spreading a general array into a fixed-arity call is accepted (`add(...xs)` for `number[]` into two `number` parameters).

## Nullable and union types

Use `|` to describe a value that may have more than one supported type:

```ts
let id: number | undefined = undefined;
let response: string | null = null;
```

Optional parameters use `?` after the parameter name. A missing optional argument is represented by `undefined`, not by an arbitrary default value:

```ts
function greet(name?: string): string {
  return name ?? "guest";
}
```

Optional chaining and nullish coalescing are available for supported nullable receivers:

```ts
const length = maybeText?.length ?? 0;
```

`??` handles nullish values (`null` and `undefined`); it does not replace every false-like value as `||` does. Chains such as `a ?? b ?? 42` are accepted. Loose `==` against a `null`/`undefined` literal matches only the same flavor (`null == undefined` is `false`), and the compiler flags the pattern with a warning (`CP3520`); use `===` for a literal check.

## Literal types and enums

Literal values can participate in type checking. A union of all-literal members of one string family spells itself as the base family at assignability sites (`"a" | "b"` behaves as `string`); number- and boolean-literal unions behave as their base families as well (`1 | 2` is assignable to/from `number`). CP3 also supports enum declarations with their backing-value rules; see the [enums page](../04-values-and-objects/03-enums.md) for the implemented syntax.

## `any`, `void`, and `unknown` are retired

There is no catch-all type in CP3, and the three spellings TypeScript developers expect are not available as annotations:

- `any` is rejected with diagnostic `CP3806`; use a concrete type, or `object` for an untyped value.
- `void` as an annotation spelling is rejected with `CP3420`; annotate returns as `undefined` (`function log(): undefined { ... }`). `Promise<undefined>` is the spelling for a promise that produces no useful value.
- `unknown` as an annotation spelling is rejected with `CP3421`; use a concrete type, or `object`.

An `object`-annotated binding stores whatever its initializer holds (a record, an object literal, a class instance). Member access on an `object` value is rejected (`CP3401`): `object` has no members, as in TypeScript. An `object` binding without an initializer is also rejected (`CP3401`). Where an API needs a validated value, supply a generic type argument such as `JSON.parse<Config>(text)`; the parse itself is checked by the API, not by bare typing.

## Type errors are compile-time errors

The analyzer checks assignments, call arity, argument types, member access, conditions, and return values. A diagnostic reports the source location and a stable code. Fix the first type error in a chain first: one bad inference can cause several later messages.

## `number`

`number` is CP3's general numeric type. It is a 64-bit floating-point value, similar in broad representation to JavaScript's `number`. It is useful for measurements, ratios, ordinary counters, and math-library operations. It is not an arbitrary-precision integer.

A floating-point number has finite precision. If exact integer values above the safe range matter, use a native integer type from the [native types chapter](../07-native-types/00-overview.md) and check its bounds. Do not convert a large exact integer through `number` and expect all low bits to survive.

The numeric literal syntax includes integer, hexadecimal (`0x`), binary (`0b`), and octal (`0o`) forms, with optional `_` separators; the type checker treats ordinary numeric expressions as `number` unless a supported native type context applies. Integer division of two ordinary `number` values remains numeric division; native integer division is a distinct operation.

```ts
const taxRate: number = 0.08;
const total = 125 * (1 + taxRate);
const flags = 0b0000_0101;
const mask = 0xff_00;
```

## `string`

`string` represents CP3 text. String literals use quotes or template literals. Concatenation and interpolation create string values; standard string operations are described in [Strings](../04-values-and-objects/05-strings.md).

A template literal interpolates expressions with `${...}` and produces a `string`. Each interpolated expression is stringified with the runtime formatter. There are no tagged templates and no template-literal types — an interpolation is not usable as a string-literal type annotation:

```ts
const label: string = "report";
const message = `generated: ${label}`;
```

CP3 stores string data as UTF-8. Its `length` is byte-oriented rather than JavaScript's UTF-16 code-unit count, while character-oriented methods have their own specified behavior. Do not port JavaScript code that assumes `text.length` counts UTF-16 code units without checking the [string guide](../04-values-and-objects/05-strings.md).

## `boolean`

`boolean` contains `true` or `false`. Conditions support truthy semantics with JavaScript-like rules (`0`, `NaN`, `-0`, `""`, `null`, and `undefined` are falsy; objects are truthy), and `if (x)` / `if (!x)` narrow `x` at the branch. See [Conditions and loops](../03-control-flow/01-conditions-and-loops.md).

```ts
let connected: boolean = false;
if (connected) {
  // ...
}
```

## `undefined` and `null`

`undefined` represents an absent value, commonly a missing optional argument or an optional result. `null` is a separate explicit null value. CP3 does not merge them into one nullish type; a variable annotated as `string | undefined` does not accept `null`.

Use the union that matches the API contract:

```ts
let absent: number | undefined = undefined;
let explicitlyCleared: string | null = null;
```

Optional parameters use `?` and can be omitted by the caller. In the function body, the parameter can be undefined. `??` provides a default for nullish values, whereas `||` follows boolean/falsey behavior only where the operator and operand types allow it.

```ts
function displayName(name?: string): string {
  return name ?? "guest";
}
```

Do not mark a value optional simply to avoid fixing a missing initialization. Optionality should represent a real state in the program.

## A result with no useful value

Annotate a function that produces no useful value as `undefined`:

```ts
function announce(message: string): undefined {
  console.log(message);
}
```

A bare `return;` is accepted in such a function. A promise that produces no useful value is spelled `Promise<undefined>`. The `void` keyword is not part of the annotation surface; writing it is `CP3420`.

## Arrays and element types

An array type is written as `ElementType[]` or `Array<ElementType>`. Every item is checked against that element type. For example, `number[]` cannot be assigned a string element. An empty array needs an annotation because there is no element from which to infer a type.

```ts
const scores: number[] = [10, 12, 15];
const names: string[] = [];
names.push("Asha");
```

Arrays are mutable reference-shaped values. `const` prevents rebinding the array variable; it does not freeze the array contents. Two bindings can refer to the same array, so a mutation through either binding is visible through the other. An annotation may carry the `readonly` qualifier — `readonly number[]` or `readonly [number, number]` — which accepts only an array or tuple type. It is a compile-time read marker only: there is no runtime representation difference and CP3 does not track per-array mutability through the type. See [Arrays and objects](../04-values-and-objects/01-arrays-and-objects.md) and [Mutability](./03-mutability.md).

`Array<number>(n)` constructs a length-form array. A bare `new Array(n)` requires a `.fill(...)` chain or an annotation that pins the element type; an un-annotated length-form binding is rejected with `CP3401`. Only value-like element types (primitives, nullish/union types of those) can be default-initialized — reference-shaped element types must be filled.

## Object shapes and named interfaces

Object literal types can describe a fixed set of known properties, and interfaces and type aliases give those shapes names so they can be reused in parameters, return values, and class contracts. Interfaces and object type literals also accept index signatures — `[name: string]: T` and `[n: number]: T`:

```ts
interface Headers {
  [name: string]: string;
}
```

Member access through an index signature resolves to the index value type, and an object literal missing declared properties but carrying an index signature accepts extra properties. CP3 does not offer index signatures on arbitrary classes or arbitrary dynamic records; model a key/value dictionary with the supported `Map<K, V>` collection when keys are fully dynamic.

```ts
interface UserSummary {
  id: number;
  name: string;
}

function label(user: UserSummary): string {
  return `${user.id}: ${user.name}`;
}
```

A shape check does not mean the runtime object is deeply immutable. Mutability and reference behavior depend on the declaration and value kind, not just on the interface name.

## Function types

A function signature describes parameters and the result. Parameters and return values are checked at call sites and return statements. Optional parameters, rest parameters, generics, and async functions each have additional rules; use [Functions and closures](../02-functions/01-functions-and-closures.md) for those details.

A function value can be passed or stored where its function type is accepted. Closures can capture surrounding bindings; that affects lifetime, not the source-level rules for rebinding a captured name.

## Type aliases and generic aliases

A type alias names an existing type expression. It does not create a new runtime object or nominally distinct wrapper type:

```ts
type UserId = number;
type MaybeText = string | undefined;
```

Generic aliases can express reusable shapes where supported. Type parameters accept `extends` constraints and `=` defaults:

```ts
type Pair<T> = [T, T];
type WithDefault<T = number> = { value: T };
function identity<T extends { id: number }>(value: T): T { return value; }
```

The compiler checks substitutions at use sites (`CP3412` on a violated constraint), and fills missing type arguments from their declared defaults. A generic alias does not make conditional, mapped, or indexed-access type operators available. See [Generics](../05-generics/01-generics.md).

## Enums

Enums describe a closed named set of values. Number-backed enums read as numbers in arithmetic and relational operators, and a number-backed enum is assignable to `number`; string-backed enums keep the rejections. See [Enums](../04-values-and-objects/03-enums.md) for the declaration syntax, arithmetic behavior, and compatibility rules.

## Classes and interfaces

Classes describe runtime objects and methods; interfaces describe a checked shape or contract. They are different tools: a class can have construction and runtime identity, while an interface is a type-level contract. See [Classes and interfaces](../04-values-and-objects/02-classes-and-interfaces.md) for constructors, inheritance/implementation rules, methods, and limitations.

## Type narrowing

A value declared as a union may be used more precisely after a supported condition establishes which alternative is present. The supported guards include:

- `typeof x === "number"` / `"string"` / `"boolean"` (and the `!=` forms) for scalar unions;
- `x === undefined` / `x !== undefined` / `x === null` / `x !== null`;
- `if (x)` / `if (!x)` for nullish unions and truthy subsets;
- assignment narrowing: after `v = 4`, a later read of `v` is `number`;
- member, `this.x`, and nested-chain receivers (`if (node.next !== null) { node.next.value }`);
- `bus.isValidEventName(name)` for a typed `EventEmitter` (see [Global builtins](../99-global-builtins.md)).

The two arms of an `if` are merged like TypeScript merges them: after the `if`, the compiler sees the union of what the arms concluded, and an arm that returns, throws, breaks, or continues never reaches the merge point. An `if (!x)` guard narrows `x` to the truthy/falsy subset of its type.

CP3 does not implement literal-tag discriminated-union narrowing (there is no implicit `x.kind === "a"` partition). A union of all-literal members of one string family spells itself as the base family at assignability sites. Check [control flow](../03-control-flow/01-conditions-and-loops.md) and the test corpus when relying on a specific guard.

## Un-annotated literal bindings take the native default

Under `cpp` or `rust`, an un-annotated binding whose initializer is a **plain integer literal** (decimal, `0x`/`0b`/`0o`, `_` separators, optional sign — `const port = 8080;`, `const a = -5;`) infers the native integer default instead of `number`:

- fits `i32` → **`i32`** (C++ `int` and Rust `i32` share the canonical id);
- does not fit `i32` but fits `i64` → **`i64`** (`long long` / `i64`);
- exceeds `i64` but fits `u64` → **`u64`** (`unsigned long long` / `u64`);
- exceeds `u64` → keeps **`number`**.

Fractional and exponent spellings (`2.5`, `2.0`, `1e3`) keep `number` — the float default is `number` here. TypeScript projects keep `number` for every literal, as before.

The default applies to the **binding**, not to every expression: array elements and object properties keep `number`, and a ternary/`if` join that contains `number` absorbs the literal into `number` (`parseInt` is `number`, so `const port = cond ? parseInt(s) : 8080` is `number` too — call `parseTo<u16>(port)` at the boundary when you need the narrow width). Negative literals (`const a = -5;`) are included, because a negated integer literal keeps its literal type until the binding widens it.

`parseInt`/`parseFloat` stay **legacy JS-compatibility globals**: they always return cp3 `number`, in every `typeSystem`. They are not wired into the native defaults; use `parseTo<T>` (and explicit native spellings) where exact widths matter.

## Native interoperability types

The language also includes prefixed native type forms used by C++ and Rust interop. They are a separate, explicit facility, not ordinary portable CP3 types. See the [native types chapter](../07-native-types/00-overview.md) for the supported names, numeric rules, and limitations, and [Native interop](../../cli-and-compiler/07-native-interop.md) before using them in an application API.

A scalar annotation can express the checker's native numeric type. It does not by itself import a header, resolve a symbol, define a calling convention, transfer ownership, or prove that a target platform uses the same width for `long` or `size_t`. Those concerns belong to the interop boundary.

## Quick selection table

| Need | Prefer |
| --- | --- |
| General application arithmetic | `number` |
| Preserve exact values above the usual floating-point integer range | Supported `i64`/`u64` spelling |
| Match a Rust-facing scalar declaration | Rust mode or `r_'...'` |
| Match a C++ scalar declaration | C++ mode or `c_'...'` |
| Keep a regular CP3 type explicit in a native mode | `ts_'number'`, `ts_'string'`, etc. |
| Describe an optional native scalar | `nativeType \| undefined` or `nativeType \| null` |
| Describe a pointer, borrow, or array declarator | Not a prefixed value type; use a supported interop boundary |

## How to resolve common type errors

| Diagnostic situation | Typical cause | What to inspect |
| --- | --- | --- |
| Assignment has incompatible types (`CP3401`) | A binding's declared or inferred type differs from the assigned value | Compare the source type, destination type, and whether the assignment is intended |
| Unknown name in a type annotation (`CP3302`) | Typo or wrong project `typeSystem` | Check spelling, `package.json`, and native prefix |
| Optional value used as present | The value may be `undefined` or `null` | Add a supported check or a nullish fallback |
| Empty array cannot be inferred | No element type is available from `[]` | Add `ElementType[]` annotation |
| Native numeric conversion rejected (`CP3401`/`CP3411`) | Strict mode or unsupported implicit conversion | Read `strictNumericCasts` and the native numeric rules |
| Unsupported `any` or dynamic shape (`CP3806`) | CP3 does not implement that open-ended TypeScript feature | Use a concrete type, `object`, or a supported `Map`/`Set` collection |
| Arithmetic result does not fit destination | Native promotion produces a different width/signedness | Declare the result's actual type or normalize at a boundary |

Fix the earliest error in a chain first. Later messages can be consequences of one incorrect inferred or declared type.

## Type design checklist

Before publishing a type in a module API, decide:

1. Is the value always present, or can it be `undefined` or `null`?
2. Is this ordinary numeric work (`number`) or a fixed-width native boundary?
3. Is the collection mutable, and can another binding alias it?
4. Is the object a named fixed shape, a class instance, or a dynamic-key collection?
5. Does the function perform asynchronous work or return a promise?
6. Does a native spelling depend on project mode or use an explicit prefix?
7. Are conversions between numeric widths allowed by the project's strictness setting?
8. Which compiler tests demonstrate the feature's supported syntax?

## Related pages

- [Bindings and declarations](./01-bindings.md)
- [Mutability and immutability](./03-mutability.md)
- [Native type-system selection](../07-native-types/01-project-type-system.md)
- [Prefixed native types](../07-native-types/02-prefixed-types.md)
- [Native numeric behavior](../07-native-types/03-numeric-rules.md)
- [Native limitations and diagnostics](../07-native-types/04-limitations.md)
