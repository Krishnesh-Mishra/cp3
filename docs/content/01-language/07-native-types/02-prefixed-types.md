---
title: Rust, C++, and TypeScript-prefixed types
chapter: Chapter 1 · The language
---
# Rust, C++, and TypeScript-prefixed types

CP3 supports three explicit type-name prefixes for code that needs to name a value using a particular ecosystem's scalar vocabulary. A prefix selects how the type name is resolved. It does not import the corresponding language, expose its entire type system, or change the generated program's backend.

## The prefix syntax

Write the prefix, an underscore, and a single-quoted type name in a type annotation:

```ts
let sequence: r_'u64' = 42;
let signedCount: c_'long long' = -8;
let normalCount: ts_'number' = 12.5;
```

The supported prefixes are:

| Prefix | Vocabulary | Why it exists |
| --- | --- | --- |
| `r_` | Supported Rust scalar names | Express Rust-shaped numeric boundaries without changing project defaults |
| `c_` | Supported C++ scalar names | Match common native header scalar spellings |
| `ts_` | Supported TypeScript/CP3 names | Explicitly request a portable CP3 type even in a Rust or C++ type-system project |

The quotes make a multiword native spelling a single type-name token. For example, `c_'unsigned long long'` names a single C++ scalar type, not a generic type expression.

## Prefixes versus project defaults

The `typeSystem` project setting selects the vocabulary for unprefixed native type names. Prefixes select a vocabulary locally and explicitly. This makes it possible to keep most code portable while recording one low-level representation:

```ts
// package.json may keep "typeSystem": "typescript".
let fileOffset: r_'u64' = 0;
let displayName: string = "report.csv";
```

If a project uses `typeSystem: rust`, `i64` is available as a plain name. `r_'i64'` still makes that choice clear at a boundary. `ts_'number'` can request CP3's regular number type in a Rust or C++ project. The regular CP3 names such as `number`, `string`, and `boolean` remain recognized in all modes.

## Rust vocabulary

The following Rust numeric names are supported by the resolver:

| Rust spelling | CP3 native category / mapping |
| --- | --- |
| `i8`, `i16`, `i32`, `i64` | Signed native integer of that width |
| `isize` | Signed pointer-sized spelling represented by CP3's `i64` native type |
| `u8`, `u16`, `u32`, `u64` | Unsigned native integer of that width |
| `usize` | Unsigned pointer-sized spelling represented by CP3's `u64` native type |
| `f32` | 32-bit native floating-point value |
| `f64` | CP3 `number` |
| `bool` | CP3 `boolean` |
| `String` | CP3 `string` |

Example:

```ts
let packetLength: r_'u32' = 512;
let retryCount: r_'i16' = -1;
let ratio: r_'f32' = 0.5;
let label: r_'String' = "payload";
```

The mapping of `isize` and `usize` to the fixed 64-bit CP3 integer representation is an implementation mapping, not a promise that the host's pointer width is always 64 bits. Treat these names as scalar-value compatibility names; they are not a complete model of Rust's target-dependent layout.

Rust `char`, `str`, `i128`, and `u128` are unsupported as CP3 plain value types. The analyzer explains these cases and suggests `u32` for a Unicode scalar value, CP3 `string` for text, and `i64`/`u64` where a 64-bit integer is suitable.

## C++ vocabulary

CP3 recognizes common C++ arithmetic scalar spellings and fixed-width aliases. The core mappings include:

| C++ spelling | CP3 native type |
| --- | --- |
| `char` | `char` (the target C++ implementation determines its signedness) |
| `signed char`, `int8_t`, `std::int8_t` | `i8` |
| `unsigned char`, `uint8_t`, `std::uint8_t` | `u8` |
| `short` and signed variants | `i16` |
| `unsigned short` variants | `u16` |
| `int`, `signed`, `signed int` | `i32` |
| `unsigned`, `unsigned int` | `u32` |
| `long`, signed `long` variants | Native `long` representation |
| `unsigned long` variants | Native `unsigned long` representation |
| `long long` and signed variants, `int64_t`, `std::int64_t` | `i64` |
| `unsigned long long`, `uint64_t`, `std::uint64_t` | `u64` |
| `size_t`, `std::size_t`, `uintptr_t` | `u64` |
| `intptr_t`, `ptrdiff_t` | `i64` |
| `float` | `f32` |
| `double` | CP3 `number` |
| `bool` | CP3 `boolean` |
| `std::string` | CP3 `string` |
| `void` | CP3 `void` |

Examples:

```ts
let rowCount: c_'unsigned int' = 8;
let fileSize: c_'std::uint64_t' = 9007199254740993;
let title: c_'std::string' = "summary";
```

Platform C++ types such as `long`, `size_t`, and pointer-sized names should be used with awareness of target ABI differences. A spelling resolving to CP3's `u64` does not make an external function ABI-safe by itself.

## TypeScript vocabulary

The `ts_` prefix explicitly names the portable CP3 types:

| Spelling | CP3 type |
| --- | --- |
| `number` | `number` |
| `string` | `string` |
| `boolean` | `boolean` |
| `undefined` | `undefined` |
| `null` | `null` |
| `void` | `void` |
| `unknown` | `unknown` |

For example, this lets a Rust-vocabulary project retain a regular CP3 number:

```ts
let raw: r_'i64' = 6;
let ratio: ts_'number' = 1.5;
let optionalRaw: r_'i64' | undefined = undefined;
```

`ts_'any'` is not an escape from CP3's banned `any` type. `object` does not name a concrete runtime shape. Use `unknown` or a declared interface/alias as appropriate.

## These are value types, not pointer declarations

A prefixed type must be a supported scalar or CP3 special type. The compiler rejects `*`, `&`, and `[]` inside a prefixed spelling. These examples are not supported:

```ts
// Not supported as CP3 prefixed value types:
// let p: c_'int*';
// let ref: r_'&i32';
// let view: c_'char[]';
```

An external function declaration that needs pointer semantics requires a separately supported interop construct and an explicit ownership contract. A pointer-looking spelling cannot safely supply these details.

## Prefix use does not cast a value

The prefix is part of a type annotation; it chooses a type name. It is not a runtime conversion operator. Assignments and expressions still pass normal type checking. Numeric literals that fit a native type can be initialized as in the examples, while nonliteral implicit conversions are governed by the analyzer and `strictNumericCasts` setting. See [Numeric rules](./03-numeric-rules.md).

## Why use explicit prefixes?

Prefixes are useful at code boundaries where width and signedness matter: file offsets, protocol fields, image channels, byte counts, checksums, and operating-system return values. They make assumptions reviewable and prevent a project setting from silently changing the interpretation of an unprefixed native name.

Keep business rules in `number` unless fixed-width arithmetic is needed. Native integers have different overflow, promotion, and formatting implications from CP3 `number`. Prefer converting at one narrow boundary over spreading native numeric types through every module.

## Verify supported names against the compiler

The mapping is a closed resolver table, not a promise to accept every spelling in the source ecosystem. If the compiler rejects a type, consult this page and the diagnostic guidance. Tests under `tests/cases/types/02/native` exercise aliases, prefixed forms, widening, unions, and unsupported names. The mapping implementation is in `src/common/src/native/native_types.cpp`.

## Related pages

- [Project type-system selection](./01-project-type-system.md)
- [Numeric behavior and conversions](./03-numeric-rules.md)
- [Limitations and diagnostics](./04-limitations.md)
- [Native interop](../../04-compiler-cli/07-native-interop.md)
