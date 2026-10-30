---
title: Types and inference
chapter: Chapter 1 · The language
---
# Types and inference

CP3 checks values before it generates C++. A type describes which values a binding can hold and which operations are valid. The analyzer uses explicit annotations and inferred types together; inference saves repetition but does not make the program dynamically typed.

## Common value types

| Type | Meaning | Example |
| --- | --- | --- |
| `number` | CP3’s general numeric value; represented as a 64-bit floating-point number | `let price: number = 12.5;` |
| `string` | Text | `const name: string = "Mira";` |
| `boolean` | `true` or `false` | `let ready: boolean = false;` |
| `undefined` | The absent-value sentinel | `let x: number | undefined = undefined;` |
| `null` | A separate null value, distinct from `undefined` | `let x: string | null = null;` |
| `void` | A function result that carries no useful value | `function log(): void { ... }` |
| `unknown` | A value whose operations need a supported checked path | `let data: unknown = 5;` |

The distinction between `null` and `undefined` is intentional. A union such as `number | undefined` does not accept `null`, and a union such as `number | null` does not accept `undefined`.

## Inference

The analyzer infers a local type from an initializer when the result is unambiguous:

```ts
let count = 4;       // number
let message = "ok"; // string
let active = true;   // boolean
```

The type remains checked on reassignment. Inference does not mean the binding changes type as different values are assigned.

Annotate when the initializer is absent, when an empty collection gives no element type, when a boundary is easier to understand with a signature, or when inference would hide an important union:

```ts
let values: number[] = [];
let selected: string | undefined = undefined;
function findName(id: number): string | undefined { ... }
```

## Arrays and function types

An array type writes the element type followed by `[]`:

```ts
const ids: number[] = [3, 5, 8];
const labels: string[] = ["north", "south"];
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

`??` handles nullish values (`null` and `undefined`); it does not replace every false-like value as `||` does.

## Literal types and enums

Literal values can participate in type checking, and CP3 supports enum declarations and their backing-value rules. The compiler checks incompatible enum members and invalid enum operations rather than silently widening all enum values to strings or numbers. See the [enums page](#01-language/04-values-and-objects/03-enums) for the implemented syntax.

## `unknown` is not `any`

`any` is deliberately rejected. `unknown` represents a value that should not be used through arbitrary unchecked operations. Where a CP3 API returns `unknown`, use supported type checks or a generic type argument such as `json.parse<Config>(text)` when the API provides runtime shape validation.

## Type errors are compile-time errors

The analyzer checks assignments, call arity, argument types, member access, conditions, and return values. A diagnostic reports the source location and a stable code. Fix the first type error in a chain first: one bad inference can cause several later messages.

## Native interoperability types

The language also includes prefixed native type forms used by C++ and Rust interop. They are a separate, explicit facility, not ordinary portable CP3 types. See [Native interop](#04-compiler-cli/07-native-interop) before using them in an application API.

## Project-native type names

A project may select a native spelling vocabulary in `package.json`. The default is `"typeSystem": "typescript"`. Setting it to `"cpp"` enables supported unprefixed C++ scalar names; setting it to `"rust"` enables supported unprefixed Rust scalar names. The compiler still compiles CP3 to C++; this option controls type-name resolution, not the backend language.

```json
{
  "name": "image-worker",
  "version": "1.0.0",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "rust",
  "strictNumericCasts": true
}
```

`cp3 init --type-system rust` creates a project with this choice. To change a project later, edit the root manifest and keep the values exact and lowercase. `typeSystem` accepts `typescript`, `cpp`, or `rust`; it is separate from the project-kind field named `type`.

## Why native scalar types exist

The ordinary CP3 `number` type is convenient for application arithmetic, but it does not preserve every integer above its floating-point precision range. A file format, database driver, native API, or binary protocol may require an exact 64-bit integer, a specific signedness, or a 32-bit float. Native scalar types make those constraints visible to the type checker.

They are not required for ordinary arithmetic. Prefer `number` when fixed-width overflow and ABI width are not part of the problem. Use a native type at boundaries where the width is a requirement, and keep the conversion surface small.

## Prefixes keep declarations independent of the project mode

The `r_`, `c_`, and `ts_` prefixes choose Rust, C++, and TypeScript/CP3 vocabularies respectively:

```ts
let bytesFromWire: r_'u32' = 512;
let oldCppCount: c_'long long' = 9007199254740993;
let displayRatio: ts_'number' = 0.25;
```

This is a type spelling, not a cast expression. An annotation does not bypass assignment checks. Prefixes are especially useful in a project using TypeScript mode that has just one fixed-width field, or in a Rust/C++ mode where one API should explicitly use CP3's regular `number`.

See [Native type-system selection](#01-language/07-native-types/01-project-type-system) for configuration and [Prefixed native types](#01-language/07-native-types/02-prefixed-types) for the supported names.

## Native values do not imply foreign function compatibility

A scalar annotation can express the CP3 checker's native numeric type. It does not by itself import a header, resolve a symbol, define a calling convention, transfer ownership, or prove that a target platform uses the same width for `long` or `size_t`. Those concerns belong to the interop boundary; see [Native interop](#04-compiler-cli/07-native-interop).

## Quick selection table

| Need | Prefer |
| --- | --- |
| General application arithmetic | `number` |
| Preserve exact values above the usual floating-point integer range | Supported `i64`/`u64` spelling |
| Match a Rust-facing scalar declaration | Rust mode or `r_'...'` |
| Match a C++ scalar declaration | C++ mode or `c_'...'` |
| Keep a regular CP3 type explicit in a native mode | `ts_'number'`, `ts_'string'`, etc. |
| Describe an optional native scalar | `nativeType | undefined` or `nativeType | null` |
| Describe a pointer, borrow, or array declarator | Not a prefixed value type; use a supported interop boundary |

## Continue with the native-type chapter

The native pages are arranged by the decision a developer usually needs to make:

1. [Select a project vocabulary](#01-language/07-native-types/01-project-type-system).
2. [Choose an explicit spelling](#01-language/07-native-types/02-prefixed-types).
3. [Understand arithmetic and conversions](#01-language/07-native-types/03-numeric-rules).
4. [Check limits and diagnostics](#01-language/07-native-types/04-limitations).

## A type is a compile-time contract

A CP3 type answers three practical questions: which values may flow into this binding, which operations can be applied to those values, and which results can flow out. The compiler checks those relationships before it emits C++. An annotation is therefore part of the program contract, not a comment for an editor.

For example, a function declared as `function parseCount(text: string): number` accepts a string argument and promises a numeric result. Passing a boolean or returning a string is a compile-time error. If a value can be missing, include `undefined` in the type instead of assuming that a caller will remember an undocumented convention.

```ts
function parseCount(text: string): number | undefined {
  if (text.length === 0) {
    return undefined;
  }
  return 1;
}
```

This example only demonstrates the return contract; a real parser should validate every character and decide how malformed input is reported.

## Type annotations and inferred types

Annotate a declaration when it has no initializer, when an empty literal gives the checker no element type, when it is a public function boundary, or when the type is a meaningful part of the design. Let the compiler infer obvious local values when the result remains clear:

```ts
const retries = 3;       // inferred number
const service = "files"; // inferred string
let isReady = false;     // inferred boolean

