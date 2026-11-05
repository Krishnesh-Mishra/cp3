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

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

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

// -------------------------------------------------------------------------
// Argument-driven result typing
// ---------------------------------------------------------------------------
// `number` stays the result for `number` arguments (JS parity; `r_'f64'`
// is `number`). A native argument narrows the result instead:
//   * integral/float/rounding helpers (`abs`, `sign`, `floor`, `ceil`,
//     `trunc`, `round`, `min`, `max`, `hypot`, `clamp`, `lerp`, `gcd`,
//     `lcm`) keep the native kind - an all-`f32` call stays `f32`, an
//     all-integral call stays its own native type (mixed natives use the
//     usual arithmetic common type);
//   * fractional helpers (`sqrt`, `cbrt`, `log*`, `sin`/`cos`/`tan`, the
//     inverse and hyperbolic forms, `exp`, `log1p`, `pow`, `atan2`) stay
//     `f32` only for all-`f32` arguments and widen to `number` otherwise
//     (an integer has no fractional representation here).
// Any `number` argument widens the whole call to `number`, exactly as
// native arithmetic does. The runtime helpers are still double-based;
// codegen casts the result back to the narrowed type.

namespace {

// The three numeric classes a Math argument can carry.
enum class MathClass { Number, NativeInt, NativeFloat, Other };

MathClass mathClassOf(const Type& t) {
    if (t.kind == TypeKind::Number || t.kind == TypeKind::Literal) {
        return MathClass::Number;
    }
    if (isNativeType(t)) {
        return isNativeFloat(t) ? MathClass::NativeFloat
                                : MathClass::NativeInt;
    }
    return MathClass::Other;
}

bool isFractionalMathFn(const std::string& prop) {
    static const char* const kFractional[] = {
        "sqrt", "cbrt", "pow",  "log",  "log2",  "log10", "sin", "cos",
        "tan",  "asin", "acos", "atan", "atan2", "sinh",  "cosh", "tanh",
        "asinh", "acosh", "atanh", "exp", "log1p"};
    for (const char* name : kFractional) {
        if (prop == name) return true;
    }
    return false;
}

std::optional<Type> mathResultType(const std::string& prop,
                                   const std::vector<Type>& args) {
    bool anyNumber = false;
    bool anyInt = false;
    bool anyFloat = false;
    Type commonNative = Type::unknown();
    for (const Type& arg : args) {
        switch (mathClassOf(arg)) {
            case MathClass::Number: anyNumber = true; break;
            case MathClass::NativeInt: {
                anyInt = true;
                if (commonNative.isUnknown()) {
                    commonNative = arg;
                } else if (auto common = nativeArithmeticResult(commonNative, arg)) {
                    commonNative = *common;
                } else {
                    return Type::number();
                }
                break;
            }
            case MathClass::NativeFloat: anyFloat = true; break;
            case MathClass::Other: return std::nullopt;
        }
    }
    if (anyNumber) return Type::number();
    if (isFractionalMathFn(prop)) {
        if (anyFloat && !anyInt) return Type::native("f32");
        return Type::number();
    }
    // Integral-preserving helpers keep the native kind.
    if (anyFloat && !anyInt) return Type::native("f32");
    if (anyInt && !anyFloat) return commonNative;
    if (anyFloat && anyInt) return Type::native("f32");
    return Type::number();
}

}  // namespace

bool Analyzer::tryInferMathCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Math") {
        return false;
    }
    static const char* const kMathFns[] = {
        "abs",    "acos",   "acosh",  "asin",  "asinh",  "atan",   "atan2",
        "atanh",  "cbrt",   "ceil",   "clamp", "cos",    "cosh",   "exp",
        "floor",  "gcd",    "hypot",  "lcm",   "lerp",   "log",    "log10",
        "log1p",  "log2",   "max",    "min",   "pow",    "random", "round",
        "sign",   "sin",    "sinh",   "sqrt",  "tan",    "tanh",   "trunc"};
    bool known = false;
    for (const char* name : kMathFns) {
        if (m.property == name) known = true;
    }
    if (!known) return false;
    Binding mathBinding;
    if (lookup("Math", mathBinding) && !mathBinding.isAmbient) {
        return false;  // a user `Math` shadows the std/math surface
    }
    std::vector<Type> argTypes;
    for (auto& arg : call.arguments) {
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*arg);
            Type at = sp.argument ? inferExpr(*sp.argument) : Type::unknown();
            argTypes.push_back(at.kind == TypeKind::Array ? elemOf(at)
                                                          : Type::unknown());
            continue;
        }
        argTypes.push_back(inferExpr(*arg));
    }
    if (m.property == "random") {
        out = withType(call, Type::number());
        return true;
    }
    // Non-numeric arguments are rejected exactly like the table signature
    // (`number` params) rejects them.
    bool bad = false;
    for (std::size_t i = 0; i < argTypes.size(); ++i) {
        const Type& t = argTypes[i];
        if (mathClassOf(t) == MathClass::Other && !t.isUnknown()) {
            if (!bad) {
                report(diagcode::kTypeMismatch,
                       call.arguments[i] ? call.arguments[i]->span : call.span,
                       "type mismatch: Math." + m.property + "'s argument must "
                       "be a number or a native numeric value");
                bad = true;
            }
        }
    }
    if (bad) {
        out = withType(call, Type::unknown());
        return true;
    }
    Type result = Type::number();
    if (auto computed = mathResultType(m.property, argTypes)) {
        result = *computed;
    }
    out = withType(call, result);
    return true;
}

std::vector<std::string> mathMemberNames() { return memberNames(kMathRows); }

StdMemberDoc mathMemberDoc(const std::string& prop) {
    return memberDoc(kMathRows, prop);
}

}  // namespace cp3
