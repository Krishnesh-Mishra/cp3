// =============================================================================
// src/codegen/src/emit/template.cpp - template interpolation and `typeof`
// (STYLE.md section 8).
//
// Both lowerings carry an ordering/value-shape rule that is easiest to read on
// its own:
//
//   * a template with two or more holes chains `append` calls so every part
//     evaluates in source order (C++ does not order `+` operands);
//   * `typeof` reports the runtime shape of the value, including the regex
//     reference type and the two nullish option flavors.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>

#include "detail/cpp_writer.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

std::string Generator::emitTemplate(const TemplateLiteral& tl) {
    // With two or more interpolations the parts cannot stay a chain of `+`:
    // C++ leaves an operator's operand order unspecified (`${c()} ${c()} ${c()}`
    // could run right-to-left). A chain of `append` calls is ordered by the
    // language - each call's postfix-expression is sequenced before its
    // argument - so every part evaluates in source order.
    if (tl.expressions.size() >= 2) {
        std::string out;
        for (std::size_t i = 0; i < tl.quasis.size(); ++i) {
            const std::string cooked =
                tl.quasis[i] ? tl.quasis[i]->cooked : std::string();
            const std::string part =
                "std::string(" + cppStringLiteral(cooked) + ")";
            out += out.empty() ? part : ".append(" + part + ")";
            if (i < tl.expressions.size()) {
                out += ".append(" + tsCall(*tl.expressions[i]) + ")";
            }
        }
        return out.empty() ? "std::string()" : "(" + out + ")";
    }
    std::string out;
    for (std::size_t i = 0; i < tl.quasis.size(); ++i) {
        const std::string cooked = tl.quasis[i] ? tl.quasis[i]->cooked : std::string();
        out += (out.empty() ? "" : " + ");
        out += "std::string(" + cppStringLiteral(cooked) + ")";
        if (i < tl.expressions.size()) {
            out += " + " + tsCall(*tl.expressions[i]);
        }
    }
    if (out.empty()) {
        out = "std::string()";
    }
    return "(" + out + ")";
}

std::string Generator::emitTypeof(const UnaryExpr& u) {
    const CppInfo ci = info(*u.operand);
    // A regex value is a reference (object) type: `typeof r === "object"`
    // (D16; the shape itself is Unknown, so the switch cannot decide it).
    if (ci.type.kind == TypeKind::Class && ci.type.name == "RegExp") {
        return "std::string(\"object\")";
    }
    switch (ci.shape) {
        case Shape::Number: return "std::string(\"number\")";
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
            if (ci.shape == Shape::Maybe) {
                return "([&]{ const auto& cp3_v = (" + expr(*u.operand) +
                       "); if (cp3_v.isNull()) return std::string(\"object\"); "
                       "if (cp3_v.isUndefined()) return "
                       "std::string(\"undefined\"); return "
                       "::cp3::runtime::typeOf(cp3_v.value()); }())";
            }
            const char* empty =
                (ci.emptyKind == NullishKind::Null) ? "object" : "undefined";
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
