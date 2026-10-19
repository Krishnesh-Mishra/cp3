// =============================================================================
// src/std/string/codegen.cpp - `string` method + literal-array
// lowering.
//
// The implemented `string` method surface (cp3/std/string.hpp) plus the literal
// array element set `["a", "b"].includes(x)`, which lowers to an equality
// chain (the only array form representable before wave 3).
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The `string` method surface implemented this wave (cp3/std/string.hpp). `at` is
// JS charAt; `split`/`charCodeAt` are not lowerable yet (wave 3 arrays).
struct StringMethod {
    const char* js;  // the cp3 method name
    const char* fn;  // the runtime spelling
};
constexpr StringMethod kStringMethods[] = {
    {"charAt", "charAt"},       {"at", "charAt"},
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

std::string Generator::emitStringMethodCall(const CallExpr& c, const MemberExpr& m) {
    // Literal array element set: `["a", "b"].includes(x)` lowers to an equality
    // chain (the only array form representable before wave 3). Any other form
    // on an array receiver stays CP3002 - never fall through to the string
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
    if (!fn && m.property == "split") {
        // `s.split(sep)` -> a regular `string[]` (the array extras';
        // `makeArray` wraps the pieces; src/std/arrays/codegen_methods.cpp
        // lowers the methods the result feeds into).
        if (c.arguments.size() != 1 || !c.arguments.front()) {
            return std::string();
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
    return "::cp3::runtime::strings::" + std::string(fn) + "(" +
           joinStrings(args, ", ") + ")";
}

}  // namespace cp3
