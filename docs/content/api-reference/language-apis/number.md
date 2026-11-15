---
title: Number
---
# Number

`number` is CP3's general numeric type: an IEEE-754 64-bit floating-point value, similar in representation to JavaScript's `number`. Literals include decimal, hexadecimal (`0x`), binary (`0b`), and octal (`0o`) forms with optional `_` separators; fractional and exponent spellings (`2.5`, `1e3`) are accepted and keep `number`. See [types](../../language/01-variables-and-bindings/02-types.md) for literal rules.

## `number` vs native integer types

`number` exactly represents integers only up to 2^53. Use a native integer (`i32`, `u32`, `i64`, `u64`, `f32`, …) when the format or API requires a fixed width; native arithmetic is overflow-checked in every project mode. Widening is implicit; conversions from `number` to a native numeric type are **not** silent truncation — `x as u16` and `parseTo<u16>(x)` range-check at runtime and throw on out-of-range or fractional values. Mixing a `number` variable with a native numeric value in arithmetic is rejected under `strictNumericCasts: true`, and mixing signed and unsigned natives is rejected in every mode (CP3411). See [native numeric behavior](../../language/07-native-types/03-numeric-rules.md) and [prefixed native types](../../language/07-native-types/02-prefixed-types.md).

## Parsing and conversion

| Member | Signature | Notes |
| --- | --- | --- |
| `parseTo` | `parseTo(value, number): number` · `parseTo(value, string): string` · `parseTo(value, boolean): boolean` | Bidirectional scalar converter; the target is a static type keyword. Two-argument form — the result type follows the target keyword. |
| `parseTo` | `parseTo<T>(value): T` | Type-argument form; `T` may be any scalar type, including a native numeric type (e.g. `parseTo<u16>(port)`). |
| `parseInt` | `parseInt(text: string): number` | Legacy JS-compat global; always `number`, one argument, no radix parameter. Details on [Global functions](./global-functions.md). |
| `parseFloat` | `parseFloat(text: string): number` | Legacy JS-compat global; always `number`, one argument. Details on [Global functions](./global-functions.md). |

`parseTo` rules (see [native numeric behavior](../../language/07-native-types/03-numeric-rules.md)):

- The target must be a **scalar** type — not an array or an object.
- A literal source is checked against the target at compile time.
- A narrowing conversion to a native numeric type range-checks at runtime and throws `Error` on an out-of-range or fractional value; it never silently truncates or wraps.
- A widening conversion passes the value through.
- In the default mode, native-to-native assignments follow C++-style conversion rules; `strictNumericCasts: true` requires an explicit `as` / `parseTo<T>` conversion.
- `parseTo(value, string)` converts any scalar to its text form; the reverse direction (`parseTo<u16>("4294967295")`) converts text to the target type with a runtime range check.

```ts
const port = parseTo<u16>("8080");   // range-checked at runtime
const n = parseTo("123", number);
const text = parseTo(port, string);
```

## `Number` statics and methods (language-level, no import)

| Member | Signature | Notes |
| --- | --- | --- |
| `Number.isInteger` | `Number.isInteger(value: number): boolean` | True when finite with no fractional part. |
| `Number.isSafeInteger` | `Number.isSafeInteger(value: number): boolean` | True for finite integers in `-(2^53 - 1)..2^53 - 1`. |
| `Number.isFinite` | `Number.isFinite(value: number): boolean` | True when finite (not `NaN` or an infinity). Strict — does not coerce. |
| `Number.isNaN` | `Number.isNaN(value: number): boolean` | True when `value` is `NaN`. Strict — does not coerce. |
| `Number.parseInt` | `Number.parseInt(text: string): number` | Same as the global `parseInt`. |
| `Number.parseFloat` | `Number.parseFloat(text: string): number` | Same as the global `parseFloat`. |
| `Number.MAX_SAFE_INTEGER` | `number` | `2^53 - 1` = `9007199254740991`. |
| `Number.MIN_SAFE_INTEGER` | `number` | `-(2^53 - 1)`. |
| `Number.EPSILON` | `number` | Difference between `1` and the next representable double. |
| `Number.MAX_VALUE` | `number` | Largest finite double. |
| `Number.MIN_VALUE` | `number` | Smallest positive double (subnormal). |
| `Number.POSITIVE_INFINITY` | `number` | Positive infinity. |
| `Number.NEGATIVE_INFINITY` | `number` | Negative infinity. |
| `Number.NaN` | `number` | The not-a-number value. |
| `toFixed` | `toFixed(digits?: number): string` | Fixed-point with `digits` digits after the decimal point (0 by default). |
| `toPrecision` | `toPrecision(precision?: number): string` | Fixed or exponential notation with `precision` significant digits. |
| `toExponential` | `toExponential(digits?: number): string` | Exponential notation with `digits` digits after the decimal point. |
| `toString` | `toString(radix?: number): string` | Text in the given radix (default 10). |

`Number(x)` is the coercion global: `Number(value): number`, JS-style conversion of any argument. See [Global functions](./global-functions.md).

## `isNaN` and `isFinite` (globals)

These coerce the argument with `Number(x)` before testing (JS behavior — they are **not** the strict `Number.isNaN` / `Number.isFinite` statics):

| Member | Signature | Notes |
| --- | --- | --- |
| `isNaN` | `isNaN(value): boolean` | True when the coerced value is `NaN`. |
| `isFinite` | `isFinite(value): boolean` | True when the coerced value is finite. |

## Scientific notation and ordering

- Exponent spellings such as `1e3` and `2.5e-2` are `number` literals.
- `+0` and `-0` are distinct values; `0` is falsy and `NaN` compares false against everything including itself.
- Division of two `number` operands is numeric division (`7 / 2` is `3.5`); native integer division is a distinct operation (truncates toward zero; divide by zero throws).

## CP3411 (mixed-shape arithmetic)

Adding a `string` to a `number` — in either order — is a compile error (`CP3411`), as is arithmetic that mixes `number` with native integer values in the rejected modes. Interpolate with a template literal or convert explicitly; use `parseTo<T>` / `as T` at native boundaries. See [Global functions](./global-functions.md) and the [types guide](../../language/01-variables-and-bindings/02-types.md).

## Related pages

- [Global functions](./global-functions.md) — `parseInt`, `parseTo`, `isNaN`, `isFinite`, `structuredClone`
- [Native numeric behavior](../../language/07-native-types/03-numeric-rules.md)
- [Prefixed native types](../../language/07-native-types/02-prefixed-types.md)
