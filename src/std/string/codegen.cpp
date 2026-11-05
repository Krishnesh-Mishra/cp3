// =============================================================================
// src/std/string/codegen.cpp - `string` method + literal-array
// lowering.
//
// The implemented `string` method surface (cp3/std/string.hpp) plus the literal
// array element set `["a", "b"].includes(x)`, which lowers to an equality
// chain; other array forms are handled by the array family.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The `string` method surface implemented this wave (cp3/std/string.hpp): `at`
// is JS charAt; `matchAll` has its own case below (it needs the regexp
// include, like `split`).
struct StringMethod {
    const char* js;  // the cp3 method name
    const char* fn;  // the runtime spelling
};
constexpr StringMethod kStringMethods[] = {
    {"charAt", "charAt"},       {"at", "charAt"},
    {"charCodeAt", "charCodeAt"}, {"codePointAt", "codePointAt"},
    {"startsWith", "startsWith"}, {"endsWith", "endsWith"},
    {"includes", "includes"},   {"repeat", "repeat"},
    {"padStart", "padStart"},   {"padEnd", "padEnd"},
    {"toUpperCase", "toUpperCase"}, {"toLowerCase", "toLowerCase"},
    {"trim", "trim"},           {"trimStart", "trimStart"},
    {"trimEnd", "trimEnd"},     {"indexOf", "indexOf"},
    {"lastIndexOf", "lastIndexOf"}, {"slice", "slice"},
    {"substring", "substring"}, {"replace", "replace"},
    {"replaceAll", "replaceAll"},
};

const char* stringRuntimeFn(const std::string& method) {
    for (const StringMethod& entry : kStringMethods) {
        if (method == entry.js) return entry.fn;
    }
    return nullptr;
}

}  // namespace

std::string Generator::emitStringStaticCall(const CallExpr& c,
                                            const MemberExpr& m) {
    if (m.property == "fromCharCode" || m.property == "fromCodePoint") {
        needsString_ = true;
        std::string list;
        for (const std::string& arg : emitPlainArgs(c)) {
            if (!list.empty()) list += ", ";
            list += "static_cast<double>(" + arg + ")";
        }
        return "::cp3::runtime::strings::" +
               std::string(m.property == "fromCharCode" ? "fromCharCode"
                                                         : "fromCodePoint") +
               "({" + list + "})";
    }
    if (m.property == "raw") {
        if (c.arguments.empty() || !c.arguments.front()) return std::string();
        const Expr* first = c.arguments.front().get();
        if (first->kind == NodeKind::SpreadElement) return std::string();
        needsString_ = true;
        if (c.arguments.size() == 1) {
            // `String.raw(chunk)` is the identity on one chunk; the
            // `Array<string>` form joins the quasis (see strings::raw).
            return "::cp3::runtime::strings::raw(" + expr(*first) + ")";
        }
        // `String.raw(parts, sub0, ...)`: the substitutions convert the way
        // template interpolation does (`tsCall`).
        std::string list;
        for (std::size_t i = 1; i < c.arguments.size(); ++i) {
            const Expr* sub = c.arguments[i].get();
            if (!sub || sub->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            if (!list.empty()) list += ", ";
            list += tsCall(*sub);
        }
        return "::cp3::runtime::strings::raw(" + expr(*first) + ", {" + list +
               "})";
    }
    return std::string();
}

std::string Generator::emitStringMethodCall(const CallExpr& c, const MemberExpr& m) {
    // Literal array element set: `["a", "b"].includes(x)` lowers to an equality
    // chain. Any other form on an array receiver is left to the array family
    // (src/language/arrays/codegen_methods.cpp) - never fall through to the string
    // method table.
    if (m.object && m.object->kind == NodeKind::ArrayLiteral) {
        if (m.property != "includes" || c.arguments.size() != 1) {
            return std::string();
        }
        const auto& array = static_cast<const ArrayLiteral&>(*m.object);
        Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        std::string list;
        for (const auto& elementPtr : array.elements) {
            const Expr* element = elementPtr.get();
            if (!element || element->kind == NodeKind::SpreadElement) {
                return std::string();
            }
            if (info(*element).shape != Shape::String) return std::string();
            if (!list.empty()) list += ", ";
            list += expr(*element);
        }
        needsString_ = true;
        return "::cp3::runtime::strings::oneOf(" + expr(*arg) + ", {" + list + "})";
    }
    const char* fn = stringRuntimeFn(m.property);
    if (m.property == "matchAll") {
        // `s.matchAll(regexp)`: a GLOBAL regexp (the runtime fatally rejects
        // a non-global one, as JS does); the eager `Array<Array<string>>`.
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            return std::string();
        }
        const Type regexp = effectiveType(*c.arguments.front());
        if (regexp.kind != TypeKind::Class || regexp.name != "RegExp") {
            return std::string();
        }
        needsString_ = true;
        needsRegex_ = true;
        return "::cp3::runtime::strings::matchAll(" + expr(*m.object) + ", " +
               expr(*c.arguments.front()) + ")";
    }
    if (!fn && m.property == "split") {
        // `s.split(sep)`: a string separator splits through the string runtime
        // (the array extras' `makeArray` wraps the pieces;
        // src/language/arrays/codegen_methods.cpp lowers the methods it feeds into);
        // a RegExp separator lowers onto the D48 engine's JS scan
        // (cp3/core/regex.hpp, Regex::split).
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            return std::string();
        }
        const Type sep = effectiveType(*c.arguments.front());
        if (sep.kind == TypeKind::Class && sep.name == "RegExp") {
            needsRegex_ = true;
            return "(" + expr(*c.arguments.front()) + ").split(" +
                   expr(*m.object) + ")";
        }
        needsString_ = true;
        needsTypedArray_ = true;
        return "::cp3::runtime::makeArray<std::string>(" +
               std::string("::cp3::runtime::strings::split(") +
               expr(*m.object) + ", " + expr(*c.arguments.front()) + "))";
    }
    if (!fn) return std::string();
    needsString_ = true;
    std::vector<std::string> args;
    args.reserve(c.arguments.size() + 1);
    args.push_back(expr(*m.object));
    for (std::string& arg : emitPlainArgs(c)) args.push_back(std::move(arg));
    const std::string call = "::cp3::runtime::strings::" + std::string(fn) +
                             "(" + joinStrings(args, ", ") + ")";
    // `indexOf`/`lastIndexOf` return the exact std signed-index width
    // (`isize`/`ptrdiff_t`); a TypeScript project keeps `number`, so the
    // read widens there. Index/count parameters keep the runtime's `double`
    // JS semantics (truncate/NaN/negative), so no cast is emitted for them
    // (a Rust/C++ argument converts at the call).
    if ((m.property == "indexOf" || m.property == "lastIndexOf") &&
        options_.typeSystem == TypeSystem::TypeScript) {
        return "static_cast<double>(" + call + ")";
    }
    return call;
}

}  // namespace cp3
