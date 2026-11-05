// =============================================================================
// src/language/number/codegen.cpp - lowering for the language-level Number surface.
//
// `(1.234).toFixed(2)` -> cp3::runtime::numbers::toFixed(receiver, 2.0)
// `Number.isInteger(x)` -> cp3::runtime::numbers::isInteger(x)
// `Number.MAX_SAFE_INTEGER` -> the runtime constant.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"

#include "detail/int/builtin_tables.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

const char* const kNumberMethods[] = {"toFixed", "toPrecision",
                                      "toExponential", "toString"};

const char* numberConstant(const std::string& prop) {
    if (prop == "MAX_SAFE_INTEGER") return "kMaxSafeInteger";
    if (prop == "MIN_SAFE_INTEGER") return "kMinSafeInteger";
    if (prop == "EPSILON") return "kEpsilon";
    if (prop == "MAX_VALUE") return "kMaxValue";
    if (prop == "MIN_VALUE") return "kMinValue";
    if (prop == "POSITIVE_INFINITY") return "kPositiveInfinity";
    if (prop == "NEGATIVE_INFINITY") return "kNegativeInfinity";
    if (prop == "NaN") return "kNaN";
    return nullptr;
}

}  // namespace

std::string Generator::emitNumberMethodCall(const CallExpr& c,
                                            const MemberExpr& m) {
    if (!CP3_IN_TABLE(m.property, kNumberMethods)) return std::string();
    needsNumber_ = true;
    std::string argument;
    if (m.property == "toFixed") {
        argument = c.arguments.empty() ? "0.0"
                                       : (c.arguments.front()
                                              ? expr(*c.arguments.front())
                                              : "0.0");
    } else if (m.property == "toPrecision") {
        argument = c.arguments.empty() ? "6.0"
                                       : (c.arguments.front()
                                              ? expr(*c.arguments.front())
                                              : "6.0");
    } else if (m.property == "toString") {
        argument = c.arguments.empty() ? "10.0"
                                       : (c.arguments.front()
                                              ? expr(*c.arguments.front())
                                              : "10.0");
    } else {
        argument = c.arguments.empty() ? "6.0"
                                       : (c.arguments.front()
                                              ? expr(*c.arguments.front())
                                              : "6.0");
    }
    return "::cp3::runtime::numbers::" + m.property + "(" +
           expr(*m.object) + ", " + argument + ")";
}

std::string Generator::emitNumberStaticCall(const CallExpr& c,
                                            const MemberExpr& m) {
    if (m.property != "isInteger" && m.property != "isFinite" &&
        m.property != "isNaN" && m.property != "isSafeInteger" &&
        m.property != "parseInt" && m.property != "parseFloat") {
        return std::string();
    }
    needsNumber_ = true;
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: Number." + m.property +
                               "() takes exactly one argument");
        return "0.0";
    }
    return "::cp3::runtime::numbers::" + m.property + "(" +
           expr(*c.arguments.front()) + ")";
}

std::string Generator::emitNumberMember(const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "Number") ||
        hasValueBinding("Number")) {
        return std::string();
    }
    const char* name = numberConstant(m.property);
    if (name == nullptr) return std::string();
    needsNumber_ = true;
    return "::cp3::runtime::numbers::" + std::string(name);
}

}  // namespace cp3
