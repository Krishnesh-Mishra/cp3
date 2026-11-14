---
title: Native numeric behavior and conversions
chapter: Chapter 1 · The language
---
# Native numeric behavior and conversions

Native numeric types let a CP3 program describe fixed-width integer and float values. They are useful when a format or API requires a specific representation. Their arithmetic rules differ from CP3's general `number` model, so code should make the boundary and conversion policy explicit.

## CP3 `number` and native values are distinct

`number` is CP3's general numeric type and uses a floating-point representation. Native types such as `r_'i64'`, `r_'u32'`, and `c_'float'` are represented as integral or 32-bit floating-point scalar values. The compiler tracks native type identity during checking and emits operations accordingly.

A `number` can exactly represent integers only up to its floating-point precision limit. Use a native integer when values above that range must preserve every bit, such as a 64-bit file offset or ID. The `native_i64_precision` test covers integer values beyond the JavaScript safe-integer range.

## Initializing native values

A fitting numeric literal can initialize a native integer or float:

```ts
let small: r_'u8' = 200;
let large: r_'i64' = 9007199254740993;
let fraction: r_'f32' = 1.5;
```

For native integer literals, the analyzer checks that the literal is integral and within the target type's range. A negative literal is valid for signed types but not unsigned types. Float types accept finite numeric literals. Keep values in range; do not rely on wraparound as an input-validation rule.

The check reads the literal's exact source spelling, not its rounded `number` value, so the 64-bit boundaries initialize without losing precision:

```ts
let maxI64: r_'i64' = 9223372036854775807;
let minI64: r_'i64' = -9223372036854775808;
let maxU64: r_'u64' = 18446744073709551615;
```

`18446744073709551615` exceeds the exact integer range of `number` but still fits `u64`, and the negative `i64` boundary is accepted because unary minus applies to the exact literal magnitude. Decimal, hexadecimal (`0x`), binary (`0b`) and octal (`0o`) spellings are accepted, with optional `_` separators.

## Integral promotion

Small integer operands are promoted before arithmetic. CP3 currently follows an integral-promotion model in which integer ranks narrower than `i32` promote to `i32`. Thus `u8 + u8` produces an `i32` result in the tested rule:

```ts
let a: r_'u8' = 200;
let b: r_'u8' = 100;
let sum: r_'i32' = a + b;
```

This is why the result type need not match either operand. Declare the result type from the operation's checked result, not from an assumption that addition retains the left-hand width. Arithmetic is checked at the result width, so `a + b` above throws when the `i32` sum is out of range; a narrow compound write (`x += 1`, `x++`) is checked at `x`'s width.

## Usual arithmetic conversion behavior

When two native values have different widths, the checker chooses a common result using the native arithmetic rules implemented in CP3. Equal signedness generally selects the higher rank. If no supported common native type exists, the checker reports an error.

Mixing a signed and an unsigned integer is rejected in every project mode, not only in strict mode: C++ would otherwise convert a negative signed value to a large unsigned one, so comparisons and division would silently change meaning. Convert one side explicitly before mixing them.

```ts
let a: r_'i64' = 10;
let b: r_'u32' = 3;
let c: r_'i64' = a + (b as r_'i64');   // explicit conversion
```

The C++ and Rust source spellings resolve to CP3 canonical native identities. Arithmetic behavior is defined by that resolver and checker, not by blindly copying every detail of either language's full specification.

## Mixing `number` and native numeric values

With the default `strictNumericCasts: false`, a mixed arithmetic expression involving a native numeric value and a nonliteral `number` may be accepted as a `number` result for ordinary arithmetic. Integral-only operations such as `%`, bitwise operators, and shifts require integral operands; CP3 does not silently treat an arbitrary floating-point `number` as an integer for those operators.

A numeric literal that fits a native operand can be treated as that native type for arithmetic. This supports expressions like `nativeCount + 1` and `nativeCount % 2` without forcing every constant to carry a separate native annotation:

```ts
let count: r_'i64' = 17;
let next: r_'i64' = count + 1;
let remainder: r_'i64' = count % 2;
```

The fitting-literal rule is not a general conversion for a variable of type `number`.

## Strict numeric conversion mode

Set `strictNumericCasts` to `true` in `package.json` to reject implicit numeric mixing at checked conversion points:

```json
{
  "typeSystem": "typescript",
  "strictNumericCasts": true
}
```

With strict mode, assigning a `number` variable to a native numeric type is rejected, and mixed native arithmetic across different native types is rejected. The error requests an explicit conversion. The mode also rejects mixed arithmetic between a native type and a `number` variable. Tests in `tests/cases/types/03/native_strict` cover literal initialization, assignment, and mixed arithmetic.

