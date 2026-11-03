---
title: Math
chapter: Chapter 3 · Standard library
---
# Math

`std/math` is a default namespace import for numeric constants and functions:

```ts
import math from "std/math";
```

The CP3 `number` type is a 64-bit floating-point value. A numeric helper may return an integer-valued number, but the type system does not turn it into a fixed-width integer. See [Numeric types](../01-language/01-variables-and-bindings/02-types.md).

## Constants and random values

- `math.PI` is the ratio of a circle's circumference to its diameter.
- `math.E` is Euler's number, the base of natural logarithms.
- `math.random()` returns a pseudo-random number in the half-open interval `[0, 1)`.

The random generator is not a cryptographic random source. Use `std/crypto.randomBytes` for cryptographic bytes.

## Selection and powers

- `min(...values)` and `max(...values)` select the smallest or largest supplied number.
- `pow(base, exponent)` computes exponentiation.
- `clamp(x, min, max)` limits a value to the supplied range.
- `lerp(start, end, t)` computes linear interpolation; `t = 0` gives `start`, and `t = 1` gives `end`.

For `clamp`, supply the lower bound before the upper bound. For `lerp`, values of `t` outside `[0, 1]` extrapolate according to the same formula.

## Integer-like helpers

`gcd(a, b)` and `lcm(a, b)` operate on `number`. Since `number` is a double, exact integer behavior is only reliable while the values are exactly representable and within the implementation's supported range. Do not use them as arbitrary-precision integer operations.

## Rounding

| Function | Rule |
| --- | --- |
| `floor(x)` | Largest integer not greater than `x` |
| `ceil(x)` | Smallest integer not less than `x` |
| `round(x)` | Round to nearest integer according to the runtime operation |
| `trunc(x)` | Remove the fractional part toward zero |

For negative values, `floor` and `trunc` differ: `floor(-1.2)` is `-2`, while `trunc(-1.2)` is `-1`.

## Roots, logarithms, and trigonometry

`abs`, `sqrt`, and `cbrt` provide absolute value, square root, and cube root. `log`, `log2`, and `log10` compute natural, base-2, and base-10 logarithms. `sin`, `cos`, and `tan` take radians.

The runtime follows the underlying floating-point math library for special values, domain errors, and precision. If an application needs a specific validation policy, check its inputs and outputs explicitly.

## Function index

`PI`, `E`, `random`, `min`, `max`, `pow`, `gcd`, `lcm`, `clamp`, `lerp`, `sqrt`, `abs`, `floor`, `ceil`, `round`, `trunc`, `cbrt`, `log`, `log2`, `log10`, `sin`, `cos`, and `tan` are the documented members. The compiler treats this namespace as closed and diagnoses unsupported names.

## Full function index

| Member | Signature | Notes |
| --- | --- | --- |
| `PI`, `E` | `number` | Numeric constants |
| `random` | `random(): number` | Pseudorandom value in `[0, 1)` |
| `min`, `max` | `(...values: number[]): number` | Variadic selection |
| `pow` | `(base: number, exponent: number): number` | Exponentiation |
| `gcd`, `lcm` | `(a: number, b: number): number` | Integer-oriented helpers over `number` inputs |
| `clamp` | `(x: number, min: number, max: number): number` | Restrict a value to a range |
| `lerp` | `(start: number, end: number, t: number): number` | Linear interpolation/extrapolation |
| `sqrt`, `abs`, `floor`, `ceil`, `round`, `trunc`, `cbrt`, `log`, `log2`, `log10`, `sin`, `cos`, `tan` | `(x: number): number` | Unary floating-point operations |

The namespace is closed to the listed members. Use CP3 `number` for these functions; native integer values have separate arithmetic rules and should be deliberately placed at conversion boundaries.

## Floating-point boundaries

Math functions operate on floating-point `number`. A mathematically integral result is still a floating-point value and can lose precision for sufficiently large integers. `gcd` and `lcm` are convenient for ordinary exactly represented integers, but they are not arbitrary-precision number-theory functions.

`floor` rounds toward negative infinity; `trunc` removes the fractional part toward zero. For positive inputs these often agree, but for negative fractions they differ. `ceil` rounds toward positive infinity. Use a native integer type when exact fixed-width integer arithmetic, rather than floating-point math, is the requirement.

## Interpolation and clamping

For `lerp(start, end, t)`, `t = 0` returns the start and `t = 1` returns the end. Values outside that interval extrapolate. Clamp `t` first if the product contract requires interpolation only within the endpoints.

`clamp(x, min, max)` expects the caller to supply an ordered lower and upper bound. Validate dynamic bounds before calling if reversed limits would be a bug in the application; do not assume this helper sorts them for you.

## Angle units and special values

`sin`, `cos`, and `tan` take radians. Convert degrees at the call boundary when that is the source format. Domain behavior and rounding precision follow the runtime's native floating-point implementation; validate inputs when the application needs a stricter error policy.

`math.random()` is not cryptographically secure. It must not generate passwords, keys, authentication tokens, or security-sensitive identifiers. Use `std/crypto` for cryptographically strong bytes and UUID generation.

## Choosing a numeric representation

Use `std/math` for general floating-point calculations. Use native integer types for exact fixed-width arithmetic, and use typed arrays when you need dense fixed-width storage. Do not repeatedly convert a large native integer to `number` and back if low-bit precision matters.

## Related pages

- [Types and inference](../01-language/01-variables-and-bindings/02-types.md)
- [Native numeric types](../01-language/07-native-types/03-numeric-rules.md)
- [Cryptography](10-crypto.md) for secure random APIs
- [Typed arrays](16-typed-arrays.md) for fixed-width numeric storage
