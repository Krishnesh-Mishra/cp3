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

}  // namespace cp3