## Explicit conversions

Two forms convert runtime values. The result type is the target type:

```ts
let n: number = 70000;
let code: r_'u16' = n as r_'u16';        // checked narrowing
let parsed: r_'u32' = parseTo<r_'u32'>("4294967295");
let text: string = parseTo(code, string);
```

- **Widening is implicit.** A narrower native integer can be used where a wider one of the same signedness is expected, and a fitting literal initializes a native value directly.
- **A 64-bit integer to `number` is explicit.** `i64`/`u64` can exceed the exact range of a `number`, so write `value as number` (or `Number(value)`) and accept the documented precision loss.
- **Narrowing is explicit and checked.** `value as u16` and `parseTo<u16>(value)` verify the value at runtime and throw an `Error` when it is fractional or out of range; they never truncate silently. `parseTo` also converts strings, numbers and booleans in both directions.

`parseTo` is a global builtin: no import is required.

## Text conversion

Every supported native numeric width has a `.toString()` method. Integers format exactly at their own width without a detour through `number`, so the 64-bit values above round-trip:

```ts
let id: r_'u64' = 18446744073709551615;
let text: string = id.toString();   // "18446744073709551615"
```

An `f32` prints the shortest decimal that identifies the float value, so `0.1f.toString()` is `"0.1"` rather than the longer double expansion of the promoted value. `String.fromCharCode(...codes)` performs the reverse byte-level conversion from numeric codes to a string; see the string documentation for its byte-oriented semantics.

## Arithmetic operators

Native integers support the checked native arithmetic operators implemented by CP3: addition, subtraction, multiplication, division, remainder, bitwise AND/OR/XOR, left/right shifts, comparisons, unary sign and bitwise negation, and increment/decrement where valid. `**` (power) and JavaScript's unsigned right shift `>>>` are rejected for native types. Convert to ordinary `number` for exponentiation or use a suitable standard-library math function where precision requirements allow.

Integer arithmetic is checked in every project mode. Addition, subtraction, multiplication, unary negation, increment/decrement and the arithmetic compound assignments throw an `Error` on overflow instead of wrapping (unsigned) or invoking undefined behaviour (signed). Division and remainder throw on a zero divisor, and signed `INT_MIN / -1` throws.

Signed integer division truncates toward zero (`-7 / 2` is `-3`) and remainder takes the sign of the dividend (`-7 % 2` is `-1`), matching the values JavaScript's number arithmetic produces. Division by zero is a thrown error rather than an infinity or NaN.

Checked arithmetic is the default. Hashing code that intentionally relies on modulo-2^N arithmetic can opt in per operation with `value.wrappingMul(other)`, which wraps at the receiver's width:

```ts
let byte: r_'u8' = 97;
let hash: r_'u64' = 14695981039346656037;
hash = hash ^ (byte as r_'u64');
hash = hash.wrappingMul(1099511628211);
```

Only multiplication has an explicit wrapping form; every other operator remains checked.

## Nullish options

A native value can be paired with `undefined` or `null` in a supported nullish union:

```ts
let maybeCode: r_'u32' | undefined = undefined;
let maybeOffset: r_'i64' | null = null;
```

This represents an optional native value. A union of two different non-nullish native alternatives is rejected because CP3 does not have a general tagged native numeric variant lowering for that combination. Convert to one common supported type before combining values.

## Native values in arrays and APIs

The native type checker knows the element type of declared native arrays where the feature is supported. Confirm the exact container and standard-library API surface before assuming that every generic function accepts native numeric types: generic substitutions and overloads are separate compiler features. At an external ABI boundary, check the generated signature, target width, and calling convention.

## A practical numeric-boundary pattern

Use a narrow parser/validation boundary and keep the rest of the program in the domain type it needs:

```ts
function addToOffset(offset: r_'u64', amount: r_'u32'): r_'u64 {
  return offset + amount;
}
```

This example is valid only when the checker finds a common result and return conversion under the active settings. For complex mixed-width inputs, explicitly normalize both values to one supported type before arithmetic. The signature documents the width assumption for callers.

## Debugging a numeric mismatch

When a type error appears:

1. Read the reported source location and identify whether each operand is a literal, `number`, or native type.
2. Check the current `typeSystem` and `strictNumericCasts` fields in the active project's manifest.
3. Determine whether the operator requires integral operands.
4. Check the arithmetic result type, including promotion and signedness.
5. Decide on one common representation at the boundary instead of weakening the checker globally.
6. Re-run `cp3 check` before compiling or benchmarking.

## Related pages

- [Project type-system selection](./01-project-type-system.md)
- [Prefixed native types](./02-prefixed-types.md)
- [Limitations and diagnostics](./04-limitations.md)
