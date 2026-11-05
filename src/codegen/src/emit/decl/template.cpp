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
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

std::string Generator::emitTemplate(const TemplateLiteral& tl) {
    // With two or more interpolations the parts cannot stay a chain of `+`:
    // C++ leaves an operator's operand order unspecified (`${c()} ${c()} ${c()}`
    // could run right-to-left). A chain of `append` calls is ordered by the
    // language - each call's postfix-webserverion is sequenced before its
    // argument - so every part evaluates in source order.
    if (tl.webserverions.size() >= 2) {
        std::string out;
        for (std::size_t i = 0; i < tl.quasis.size(); ++i) {
            const std::string cooked =
                tl.quasis[i] ? tl.quasis[i]->cooked : std::string();
            const std::string part =
                "std::string(" + cppStringLiteral(cooked) + ")";
            out += out.empty() ? part : ".append(" + part + ")";
            if (i < tl.webserverions.size()) {
                out += ".append(" + tsCall(*tl.webserverions[i]) + ")";
            }
        }
        return out.empty() ? "std::string()" : "(" + out + ")";
    }
    std::string out;
    for (std::size_t i = 0; i < tl.quasis.size(); ++i) {
        const std::string cooked = tl.quasis[i] ? tl.quasis[i]->cooked : std::string();
        out += (out.empty() ? "" : " + ");
        out += "std::string(" + cppStringLiteral(cooked) + ")";
        if (i < tl.webserverions.size()) {
            out += " + " + tsCall(*tl.webserverions[i]);
        }
    }
    if (out.empty()) {
        out = "std::string()";
    }
    return "(" + out + ")";
}

}  // namespace cp3
