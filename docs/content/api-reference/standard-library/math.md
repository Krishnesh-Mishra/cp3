---
title: Math
---
# Math

`std/math` provides numeric constants and functions. Import the default namespace:

```ts
import math from "std/math";
```

CP3 `number` is a 64-bit floating-point value; the namespace is closed to the members below. `math.random()` is not cryptographically secure — use `std/crypto` for secret material (see [Cryptography](./crypto.md)).

## Constants

| Member | Value meaning | Notes |
| --- | --- | --- |
| `PI` | Ratio of circumference to diameter | `number` |
| `E` | Euler's number (base of natural logarithms) | `number` |
| `LN2` | Natural logarithm of 2 | `number` |
| `LN10` | Natural logarithm of 10 | `number` |
| `LOG2E` | Base-2 logarithm of Euler's number | `number` |
| `LOG10E` | Base-10 logarithm of Euler's number | `number` |
| `SQRT1_2` | Square root of one half | `number` |
| `SQRT2` | Square root of 2 | `number` |

## Selection and powers

| Member | Signature | Notes |
| --- | --- | --- |
| `random` | `random(): number` | Pseudorandom value in `[0, 1)` |
| `min`, `max` | `(...values: number[]): number` | Variadic selection |
| `pow` | `pow(base: number, exponent: number): number` | Exponentiation |
| `clamp` | `clamp(x: number, min: number, max: number): number` | Restricts `x` to `[min, max]`; bounds are not sorted for you |
| `lerp` | `lerp(start: number, end: number, t: number): number` | Linear interpolation; `t` outside `[0, 1]` extrapolates |
| `hypot` | `hypot(...values: number[]): number` | √(sum of squares), without intermediate overflow |
| `gcd`, `lcm` | `(a: number, b: number): number` | Integer-oriented helpers; exact only while values are exactly representable doubles |

## Rounding

| Member | Signature | Rule |
| --- | --- | --- |
| `floor` | `floor(x: number): number` | Largest integer not greater than `x` |
| `ceil` | `ceil(x: number): number` | Smallest integer not less than `x` |
| `round` | `round(x: number): number` | Nearest integer, per the runtime operation |
| `trunc` | `trunc(x: number): number` | Remove the fractional part toward zero |

`floor(-1.2)` is `-2`; `trunc(-1.2)` is `-1`.

## Roots, logarithms, trigonometry

| Member | Signature | Notes |
| --- | --- | --- |
| `sqrt`, `cbrt` | `(x: number): number` | Square / cube root |
| `abs` | `abs(x: number): number` | Absolute value |
| `log`, `log2`, `log10` | `(x: number): number` | Natural / base-2 / base-10 logarithm |
| `sin`, `cos`, `tan` | `(x: number): number` | Radians |
| `asin`, `acos`, `atan` | `(x: number): number` | Inverse trig, result in radians; `asin`/`acos` expect `[-1, 1]` input |
| `atan2` | `atan2(y: number, x: number): number` | Angle of the ray to `(x, y)`, in radians |
| `sinh`, `cosh`, `tanh` | `(x: number): number` | Hyperbolic trig |
| `asinh`, `acosh`, `atanh` | `(x: number): number` | Inverse hyperbolic trig; `atanh` expects `(-1, 1)` input |
| `exp` | `exp(x: number): number` | `E` raised to the power `x` |
| `log1p` | `log1p(x: number): number` | `ln(1 + x)`, accurate for small `x` |
| `sign` | `sign(x: number): number` | `-1`, `0`, or `1`; `NaN` stays `NaN` |

Domain behavior outside the expected ranges follows the runtime's floating-point math library. Special values and precision are governed by that library; check inputs and outputs when the application needs a stricter policy. A mathematically integral result is still a floating-point value; use native integer types for exact fixed-width arithmetic.

Behavior details: [guide](../../standard-library/07-math.md)
