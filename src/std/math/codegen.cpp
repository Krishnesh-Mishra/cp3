// =============================================================================
// src/std/math/codegen.cpp - `Math.*` call/property lowering.
//
// The variadic `min`/`max` go through the runtime's initializer_list overload;
// everything else is a 1:1 name. The `Math.PI`/`Math.E` property reads lower
// in emitMathMember below; the namespace dispatch stays in
// emit/std/common.cpp's emitBuiltinMember.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/int/builtin_tables.h"
#include "detail/generator/cpp_writer.h"

namespace cp3 {

using namespace detail;

namespace {

// Math (docs §04): the variadic `min`/`max` go through the runtime's
// initializer_list overload; everything else is a 1:1 name.
const char* const kMathUnary[] = {
    "sqrt", "abs",  "floor", "ceil",  "round", "trunc", "cbrt",
    "log",  "log2", "log10", "sin",   "cos",   "tan",   "asin",
    "acos", "atan", "sinh",  "cosh",  "tanh",  "asinh", "acosh",
    "atanh", "exp", "log1p", "sign"};
bool isMathFn(const std::string& name) {
    return name == "random" || name == "pow" || name == "min" || name == "max" ||
           name == "gcd" || name == "lcm" || name == "clamp" ||
           name == "lerp" || name == "atan2" || name == "hypot" ||
           CP3_IN_TABLE(name, kMathUnary);
}

}  // namespace

std::string Generator::emitMathCall(const CallExpr& c, const MemberExpr& m) {
    if (!isMathFn(m.property)) return std::string();
    needsMath_ = true;
    // A native result type (argument-driven narrowing, src/std/math/sema.cpp)
    // casts the double runtime result back to that native type.
    const Type resultType = effectiveType(c);
    CppInfo resultInfo = classify(resultType);
    std::string call;
    const std::vector<std::string> args = emitPlainArgs(c);
    if (m.property == "min" || m.property == "max" || m.property == "hypot") {
        // The runtime pair takes an initializer_list<double>; the braces make
        // this a list-initialization context, so each element is forced to
        // double (an int-looking spelling would be a narrowing conversion).
        std::string list;
        for (const std::string& arg : args) {
            if (!list.empty()) list += ", ";
            list += "static_cast<double>(" + arg + ")";
        }
        call = "::cp3::runtime::math::" + m.property + "({" + list + "})";
    } else {
        call = "::cp3::runtime::math::" + m.property + "(" +
               joinStrings(args, ", ") + ")";
    }
    if (resultType.kind == TypeKind::Native && resultInfo.representable) {
        return "static_cast<" + resultInfo.cpp + ">(" + call + ")";
    }
    return call;
}

// The two `Math` property reads. Returns "" when `m` is not one of them
// (defensive: the closed Math surface is rejected in sema as CP3305 first).
std::string Generator::emitMathMember(const MemberExpr& m) {
    if (!isNamespaceObject(m.object.get(), "Math")) return std::string();
    if (m.property == "PI") {
        needsMath_ = true;
        return "::cp3::runtime::math::kPi";
    }
    if (m.property == "E") {
        needsMath_ = true;
        return "::cp3::runtime::math::kE";
    }
    if (m.property == "LN2") {
        needsMath_ = true;
        return "::cp3::runtime::math::kLn2";
    }
    if (m.property == "LN10") {
        needsMath_ = true;
        return "::cp3::runtime::math::kLn10";
    }
    if (m.property == "LOG2E") {
        needsMath_ = true;
        return "::cp3::runtime::math::kLog2e";
    }
    if (m.property == "LOG10E") {
        needsMath_ = true;
        return "::cp3::runtime::math::kLog10e";
    }
    if (m.property == "SQRT1_2") {
        needsMath_ = true;
        return "::cp3::runtime::math::kSqrt1_2";
    }
    if (m.property == "SQRT2") {
        needsMath_ = true;
        return "::cp3::runtime::math::kSqrt2";
    }
    return std::string();
}

}  // namespace cp3
