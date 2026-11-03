// =============================================================================
// src/std/math/sema.cpp - `std/math` member typing and documentation.
//
// OWNERSHIP: the std/math family (src/std/math/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. Math is
// a CLOSED family (std_check.cpp isClosedStdNamespace): a member outside the
// table is CP3305 with these names as candidates, never an untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, Math section): the
// helpers are backed directly by `<numeric>`/`<cmath>`, and `gcd`/`lcm`
// operate on `number` (a 64-bit double), not on a true integer type.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type numberValue() { return Type::number(); }

// `(...values: number[]) => number` - at least one argument.
Type variadicNumberToNumber() {
    Type fn = Type::function({Type::number()}, Type::number());
    fn.name = kVariadicLabel;
    return fn;
}

// `(x: number) => number`.
Type numberUnary() { return Type::function({Type::number()}, Type::number()); }

Type twoNumberToNumber() {
    return Type::function({Type::number(), Type::number()}, Type::number());
}
Type threeNumberToNumber() {
    return Type::function({Type::number(), Type::number(), Type::number()},
                          Type::number());
}

const MemberRow kMathRows[] = {
    {"PI", numberValue, "PI: number",
     "The ratio of a circle's circumference to its diameter."},
    {"E", numberValue, "E: number",
     "Euler's number - the base of the natural logarithm."},
    {"random", zeroToNumber, "random(): number",
     "Returns a pseudo-random number in the half-open interval [0, 1)."},
    {"min", variadicNumberToNumber, "min(...values: number[]): number",
     "Returns the smallest of the given values."},
    {"max", variadicNumberToNumber, "max(...values: number[]): number",
     "Returns the largest of the given values."},
    {"pow", twoNumberToNumber, "pow(base: number, exponent: number): number",
     "Returns `base` raised to `exponent`."},
    {"gcd", twoNumberToNumber, "gcd(a: number, b: number): number",
     "Returns the greatest common divisor of `a` and `b`.\n\n"
     "`number` is a 64-bit double, so this is an exact-integer gcd only for "
     "values a double represents exactly."},
    {"lcm", twoNumberToNumber, "lcm(a: number, b: number): number",
     "Returns the least common multiple of `a` and `b`."},
    {"clamp", threeNumberToNumber,
     "clamp(x: number, min: number, max: number): number",
     "Returns `x` limited to the range [`min`, `max`]."},
    {"lerp", threeNumberToNumber,
     "lerp(start: number, end: number, t: number): number",
     "Returns the linear interpolation between `start` and `end` at `t` "
     "(`t` = 0 gives `start`, `t` = 1 gives `end`)."},
    {"sqrt", numberUnary, "sqrt(x: number): number",
     "Returns the square root of `x`."},
    {"abs", numberUnary, "abs(x: number): number",
     "Returns the absolute value of `x`."},
    {"floor", numberUnary, "floor(x: number): number",
     "Returns the largest integer not greater than `x`."},
    {"ceil", numberUnary, "ceil(x: number): number",
     "Returns the smallest integer not less than `x`."},
    {"round", numberUnary, "round(x: number): number",
     "Returns `x` rounded to the nearest integer."},
    {"trunc", numberUnary, "trunc(x: number): number",
     "Returns `x` with its fractional part removed."},
    {"cbrt", numberUnary, "cbrt(x: number): number",
     "Returns the cube root of `x`."},
    {"log", numberUnary, "log(x: number): number",
     "Returns the natural logarithm of `x`."},
    {"log2", numberUnary, "log2(x: number): number",
     "Returns the base-2 logarithm of `x`."},
    {"log10", numberUnary, "log10(x: number): number",
     "Returns the base-10 logarithm of `x`."},
    {"sin", numberUnary, "sin(x: number): number",
     "Returns the sine of `x` (in radians)."},
    {"cos", numberUnary, "cos(x: number): number",
     "Returns the cosine of `x` (in radians)."},
    {"tan", numberUnary, "tan(x: number): number",
     "Returns the tangent of `x` (in radians)."},
    {"asin", numberUnary, "asin(x: number): number",
     "Returns the arcsine of `x`, in radians."},
    {"acos", numberUnary, "acos(x: number): number",
     "Returns the arccosine of `x`, in radians."},
    {"atan", numberUnary, "atan(x: number): number",
     "Returns the arctangent of `x`, in radians."},
    {"atan2", twoNumberToNumber, "atan2(y: number, x: number): number",
     "Returns the angle in the plane between the positive x-axis and the ray "
     "to `(x, y)`, in radians."},
    {"sinh", numberUnary, "sinh(x: number): number",
     "Returns the hyperbolic sine of `x`."},
    {"cosh", numberUnary, "cosh(x: number): number",
     "Returns the hyperbolic cosine of `x`."},
    {"tanh", numberUnary, "tanh(x: number): number",
     "Returns the hyperbolic tangent of `x`."},
    {"asinh", numberUnary, "asinh(x: number): number",
     "Returns the inverse hyperbolic sine of `x`."},
    {"acosh", numberUnary, "acosh(x: number): number",
     "Returns the inverse hyperbolic cosine of `x`."},
    {"atanh", numberUnary, "atanh(x: number): number",
     "Returns the inverse hyperbolic tangent of `x`."},
    {"exp", numberUnary, "exp(x: number): number",
     "Returns e raised to the power `x`."},
    {"log1p", numberUnary, "log1p(x: number): number",
     "Returns the natural logarithm of `1 + x`, accurate for small `x`."},
    {"hypot", variadicNumberToNumber, "hypot(...values: number[]): number",
     "Returns the square root of the sum of squares of the arguments."},
    {"sign", numberUnary, "sign(x: number): number",
     "Returns -1, 0, or 1 according to the sign of `x` (NaN stays NaN)."},
    {"LN2", numberValue, "LN2: number",
     "The natural logarithm of 2."},
    {"LN10", numberValue, "LN10: number",
     "The natural logarithm of 10."},
    {"LOG2E", numberValue, "LOG2E: number",
     "The base-2 logarithm of Euler's number."},
    {"LOG10E", numberValue, "LOG10E: number",
     "The base-10 logarithm of Euler's number."},
    {"SQRT1_2", numberValue, "SQRT1_2: number",
     "The square root of one half."},
    {"SQRT2", numberValue, "SQRT2: number",
     "The square root of 2."},
};

}  // namespace

std::optional<Type> Analyzer::mathMember(const std::string& prop) {
    return lookupMember(kMathRows, prop);
}

std::vector<std::string> mathMemberNames() { return memberNames(kMathRows); }

StdMemberDoc mathMemberDoc(const std::string& prop) {
    return memberDoc(kMathRows, prop);
}

}  // namespace cp3
