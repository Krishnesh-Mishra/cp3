// =============================================================================
// src/std/env/sema.cpp - `std/env` surface typing (`{ env }` named import).
//
// OWNERSHIP: the std/env family (src/std/env/**). `std/env` is a named-only
// module (src/modules/src/support/std_registry.cpp, named export `env`), so
// the import binds the ambient name `env` and member typing flows through the
// std_check.cpp namespace dispatch (stdNamespaceMember) into `envMember`.
//
// CLOSED family: load/get/has is the whole documented surface (STDLIB docs
// §04, env section), so an unknown member is CP3305 with these names as
// candidates instead of the permissive ambient fallthrough.
//
// `get<T>(key, options?)` is the one member whose RESULT depends on the call
// site: T is the explicit type argument, else the options object's `default`
// value type, else `string` (the raw value). That typing lives in
// `tryInferEnvCall`, hooked from call_check.cpp; the table entry stays
// permissive so the generic call path never rejects a valid form.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// `get<T>(key, options?) => T`; T is refined per call site (tryInferEnvCall).
Type envGetMember() {
    return Type::function({Type::string(), ambient()}, ambient());
}

Type envHasMember() { return Type::function({Type::string()}, Type::boolean()); }

Type envLoadMember() { return Type::function({Type::string()}, Type::voidType()); }

const MemberRow kEnvRows[] = {
    {"get", envGetMember},
    {"has", envHasMember},
    {"load", envLoadMember},
};

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
/// when `options` is not an object literal carrying one (`{ default: v }` is
/// the documented shape; the `default: v` named-argument spelling stays the
/// open question recorded by the `_ambiguous` marker).
Expr* defaultOption(Expr* options) {
    if (!options || options->kind != NodeKind::ObjectLiteral) return nullptr;
    const auto& literal = static_cast<const ObjectLiteral&>(*options);
    for (const auto& p : literal.properties) {
        if (!p || !p->value) continue;
        if (objectKey(*p) == "default") return p->value.get();
    }
    return nullptr;
}

}  // namespace

std::optional<Type> Analyzer::envMember(const std::string& prop) {
    return lookupMember(kEnvRows, prop);
}

std::vector<std::string> envMemberNames() { return memberNames(kEnvRows); }

bool Analyzer::tryInferEnvCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (m.property != "get") return false;
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<const Identifier&>(*m.object).name != "env") {
        return false;
    }
    // Arity and argument types first: the result type below reads the
    // options object after its own inference published the default value.
    FuncSig sig;
    sig.annotatedReturn = true;
    sig.name = "env.get";
    sig.params = {Type::string(), ambient()};
    sig.optional = {false, true};
    sig.ret = ambient();
    checkArgs(sig, call);
    Type value = Type::string();
    if (!call.typeArgs.empty() && call.typeArgs.front()) {
        value = resolveType(*call.typeArgs.front());
    } else {
        Expr* options =
            call.arguments.size() > 1 ? call.arguments[1].get() : nullptr;
        if (Expr* def = defaultOption(options)) value = typeOfExpr(*def);
    }
    out = withType(call, value);
    return true;
}

}  // namespace cp3
