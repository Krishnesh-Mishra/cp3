---
title: Native type limitations and diagnostics
chapter: Chapter 1 · The language
---
# Native type limitations and diagnostics

CP3's native type vocabulary is deliberately a small scalar tier. It is not a parser for arbitrary Rust or C++ declarations. Recognizing a name means the analyzer has a CP3 value representation and rules for that type; it does not mean the compiler can import a class, template, pointer, reference, or platform ABI automatically.

## Unsupported spellings are intentional

The resolver rejects types that do not have a supported plain CP3 value representation. The diagnostic often explains a safe alternative:

| Requested spelling | Reason | Suggested direction |
| --- | --- | --- |
| Rust `i128`, `u128` | No portable C++ scalar value mapping in the current native tier | Use `i64` or `u64` if sufficient |
| Rust `char` | Rust Unicode scalar type is not represented as CP3's plain character type | Use `u32` for a code point or CP3 `string` for text |
| Rust `str` | Unsized borrowed string view, with lifetime semantics CP3 does not express here | Use CP3 `string` |
| C++ `long double` | No CP3 native value mapping | Use `double`/`number` if its precision is adequate |
| C++ `wchar_t`, `char8_t`, `char16_t`, `char32_t` | Encoding and platform representation are not modeled by this value tier | Use a documented integer/code point or CP3 `string` representation |
| C++ `auto` | Deduction placeholder, not a concrete declared value type | Write the concrete type |
| C++ `std::string_view` | Non-owning view with lifetime requirements | Use `std::string`/CP3 `string` when an owned string is intended |
| `any` | CP3 intentionally rejects unchecked arbitrary values | Use `unknown` or a concrete type |
| TypeScript `object` as a native prefixed scalar | It does not describe a concrete runtime layout | Use an interface or named object type |

These limits protect the front end from claiming ownership, layout, or conversion semantics it cannot guarantee.

## Pointer, reference, and array spellings

`r_`, `c_`, and `ts_` forms are scalar type-name escape hatches. The analyzer rejects `*`, `&`, and `[]` in the quoted spelling. For example, `c_'int*'`, `r_'&i32'`, and `c_'char[]'` are invalid. This avoids treating a borrowed pointer or a C++ array declarator as a self-contained CP3 value.

Use CP3 arrays for owned CP3 collections. For native pointers, define a small foreign-function boundary using an explicitly supported interop mechanism, and document which side owns and frees each resource. See [Native interop](../../04-compiler-cli/07-native-interop.md).

## Unknown name versus unsupported type

The compiler distinguishes an unrecognized spelling from a known but unsupported spelling. A typo such as `r_'u33'` is an unknown Rust type name and may include a suggestion. A known unsupported type such as `r_'i128'` reports why that type has no current CP3 representation. Correct the spelling or choose the suggested supported scalar; do not silence the diagnostic with `unknown` unless the value is genuinely dynamic.

## Prefix and project mode errors

A prefix must be one of `r_`, `c_`, or `ts_`. A spelling must belong to that prefix's closed vocabulary. For example, `r_'long long'` is not a Rust spelling, while `c_'long long'` is valid. Without a prefix, the active project `typeSystem` determines whether native names are considered.

## Union restrictions

A nullish union with one native alternative is supported, such as `r_'u32' | undefined`. A union with two non-nullish native alternatives such as `r_'i64' | r_'u32'` is rejected. The current backend does not define a stable tagged representation and coercion policy for arbitrary native alternatives.

## Numeric conversion diagnostics

When `strictNumericCasts` is true, the checker reports a conversion or mixed-arithmetic diagnostic when values of different numeric types are combined implicitly in the covered contexts. The message asks for an explicit conversion. CP3 does not inherit the cast syntax of the selected source vocabulary; verify a supported conversion before designing an API around it.

Even with strict mode disabled, integral-only operations still reject non-integral or `number` operands where an integer is required. A native integer plus a fitting literal is treated as the native operation, but a `number` variable is not the same as a literal constant.

## Diagnostic codes and source locations

A type diagnostic includes a stable CP3 code and a source span. Use the code when searching tests or compiler diagnostics; use the source span to find the first declaration or expression that caused the mismatch. The exact diagnostic text can evolve, while the semantic distinction—unknown name, unsupported name, invalid union, or illegal conversion—should guide the repair.

For compiler contributors, the native vocabulary is centralized in `src/common/src/native/native_types.cpp`; sema policy lives in `src/sema/src/checks/native_types.cpp` and assignment checking. Parser support for prefixed type syntax is separate from name resolution. Keep the mapping, diagnostics, tests, and docs in sync when adding a type.

## Tests that define the current surface

The most relevant conformance fixtures are grouped under `tests/cases/types/02` and `tests/cases/types/03`:

- Native project fixtures check unprefixed Rust and C++ names.
- Prefixed fixtures check `r_`, `c_`, and `ts_` resolution.
- Integer fixtures check width, signedness, arithmetic, shifts, and precision.
- Union fixtures check nullish optional values and reject unsupported mixed native unions.
- Unsupported-type fixtures check messages for Rust and C++ names outside the supported set.
- Strict-mode fixtures check assignment and mixed arithmetic rejection.

A new mapping should include a success fixture and a failure fixture when relevant. Test values near zero, signed boundaries, and widths above the ordinary `number` exact-integer range when precision is part of the feature.

## Safe documentation rule

Do not describe a type as ABI-compatible solely because its name resembles a Rust or C++ type. Verify the compiler's generated declaration, platform-width assumptions, call ABI, ownership, and linked implementation. The type vocabulary documents scalar checking; the [compiler's interop guide](../../04-compiler-cli/07-native-interop.md) documents the separate foreign boundary.

## Related pages

- [Project type-system selection](01-project-type-system.md)
- [Prefixed native types](02-prefixed-types.md)
- [Numeric behavior](03-numeric-rules.md)
- [Types and inference](../01-variables-and-bindings/02-types.md)
