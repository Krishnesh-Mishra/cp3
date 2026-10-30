---
title: Native type-system selection
chapter: Chapter 1 · The language
---
# Native type-system selection

CP3 has a portable TypeScript-like type vocabulary and an opt-in native numeric vocabulary. A project selects which native vocabulary is used for unprefixed names. This setting affects how the compiler parses and resolves types; it does not select a C++ or Rust runtime, compiler backend, ABI, or linker.

## The project setting lives in `package.json`

The setting is named `typeSystem` and accepts exactly `typescript`, `cpp`, or `rust`:

```json
{
  "name": "counter-app",
  "version": "1.0.0",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "rust"
}
```

`cp3 init` writes `"typescript"` by default. Choose `cpp` or `rust` if you want the corresponding native spelling vocabulary to work without a prefix. The compiler still emits C++ for CP3 programs; `"rust"` means Rust-style type names, not Rust code generation.

## What each choice changes

| `typeSystem` | Portable CP3 names | Native names accepted without a prefix | Typical use |
| --- | --- | --- | --- |
| `typescript` | `number`, `string`, `boolean`, `void`, unions, arrays, and other supported CP3 forms | No Rust/C++ numeric vocabulary | Portable CP3 applications and libraries |
| `cpp` | Same CP3 names | C++ scalar spellings such as `int`, `unsigned`, `long long`, `size_t`, `float` | Code whose numeric boundaries mirror C/C++ APIs |
| `rust` | Same CP3 names | Rust scalar spellings such as `i32`, `u64`, `usize`, `f32` | Code whose numeric boundaries mirror Rust APIs |

