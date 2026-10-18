// =============================================================================
// src/sema/src/checks/std_check.cpp - the std-namespace dispatch + `String(x)`.
//
// Routes the typed views of the ambient std surfaces that `import ... from
// "std/..."` names, per STDLIB docs §04: `Math`, `path`, `os`, `process`,
// `crypto` and `console` / `JSON`, whose calls partly stay routed through
// `tryInferJsonCall` for their generics. Typing these names is what turns
// `let r = Math.random()` into a real declaration and enforces the builtin
// signatures in `check` (M2 open bug 9: `Math.abs("x")`, `Math.round()`).
//
// The member tables themselves live with their families
// (`src/std/<family>/sema.cpp`, D72 stage 2); this unit keeps the per-namespace
// switch and the shared suggestion machinery, so a new member lands in the
// family unit without touching core.
//
// Two shapes of namespace:
//   * CLOSED families (Math / path / os / crypto / console / JSON): every
//     member is listed in the family's table, and an unknown member is CP3305
//     with "did you mean" candidates from the implemented names (member_check.cpp
//     consults isClosedStdNamespace). Console is surface-aware: members
//     documented but still pending codegen (assert/clear/input/readKey) stay
//     ambient so their CP3002 skip survives, and the candidates stay limited
//     to the implemented set.
//   * OPEN families (process in src/std/process/sema.cpp, network in
//     src/std/network/sema.cpp, fs / env / ... which have no table at all):
//     unknown members keep the old permissive ambient behavior, so the pinned
//     signature cases pass unchanged.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

// -----------------------------------------------------------------------------
// Namespace dispatch
// -----------------------------------------------------------------------------

std::optional<Type> Analyzer::stdNamespaceMember(const std::string& ns,
                                                 const std::string& prop) {
    if (ns == "Math") return mathMember(prop);
    if (ns == "path") return pathMember(prop);
    if (ns == "os") return osMember(prop);
    if (ns == "process") return processMember(prop);
    if (ns == "crypto") return cryptoMember(prop);
    if (ns == "network") return networkMember(prop);
    if (ns == "console") return consoleMember(prop);
    if (ns == "JSON") return jsonMember(prop);
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// Surface helpers (declared in detail/analyzer.h)
// -----------------------------------------------------------------------------

bool isClosedStdNamespace(const std::string& ns) {
    return ns == "Math" || ns == "path" || ns == "os" || ns == "crypto" ||
           ns == "console" || ns == "JSON";
}

// The CP3305 candidate lists come from the family units, which own the tables.
std::vector<std::string> stdNamespaceMemberNames(const std::string& ns) {
    if (ns == "Math") return mathMemberNames();
    if (ns == "path") return pathMemberNames();
    if (ns == "os") return osMemberNames();
    if (ns == "JSON") return jsonMemberNames();
    if (ns == "crypto") return cryptoMemberNames();
    if (ns == "console") return consoleMemberNames();
    return {};
}

std::string stdImportHint(const std::string& name) {
    struct ModuleHint {
        const char* binding;
        const char* specifier;
    };
    // The default-exported std modules (std_registry.cpp) whose import line a
    // bare name follows from. Named-only modules (std/events, std/env, ...)
    // are handled by import validation instead.
    static const ModuleHint kHints[] = {
        {"Math", "std/math"},       {"JSON", "std/json"},
        {"console", "std/console"}, {"path", "std/path"},
        {"os", "std/os"},           {"crypto", "std/crypto"},
        {"fs", "std/fs"},           {"Date", "std/date"},
        {"process", "std/process"},
    };
    for (const ModuleHint& hint : kHints) {
        if (name == hint.binding) {
            return "import " + std::string(hint.binding) + " from \"" +
                   hint.specifier + "\";";
        }
    }
    return {};
}

// -----------------------------------------------------------------------------
// `String(x)` global
// -----------------------------------------------------------------------------

// `String(value)`: the type is always `string` (the runtime lowers it onto
// toString). The argument is not constrained: JS `String` accepts anything,
// including arrays (which M1 cannot represent yet).
bool Analyzer::tryInferStringBuiltin(CallExpr& call, const Identifier& id,
                                     Type& out) {
    if (id.name != "String") return false;
    inferArgs(call);
    out = Type::string();
    return true;
}

}  // namespace cp3
