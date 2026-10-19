// =============================================================================
// src/std/env/codegen.cpp - `{ env }` (std/env) call lowering.
//
// OWNERSHIP: the std/env family (src/std/env/**). Lowers the named-import
// surface typed by src/std/env/sema.cpp onto cp3/std/env.hpp:
//
//   env.get<T>(key)            -> ::cp3::runtime::env::get<T>(key)
//   env.get<T>(key, {default}) -> ::cp3::runtime::env::get<T>(key, default)
//   env.has(key)               -> ::cp3::runtime::env::has(key)
//   env.load(path)             -> ::cp3::runtime::env::load(path)
//
// T is sema's per-call result type (the explicit type argument, else the
// options object's `default` value type, else string), classified to its C++
// spelling. The options wrapper is dropped at the call: the runtime takes the
// fallback value itself. The named-argument spelling
// (`env.get<T>(key, default: v)`) stays the recorded open question
// (tests/cases/stdlib/_ambiguous/env_get_named_arg_syntax.cp3).
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kRuntimeNs = "::cp3::runtime::env::";

/// The literal property name of an object-literal entry ("" when computed).
std::string objectKey(const ObjectProperty& p) {
    if (p.computed || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
}

/// The value expression of the options object's `default` entry, or nullptr
/// when `options` is not an object literal carrying one.
const Expr* defaultOption(const Expr* options) {
    if (!options || options->kind != NodeKind::ObjectLiteral) return nullptr;
    const auto& literal = static_cast<const ObjectLiteral&>(*options);
    for (const auto& prop : literal.properties) {
        if (!prop || !prop->value) continue;
        if (objectKey(*prop) == "default") return prop->value.get();
    }
    return nullptr;
}

}  // namespace

std::string Generator::emitEnvCall(const CallExpr& c, const MemberExpr& m) {
    if (m.property == "has" || m.property == "load") {
        if (c.arguments.size() != 1) return std::string();
        const Expr* arg = c.arguments.front().get();
        if (!arg || arg->kind == NodeKind::SpreadElement) return std::string();
        needsEnv_ = true;
        return std::string(kRuntimeNs) + m.property + "(" + expr(*arg) + ")";
    }
    if (m.property != "get" || c.arguments.empty()) return std::string();
    const Expr* key = c.arguments.front().get();
    if (!key || key->kind == NodeKind::SpreadElement) return std::string();
    // T: sema's per-call result type (tryInferEnvCall published it), so the
    // explicit type argument, the `default` value type and the plain-string
    // form all land on the same classification.
    const CppInfo value = classify(effectiveType(c));
    if (!value.representable || !isPrimitiveShape(value.shape)) {
        notImplemented(c, "env.get whose value type has no C++ spelling");
        return "0.0";
    }
    needsEnv_ = true;
    std::string text = std::string(kRuntimeNs) + "get<" + value.cpp + ">(" +
                       expr(*key);
    if (c.arguments.size() > 1) {
        const Expr* fallback = defaultOption(c.arguments[1].get());
        if (fallback == nullptr) {
            notImplemented(c, "env.get options other than { default: ... }");
        } else {
            text += ", " + expr(*fallback);
        }
    }
    return text + ")";
}

}  // namespace cp3