let pending: string | undefined = undefined;
let names: string[] = [];
```

Inference happens during analysis. Once a local binding has a type, a later assignment must satisfy it. CP3 does not change a binding from number to string just because a later branch assigns a string.

Avoid annotating every intermediate expression with a type that repeats the initializer. Instead, place annotations at APIs, persistent state, and places where a type cannot be inferred safely.

## `number`

`number` is CP3's general numeric type. It is a 64-bit floating-point value, similar in broad representation to JavaScript's `number`. It is useful for measurements, ratios, ordinary counters, and math-library operations. It is not an arbitrary-precision integer.

A floating-point number has finite precision. If exact integer values above the safe range matter, use a native integer type from the [native types chapter](#01-language/07-native-types/00-overview) and check its bounds. Do not convert a large exact integer through `number` and expect all low bits to survive.

The compiler's numeric literal syntax includes integer and decimal forms; the type checker treats ordinary numeric expressions as `number` unless a supported native type context applies. Integer division of two ordinary `number` values remains numeric division; native integer division is a distinct operation.

```ts
const taxRate: number = 0.08;
const total = 125 * (1 + taxRate);
```

## `string`

`string` represents CP3 text. String literals use quotes or template literals. Concatenation and interpolation create string values; standard string operations are described in [Strings](#03-standard-library/15-strings).

CP3 stores string data as UTF-8. Its `length` behavior is byte-oriented rather than JavaScript's UTF-16 code-unit count, while character-oriented methods have their own specified behavior. Do not port JavaScript code that assumes `text.length` counts UTF-16 code units without checking the [string guide](#03-standard-library/15-strings).

```ts
const label: string = "report";
const message = `generated: ${label}`;
```

A CP3 string is an owned CP3 value. A C++ `std::string` or Rust `String` spelling resolves to this CP3 string type in the corresponding native name vocabulary; it does not expose arbitrary foreign string storage or lifetime semantics.

## `boolean`

`boolean` contains `true` or `false`. Conditions require a supported boolean context. CP3 does not generally treat every JavaScript truthy or falsey value as a boolean. The current checker has specific rules for conditions; see [Conditions and loops](#01-language/03-control-flow/01-conditions-and-loops).

```ts
let connected: boolean = false;
if (connected) {
  // ...
}
```

When porting JS, replace implicit checks such as `if (value)` with an explicit comparison or a documented boolean conversion if `value` is not already boolean.

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

## `void`

A function returning `void` does not return a useful value. It can still perform effects such as writing output or updating state. A void result should not be used as if it were a number, string, or boolean.

```ts
function announce(message: string): void {
  console.log(message);
}
```

The exact accepted syntax for function bodies and imports is covered in the linked language and standard-library pages.

## `unknown`

`unknown` is a checked boundary for a value whose concrete type is not known at the current point. It does not authorize arbitrary property access or calls. Narrow the value using a supported check or pass it to an API that validates it. CP3 does not accept `any` as an unchecked substitute.

JSON parsing often returns `unknown` unless a generic type argument is supplied to the checked parsing API. A generic argument is only as trustworthy as the API's runtime validation; read [JSON](#03-standard-library/09-json) before treating a parsed value as a validated domain object.

## Arrays and element types

An array type is written as `ElementType[]`. Every item is checked against that element type. For example, `number[]` cannot be assigned a string element. Empty arrays need an annotation because there is no element from which to infer a type.

```ts
const scores: number[] = [10, 12, 15];
const names: string[] = [];
names.push("Asha");
```

Arrays are mutable reference-shaped values. `const` prevents rebinding the array variable; it does not freeze the array contents. Two bindings can refer to the same array, so a mutation through either binding is visible through the other. See [Arrays and objects](#01-language/04-values-and-objects/01-arrays-and-objects) and [Mutability](#01-language/01-variables-and-bindings/03-mutability).

## Object shapes and named interfaces

Object literal types can describe a fixed set of known properties. Interfaces and aliases give those shapes names so they can be reused in parameters, return values, and class contracts. CP3 does not provide arbitrary dynamic index signatures over every object; model a dictionary with the supported collection type when keys are dynamic.

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

A function signature describes parameters and the result. Parameters and return values are checked at call sites and return statements. Optional parameters, rest parameters, generics, and async functions each have additional rules; use [Functions and closures](#01-language/02-functions/01-functions-and-closures) for those details.

A function value can be passed or stored where its function type is accepted. Closures can capture surrounding bindings, which affects lifetime and runtime ownership; see [Closures](#02-runtime/04-closures).

## Type aliases and generic aliases

A type alias names an existing type expression. It does not create a new runtime object or nominally distinct wrapper type:

```ts
type UserId = number;
type MaybeText = string | undefined;
```

Generic aliases can express reusable shapes where supported:

```ts
type Pair<T> = [T, T];
```

The compiler checks substitutions at use sites. A generic alias does not make unsupported type operators or arbitrary TypeScript conditional types available. See [Generics](#01-language/05-generics/01-generics).

## Tuples and unions

A tuple describes a fixed sequence of element types. A union describes a value that can match one of its listed alternatives. In both cases, the compiler tracks the possible types and checks operations against that information.

```ts
type Point = [number, number];
type Lookup = string | undefined;
```

For a union, an operation must be valid for the current narrowed type or for every possible member as the language rules require. Nullish unions are common for optional results. Native scalar unions have additional restrictions described in [Native limitations](#01-language/07-native-types/04-limitations).

## Enums

Enums describe a closed named set of values with checked member rules. CP3 validates enum declarations and operations; it does not treat every enum as a string union. Use [Enums](#01-language/04-values-and-objects/03-enums) for the actual declaration syntax, backing-value behavior, and compatibility rules.

## Classes and interfaces

Classes describe runtime objects and methods; interfaces describe a checked shape or contract. They are different tools: a class can have construction and runtime identity, while an interface is a type-level contract. See [Classes and interfaces](#01-language/04-values-and-objects/02-classes-and-interfaces) for constructors, inheritance/implementation rules, methods, and limitations.

Class instances are reference-shaped under the ordinary runtime representation. The annotation does not say that an object lives on the stack. Allocation and ownership behavior are documented in the [runtime chapter](#02-runtime/00-overview).

## Type narrowing

A value declared as a union may be used more precisely after a supported condition establishes which alternative is present. Nullish coalescing is one common way to supply a fallback. The exact narrowing forms are language features, so do not assume every TypeScript discriminated-union pattern is implemented. Check [control flow](#01-language/03-control-flow/01-conditions-and-loops) and conformance tests when relying on a specific guard.

## Compile-time types and runtime representation

Some types have direct scalar representations; others use runtime-managed objects or tagged/nullish wrappers. The compiler and runtime jointly implement these representations. Source code should rely on language semantics rather than `std::shared_ptr`, generated struct layouts, or C++ aliases, except when deliberately working at a native interop boundary.

CP3 does not have a tracing garbage collector. Reference-shaped runtime values use reference counting: ordinary `Ref<T>` handles have atomic counts, while a conservative compiler gate can select non-atomic `LocalRef<T>` for some class types. Neither representation changes source-level type rules or guarantees stack allocation. See [Reference ownership](#02-runtime/03-reference-ownership) and [No tracing GC](#02-runtime/08-no-tracing-gc).

## How to resolve common type errors

| Diagnostic situation | Typical cause | What to inspect |
| --- | --- | --- |
| Assignment has incompatible types | A binding's declared or inferred type differs from the assigned value | Compare the source type, destination type, and whether the assignment is intended |
| Unknown name in a type annotation | Typo or wrong project `typeSystem` | Check spelling, `package.json`, and native prefix |
| Optional value used as present | The value may be `undefined` or `null` | Add a supported check or a nullish fallback |
| Empty array cannot be inferred | No element type is available from `[]` | Add `ElementType[]` annotation |
| Native numeric conversion rejected | Strict mode or unsupported implicit conversion | Read `strictNumericCasts` and native numeric rules |
| Unsupported `any` or dynamic shape | CP3 does not implement that open-ended TypeScript feature | Use `unknown`, a named shape, or a supported Map/Set collection |
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

## Choosing a native type mode, step by step

Use the native type system only when the code needs an explicit width, signedness, or compatibility spelling. Choose
between project-wide vocabulary and a local prefix based on how much of the project needs those names:

1. **Keep `typescript`** when the application mostly uses CP3's portable `number`, `string`, and `boolean` types.
2. **Choose `cpp`** when many declarations should use supported C++ spellings such as `long long`, `uint32_t`, or
   `size_t`.
3. **Choose `rust`** when many declarations should use supported Rust spellings such as `i64`, `u32`, or `usize`.
4. **Keep the default and prefix one declaration** when only one module boundary requires a native width.
5. **Turn on `strictNumericCasts`** when the project should require careful handling at mixed numeric boundaries.

The setting belongs in the project root's `package.json`, next to other CP3 project fields:

```json
{
  "name": "binary-reader",
  "version": "1.0.0",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "rust",
  "strictNumericCasts": true
}
```

`type` in that file describes whether the project is an application or package. `typeSystem` chooses the default native
type-name vocabulary. `strictNumericCasts` is a boolean policy for implicit numeric conversions. These fields solve
different problems and should not be interchanged.

## What happens when the compiler reads the setting

The CLI loads `typeSystem` and `strictNumericCasts` into compile options for the project. TypeScript mode is the default
when no setting is provided. If a value in `typeSystem` is misspelled, correct the JSON rather than relying on a
fallback. The source type resolver first recognizes the ordinary CP3 names; in C++ or Rust mode it can then resolve
unprefixed names from that mode's closed native table. A prefixed type explicitly selects its table independent of the
project mode.

For compiler contributors, these behaviors are implemented across `src/cli/src/pkg/manifest/package_json.cpp`,
`src/common/src/native/native_types.cpp`, parser type handling, and semantic checking. Add or update tests when any
default, accepted spelling, or conversion rule changes; changing the documentation alone does not enable a type.

## Compare project defaults and explicit prefixes

| Project setting | Source annotation | How CP3 resolves it |
| --- | --- | --- |
| `typescript` | `number` | Ordinary CP3 numeric type |
| `typescript` | `i64` | Not selected as a plain Rust name; use the explicit prefix |
| `typescript` | `r_'i64'` | Rust vocabulary, native signed 64-bit integer |
| `cpp` | `long long` | C++ vocabulary, native signed 64-bit integer |
| `rust` | `i64` | Rust vocabulary, native signed 64-bit integer |
| `rust` | `ts_'number'` | Explicit ordinary CP3 numeric type |

This difference helps library code be explicit about its public boundary. A local prefix makes the intended type
visible even if a reader does not know the project's `package.json` setting. Unprefixed names are shorter when the
whole project consistently uses one native vocabulary.

## Why the prefixes use quoted spellings

The grammar treats `r_`, `c_`, and `ts_` as a prefix followed by a quoted type name. Quoting lets the C++ vocabulary
include multiword types in one syntactic unit:

```ts
let signedWide: c_'long long' = 9007199254740992;
let unsignedWide: c_'unsigned long long' = 12;
let rustCount: r_'u32' = 12;
let ordinary: ts_'number' = 12.5;
```

The quote does not turn a value into a string. It belongs to type syntax. It is also not a general escape hatch: the
resolver accepts only listed type names and rejects pointer, reference, and array declarators in a prefixed name.

## Reading native values in an API

For a portable API, use `number` and validate its allowed range at the boundary. For an API whose contract requires a
fixed-width integer, put that requirement in the signature:

```ts
function readWord(offset: r_'u64'): r_'u32' {
  return 0;
}
```

The sketch shows the intended type boundary, not a real buffer parser. An implementation must check the source length,
endianness, and failure behavior before reading. For a group of C++-shaped declarations, set the project mode to `cpp`
and write plain supported names. For an isolated declaration, a `c_` prefix avoids making the rest of the application
depend on that project setting.

Do not use the native scalar type tier as a substitute for an FFI design. To call external code, separately define the
symbol, ABI, target compiler, ownership, and error contract; see [Native interop](#04-compiler-cli/07-native-interop).

## Understand numeric strictness through a concrete example

With `strictNumericCasts` omitted or `false`, the checker permits some ordinary mixed arithmetic and produces a
`number` result. A fitting literal next to a native number can instead retain native arithmetic. This convenience does
not mean an arbitrary `number` variable is an integer or that every conversion is safe.

With `strictNumericCasts: true`, the checker rejects conversions such as this one:

```ts
let source: number = 5;
let destination: r_'i64' = source;
```

It also rejects implicit mixed arithmetic between a native number and a separate `number` variable, or between distinct
native scalar types in the checked cases. An error in `tests/cases/types/03/native_strict/assign/assign_number` asserts
diagnostic CP3401 for the assignment above. The mixed-width and mixed-number fixtures in the same test family cover
arithmetic. Use these tests to understand the exact current policy before designing a conversion-heavy API.

The setting is opt-in and project-wide. It helps expose implicit numeric boundaries early, but it does not validate
external input, prevent overflow in every operation, or make a native type portable across every target ABI.

## Native type boundaries are still checked CP3 types

The C++ and Rust spelling families map into CP3 canonical types. For example, the checker treats `r_'i64'` and a
supported C++ `long long` spelling as the same signed native integer category for arithmetic. `r_'f64'`, `c_'double'`,
and `ts_'number'` resolve to
ordinary CP3 `number`; they are not distinct native 64-bit float types in CP3's type checker.

This mapping is useful at source boundaries, but it is not evidence that values can be passed to arbitrary foreign
functions without conversion. Generated function declarations and target ABI requirements must also agree.

## Test paths that define the native surface

When changing or documenting native types, inspect these conformance families:

- `tests/cases/types/02/native_project_cpp` demonstrates plain C++ scalar names under a C++ project setting.
- `tests/cases/types/03/native_project_rust` demonstrates plain Rust names under Rust mode and prefixed CP3 types.
- `tests/cases/types/02/native/prefixed` covers explicit prefixed types.
- `tests/cases/types/02/native/i64` checks native integer precision beyond the regular safe-integer boundary.
- `tests/cases/types/02/native/unions` checks native nullish unions.
- `tests/cases/types/02/native/unsupported` and `tests/cases/types/03/native_strict` cover rejected forms and strict
  numeric conversions.

Run the focused type test filter after a type-system change. A `cp3 check` success validates parsing and semantic
analysis; run-mode fixtures are needed to check generated values and observable output too.

## Related guides

- [Bindings and declarations](#01-language/01-variables-and-bindings/01-bindings)
- [Mutability and immutability](#01-language/01-variables-and-bindings/03-mutability)
- [Project native type-system selection](#01-language/07-native-types/01-project-type-system)
- [Prefixed native types](#01-language/07-native-types/02-prefixed-types)
- [Native numeric behavior](#01-language/07-native-types/03-numeric-rules)
- [Native limitations and diagnostics](#01-language/07-native-types/04-limitations)
