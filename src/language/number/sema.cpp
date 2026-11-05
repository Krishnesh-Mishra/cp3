// =============================================================================
// src/language/number/sema.cpp - the language-level Number surface.
//
// Number methods (`(1.5).toFixed(2)`) and Number statics
// (`Number.isInteger(x)`, `Number.MAX_SAFE_INTEGER`) are part of the language,
// not an importable std module. Their typing lives here; lowering lives in the
// sibling codegen.cpp.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>

#include "detail/scopes.h"

namespace cp3 {

Type Analyzer::numberMember(const std::string& prop) {
    if (prop == "toFixed" || prop == "toPrecision" ||
        prop == "toExponential") {
        return withOptionalTail(
            Type::function({Type::number()}, Type::string()));
    }
    if (prop == "toString") {
        return withOptionalTail(
            Type::function({Type::number()}, Type::string()));
    }
    return ambient();
}

std::optional<Type> numberStaticMember(const std::string& prop) {
    if (prop == "isInteger" || prop == "isFinite" || prop == "isNaN" ||
        prop == "isSafeInteger") {
        return Type::function({Type::number()}, Type::boolean());
    }
    if (prop == "parseInt" || prop == "parseFloat") {
        return Type::function({Type::string()}, Type::number());
    }
    if (prop == "MAX_SAFE_INTEGER" || prop == "MIN_SAFE_INTEGER" ||
        prop == "EPSILON" || prop == "MAX_VALUE" || prop == "MIN_VALUE" ||
        prop == "POSITIVE_INFINITY" || prop == "NEGATIVE_INFINITY" ||
        prop == "NaN") {
        return Type::number();
    }
    return std::nullopt;
}

}  // namespace cp3
