// =============================================================================
// src/std/number/sema_docs.cpp - documentation for the Number surface.
// =============================================================================
#include "detail/std_tables.hpp"

#include <string>
#include <vector>

namespace cp3 {

namespace {

const DocRow kNumberMemberRows[] = {
    {"toFixed", "toFixed(digits?: number): string",
     "Returns the number in fixed-point notation with `digits` digits after "
     "the decimal point (0 by default)."},
    {"toPrecision", "toPrecision(precision?: number): string",
     "Returns the number in fixed or exponential notation with `precision` "
     "significant digits."},
    {"toExponential", "toExponential(digits?: number): string",
     "Returns the number in exponential notation with `digits` digits after "
     "the decimal point."},
};

const DocRow kNumberStaticRows[] = {
    {"isInteger", "isInteger(value: number): boolean",
     "True when `value` is a finite number with no fractional part."},
    {"isFinite", "isFinite(value: number): boolean",
     "True when `value` is finite (not NaN or either infinity)."},
    {"isNaN", "isNaN(value: number): boolean",
     "True when `value` is NaN."},
    {"MAX_SAFE_INTEGER", "MAX_SAFE_INTEGER: number",
     "The largest integer exactly representable as a double (2^53 - 1)."},
    {"MIN_SAFE_INTEGER", "MIN_SAFE_INTEGER: number",
     "The smallest integer exactly representable as a double (-(2^53 - 1))."},
    {"EPSILON", "EPSILON: number",
     "The difference between 1 and the next representable double."},
    {"MAX_VALUE", "MAX_VALUE: number", "The largest finite double."},
    {"MIN_VALUE", "MIN_VALUE: number", "The smallest positive double."},
    {"POSITIVE_INFINITY", "POSITIVE_INFINITY: number", "Positive infinity."},
    {"NEGATIVE_INFINITY", "NEGATIVE_INFINITY: number", "Negative infinity."},
    {"NaN", "NaN: number", "The not-a-number value."},
};

}  // namespace

StdMemberDoc numberMemberDoc(const std::string& prop) {
    return memberDoc(kNumberMemberRows, prop);
}

StdMemberDoc numberStaticMemberDoc(const std::string& prop) {
    return memberDoc(kNumberStaticRows, prop);
}

std::vector<std::string> numberMemberNames() {
    return docRowNames(kNumberMemberRows);
}

std::vector<std::string> numberStaticMemberNames() {
    return docRowNames(kNumberStaticRows);
}

}  // namespace cp3