This is a type-name setting. It does not make C++ headers importable, bind Rust crates, make pointers safe, or change native toolchain selection. See [Native interop](#04-compiler-cli/07-native-interop) for the narrower ABI boundary.

## Choosing a mode when creating a project

The interactive `cp3 init` prompt asks for a type system. For scripts and automation, pass the option directly:

```powershell
cp3 init --type-system rust
```

The accepted values are `typescript`, `cpp`, and `rust`. The generated manifest records the choice. To change an existing project, edit the `typeSystem` field in its root `package.json`; the compiler reads the project manifest when it loads project settings.

A minimal existing project can be updated like this:

```json
{
  "name": "native-boundary-demo",
  "version": "1.0.0",
  "type": "application",
  "main": "src/index.cp3",
  "typeSystem": "cpp"
}
```

Keep valid JSON: a missing comma or a quoted boolean can invalidate the intended configuration. `strictNumericCasts` is a separate boolean setting; it is not a `typeSystem` value.

## TypeScript mode and explicit native spellings

In `typescript` mode, `number` remains the ordinary CP3 numeric type. To declare a native integer in a project that otherwise uses portable CP3 names, use a prefix:

```ts
let requestId: r_'u64' = 9007199254740993;
let ratio: number = 1.25;
```

`r_` says that the quoted name belongs to the Rust vocabulary. This makes native-width intent visible at the declaration and works regardless of the project default. The `c_` and `ts_` prefixes similarly select the C++ or TypeScript name vocabulary explicitly.

## Plain native names in C++ mode

With `"typeSystem": "cpp"`, the compiler accepts supported C++ scalar names as plain type names. Multiword spellings must be written as a CP3 type name; the parser joins recognized continuations:

```ts
let count: long long = 9007199254740993;
let flags: unsigned int = 12;
let bytes: size_t = 64;
let message: string = "ready";
```

The project test `tests/cases/types/02/native_project_cpp/plain/plain/main.cp3` checks these forms, including `int64_t`, `std::string`, `char`, `float`, and `double`. `std::string` is a spelling for CP3 `string`; it is not a raw `std::string` object exposed to source code.

## Plain native names in Rust mode

With `"typeSystem": "rust"`, the compiler accepts supported Rust scalar names without prefixes:

```ts
let count: i64 = 31;
let capacity: usize = 10;
let ratio: f32 = 1.5;
let label: String = "worker";
```

`i64`, `usize`, and `f32` are native numeric values. `String` maps to CP3 `string`; `bool` maps to CP3 `boolean`; `f64` maps to CP3 `number`. The project's test uses these mappings and checks output. It does not establish a Rust ABI or permit borrowing Rust-owned strings across a boundary.

## Settings are project scoped

A project manifest supplies defaults to compilations made in that project. Keep one consistent setting for a project instead of relying on source-file-specific assumptions. A library's authors should document the mode expected for its examples and public numeric types. When source must be independent of project mode, prefer explicit `r_` or `c_` prefixes for native declarations.

The compiler defaults to TypeScript mode when no usable setting is loaded. An absent setting therefore means portable CP3 names, not implicit C++ or Rust names. An unrecognized `typeSystem` string is not one of the accepted settings; fix the manifest rather than depending on fallback behavior.

## Strict numeric conversions are a second switch

`strictNumericCasts` controls implicit conversions involving native numeric values. It defaults to `false`. With strict mode enabled, implicit mixing or assignment between `number` and a native numeric type, or between distinct native numeric types, is rejected in the cases checked by the analyzer. The setting can be enabled in the same manifest:

```json
{
  "typeSystem": "rust",
  "strictNumericCasts": true
}
```

This switch does not change the chosen spelling vocabulary. For example, `typeSystem: rust` plus `strictNumericCasts: true` still resolves plain `i64` as Rust-style native integer, but applies stricter conversion checks to its use. See [Native numeric behavior](#01-language/07-native-types/03-numeric-rules).

## How to verify a project's setting

1. Locate the `package.json` that belongs to the project being compiled.
2. Read `typeSystem` and check it is one of the three supported strings.
3. Confirm your source spelling matches the selected vocabulary or uses an explicit prefix.
4. Run `cp3 check` on the same project entry point, so the manifest is loaded.
5. If a native name is rejected, check its exact supported spelling on [Prefixed native types](#01-language/07-native-types/02-prefixed-types).

Do not infer the type mode from the operating system or the installed `g++`/`rustc`. It comes from the CP3 project setting.

## Practical choice guide

Use TypeScript mode when portability and familiar CP3 names matter more than fixed-width arithmetic. Use Rust mode if APIs and domain values are naturally described by Rust scalar names, such as byte counts and integer IDs. Use C++ mode when native APIs expose C/C++ integer ranks or when matching their declared numeric widths avoids repeated translation at the boundary.

Do not switch a whole application to a native mode only to change one variable. In TypeScript mode, a prefixed native type can keep the rest of the application in ordinary CP3 types. Conversely, a native vocabulary does not make every value native: `string`, arrays, classes, and promises still follow their CP3 definitions.

## Common configuration mistakes

| Mistake | Why it fails or surprises | Correction |
| --- | --- | --- |
| Setting `"typeSystem": "C++"` | Values are case-sensitive and the accepted spelling is `cpp` | Use `"cpp"` |
| Putting `"rust"` in `"type"` | `type` selects application/package project kind | Put it in `typeSystem` |
| Expecting `typeSystem: rust` to generate Rust | This selects names only; CP3's backend remains C++ | Configure the native toolchain separately if linking is needed |
| Using `i64` under TypeScript mode without a prefix | Plain Rust names are not selected in this mode | Use `r_'i64'` or select Rust mode |
| Assuming every Rust/C++ type works | CP3 supports a documented subset of scalar value types | Check the spelling and limitation pages |
| Quoting `true` for strict mode | A JSON string is not a JSON boolean | Write `"strictNumericCasts": true` |

## Related pages

- [Types and inference](#01-language/01-variables-and-bindings/02-types) explains ordinary CP3 types.
- [Prefixed types](#01-language/07-native-types/02-prefixed-types) lists the supported name families.
- [Numeric rules](#01-language/07-native-types/03-numeric-rules) explains promotions, literals, and strict conversion checks.
- [Limitations](#01-language/07-native-types/04-limitations) lists unsupported forms and common diagnostics.
- [Native interop](#04-compiler-cli/07-native-interop) covers ABI and ownership boundaries.
