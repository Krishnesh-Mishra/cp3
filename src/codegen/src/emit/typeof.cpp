// =============================================================================
// src/codegen/src/emit/typeof.cpp - `typeof` lowering (STYLE.md §8).
//
// A `typeof` over a primitive value is a compile-time string; a primitive
// union reports the held alternative, and a nullish option inspects its slot
// (the empty flavor is "object" for null, "undefined" otherwise). Native
// integers and floats report "number" (D65). Out-of-scope operands report
// CP3002 and answer "undefined".
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

std::string Generator::emitTypeof(const UnaryExpr& u) {
    const CppInfo ci = info(*u.operand);
    // A regex value is a reference (object) type: `typeof r === "object"`
    // (D16; the shape itself is Unknown, so the switch cannot decide it).
    if (ci.type.kind == TypeKind::Class && ci.type.name == "RegExp") {
        return "std::string(\"object\")";
    }
    switch (ci.shape) {
        case Shape::Number: return "std::string(\"number\")";
        case Shape::Native: return "std::string(\"number\")";
        case Shape::String: return "std::string(\"string\")";
        case Shape::Bool: return "std::string(\"boolean\")";
        case Shape::Undefined: return "std::string(\"undefined\")";
        case Shape::Null: return "std::string(\"object\")";
        case Shape::Object: return "std::string(\"object\")";
        case Shape::Union:
            // A primitive union reports the held alternative.
            return "::cp3::runtime::unions::typeOf(" + expr(*u.operand) + ")";
        case Shape::Optional:
        case Shape::Maybe: {
            // A nullish option over a primitive: the empty slot is `null`
            // ("object") or `undefined`, and `Maybe` stores which.
            Type inner;
            bool found = false;
            for (const Type& part : ci.type.parts) {
                if (!part.isNullish()) {
                    inner = part;
                    found = true;
                    break;
                }
            }
            if (!found || !isPrimitiveShape(classify(inner).shape)) {
                notImplemented(u, "typeof of a non-primitive value");
                return "std::string(\"undefined\")";
            }
            const bool innerNative = classify(inner).shape == Shape::Native;
            if (ci.shape == Shape::Maybe) {
                if (innerNative) {
                    return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
                           "); if (cp3_v.isNull()) return "
                           "std::string(\"object\"); if (cp3_v.isUndefined()) "
                           "return std::string(\"undefined\"); return "
                           "std::string(\"number\"); }())";
                }
                return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
                       "); if (cp3_v.isNull()) return std::string(\"object\"); "
                       "if (cp3_v.isUndefined()) return "
                       "std::string(\"undefined\"); return "
                       "::cp3::runtime::typeOf(cp3_v.value()); }())";
            }
            const char* empty =
                (ci.emptyKind == NullishKind::Null) ? "object" : "undefined";
            if (innerNative) {
                return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
                       "); return cp3_v.has_value() ? std::string(\"number\") "
                       ": std::string(\"" + empty + "\"); }())";
            }
            return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
                   "); return cp3_v.has_value() ? ::cp3::runtime::typeOf(*cp3_v) "
                   ": std::string(\"" + empty + "\"); }())";
        }
        default:
            notImplemented(u, "typeof of a non-primitive value");
            return "std::string(\"undefined\")";
    }
}

}  // namespace cp3
