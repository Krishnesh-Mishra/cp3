// =============================================================================
// src/codegen/src/emit/std/math.cpp - `Math.*` call/property lowering.
//
// The variadic `min`/`max` go through the runtime's initializer_list overload;
// everything else is a 1:1 name. `Math.PI`/`Math.E` lower in
// emit/std/common.cpp's emitBuiltinMember.
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <vector>

#include "detail/builtin_tables.h"
#include "detail/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// Math (docs §04): the variadic `min`/`max` go through the runtime's
// initializer_list overload; everything else is a 1:1 name.
const char* const kMathUnary[] = {"sqrt", "abs",  "floor", "ceil", "round",
                                  "trunc", "cbrt", "log",   "log2", "log10",
                                  "sin",  "cos",  "tan"};
bool isMathFn(const std::string& name) {
    return name == "random" || name == "pow" || name == "min" || name == "max" ||
           name == "gcd" || name == "lcm" || name == "clamp" ||
           name == "lerp" || CP3_IN_TABLE(name, kMathUnary);
}

}  // namespace

std::string Generator::emitMathCall(const CallExpr& c, const MemberExpr& m) {
    if (!isMathFn(m.property)) return std::string();
    needsMath_ = true;
    const std::vector<std::string> args = emitPlainArgs(c);
    if (m.property == "min" || m.property == "max") {
        // The runtime pair takes an initializer_list<double>; the braces make
        // this a list-initialization context, so each element is forced to
        // double (an int-looking spelling would be a narrowing conversion).
        std::string list;
        for (const std::string& arg : args) {
            if (!list.empty()) list += ", ";
            list += "static_cast<double>(" + arg + ")";
        }
        return "::cp3::runtime::math::" + m.property + "({" + list + "})";
    }
    return "::cp3::runtime::math::" + m.property + "(" +
           joinStrings(args, ", ") + ")";
}

}  // namespace cp3
