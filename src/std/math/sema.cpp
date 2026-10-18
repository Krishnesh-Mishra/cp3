// =============================================================================
// src/std/math/sema.cpp - `std/math` member typing.
//
// OWNERSHIP: the std/math family (src/std/math/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing and the CP3305 candidate list, so the
// two can never drift apart. Math is a CLOSED family
// (std_check.cpp isClosedStdNamespace): a member outside the table is CP3305
// with these names as candidates instead of a CP3002 skip.
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
    {"PI", numberValue},             {"E", numberValue},
    {"random", zeroToNumber},        {"min", variadicNumberToNumber},
    {"max", variadicNumberToNumber}, {"pow", twoNumberToNumber},
    {"gcd", twoNumberToNumber},      {"lcm", twoNumberToNumber},
    {"clamp", threeNumberToNumber},  {"lerp", threeNumberToNumber},
    {"sqrt", numberUnary},           {"abs", numberUnary},
    {"floor", numberUnary},          {"ceil", numberUnary},
    {"round", numberUnary},          {"trunc", numberUnary},
    {"cbrt", numberUnary},           {"log", numberUnary},
    {"log2", numberUnary},           {"log10", numberUnary},
    {"sin", numberUnary},            {"cos", numberUnary},
    {"tan", numberUnary},
};

}  // namespace

std::optional<Type> Analyzer::mathMember(const std::string& prop) {
    return lookupMember(kMathRows, prop);
}

std::vector<std::string> mathMemberNames() { return memberNames(kMathRows); }

}  // namespace cp3
