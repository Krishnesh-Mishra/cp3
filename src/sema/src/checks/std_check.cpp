// =============================================================================
// src/sema/src/checks/std_check.cpp - std-namespace member tables + `String(x)`.
//
// Owns the typed views of the ambient std surfaces that `import ... from
// "std/..."` names, per STDLIB docs §04: `Math`, `path`, `os`, `process`,
// `crypto` (its table lives in std_crypto.cpp) and `console` / `JSON`, whose
// calls partly stay routed through `tryInferJsonCall` for their generics.
// Typing these names is what turns `let r = Math.random()` into a real
// declaration and enforces the builtin signatures in `check` (M2 open bug 9:
// `Math.abs("x")`, `Math.round()`).
//
// Two shapes of namespace live here:
//   * CLOSED families (Math / path / os / crypto / console / JSON): every
//     member is listed in a table, and an unknown member is CP3305 with
//     "did you mean" candidates from the implemented names (member_check.cpp
//     consults isClosedStdNamespace). Console is surface-aware: members
//     documented but still pending codegen (assert/clear/input/readKey) stay
//     ambient so their CP3002 skip survives, and the candidates stay limited
//     to the implemented set.
//   * OPEN families (process here, network in std_network.cpp, fs / env / ...
//     which have no table at all): unknown members keep the old permissive
//     ambient behavior, so the pinned signature cases pass unchanged.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// One implemented member of a std namespace: its name and a factory for its
// type. The same table drives member typing and the CP3305 candidate list, so
// the two can never drift apart.
struct MemberRow {
    const char* name;
    Type (*make)();
};

template <std::size_t N>
std::optional<Type> lookupMember(const MemberRow (&rows)[N], const std::string& prop) {
    for (const MemberRow& row : rows) {
        if (prop == row.name) return row.make();
    }
    return std::nullopt;
}

template <std::size_t N>
std::vector<std::string> memberNames(const MemberRow (&rows)[N]) {
    std::vector<std::string> names;
    names.reserve(N);
    for (const MemberRow& row : rows) names.push_back(row.name);
    return names;
}

// --- type factories -----------------------------------------------------------

Type numberValue() { return Type::number(); }
Type stringValue() { return Type::string(); }

// `(...values: number[]) => number` - at least one argument.
Type variadicNumberToNumber() {
    Type fn = Type::function({Type::number()}, Type::number());
    fn.name = kVariadicLabel;
    return fn;
}

// `(x: number) => number`.
Type numberUnary() { return Type::function({Type::number()}, Type::number()); }

Type zeroToNumber() { return Type::function({}, Type::number()); }
Type zeroToString() { return Type::function({}, Type::string()); }
Type twoNumberToNumber() {
    return Type::function({Type::number(), Type::number()}, Type::number());
}
Type threeNumberToNumber() {
    return Type::function({Type::number(), Type::number(), Type::number()},
                          Type::number());
}
Type twoStringToString() {
    return Type::function({Type::string(), Type::string()}, Type::string());
}
Type stringToString() { return Type::function({Type::string()}, Type::string()); }
Type stringToBool() {
    return Type::function({Type::string()}, Type::boolean());
}
Type optionalStringToString() {
    return withOptionalTail(twoStringToString());
}
Type variadicStringToString() {
    Type fn = Type::function({Type::string()}, Type::string());
    fn.name = kVariadicLabel;
    return fn;
}

// `(text: string) => unknown` / `(value: unknown, indent?: number) => string`.
// The real call typing stays in tryInferJsonCall (call_check.cpp); these rows
// only mark the surface closed and give member reads a type.
Type jsonParseSignature() {
    return Type::function({Type::string()}, Type::unknown());
}
Type jsonStringifySignature() {
    return withOptionalTail(
        Type::function({Type::unknown(), Type::number()}, Type::string()));
}

// --- implemented member tables (the suggestion candidates) --------------------

const MemberRow kMathRows[] = {
    {"PI", numberValue},             {"E", numberValue},
    {"random", zeroToNumber},        {"min", variadicNumberToNumber},
    {"max", variadicNumberToNumber}, {"pow", twoNumberToNumber},
    {"gcd", twoNumberToNumber},      {"lcm", twoNumberToNumber},
    {"clamp", threeNumberToNumber},  {"lerp", threeNumberToNumber},
    {"sqrt", numberUnary},           {"abs", numberUnary},
    {"floor", numberUnary},          {"ceil", numberUnary},
    {"round", numberUnary},          {"trunc", numberUnary},
    {"cbrt", numberUnary},           {"log", numberUnary},
    {"log2", numberUnary},           {"log10", numberUnary},
    {"sin", numberUnary},            {"cos", numberUnary},
    {"tan", numberUnary},
};

const MemberRow kPathRows[] = {
    {"sep", stringValue},
    {"join", variadicStringToString},
    {"resolve", variadicStringToString},
    {"extname", stringToString},
    {"dirname", stringToString},
    {"normalize", stringToString},
    {"basename", optionalStringToString},
    {"isAbsolute", stringToBool},
};

const MemberRow kOsRows[] = {
    {"platform", zeroToString}, {"homedir", zeroToString},
    {"tmpdir", zeroToString},   {"hostname", zeroToString},
    {"cpus", zeroToNumber},     {"totalMemory", zeroToNumber},
    {"freeMemory", zeroToNumber},
};

const MemberRow kJsonRows[] = {
    {"parse", jsonParseSignature},
    {"stringify", jsonStringifySignature},
};

// Console's implemented set: a typo must never recommend a member that would
// just report CP3002, so the documented-but-pending members are not candidates.
const char* const kConsoleImplemented[] = {"log", "info", "warn", "error"};

}  // namespace

// -----------------------------------------------------------------------------
// Namespace member tables
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

std::optional<Type> Analyzer::mathMember(const std::string& prop) {
    return lookupMember(kMathRows, prop);
}

std::optional<Type> Analyzer::pathMember(const std::string& prop) {
    return lookupMember(kPathRows, prop);
}

std::optional<Type> Analyzer::osMember(const std::string& prop) {
    return lookupMember(kOsRows, prop);
}

std::optional<Type> Analyzer::processMember(const std::string& prop) {
    // OPEN family: only the members with a pinned signature are typed, the
    // rest (`exec` / `spawn` / streaming argv) stays permissive (the file
    // header explains the split).
    if (prop == "exit") {
        return Type::function({Type::number()}, Type::voidType());
    }
    if (prop == "argv") return Type::array(Type::string());
    if (prop == "env") {
        // A minimal fixed shape: `process.env.get(name): string | undefined`.
        std::vector<Type> value;
        value.push_back(Type::string());
        value.push_back(Type::undefined());
        Field get;
        get.name = "get";
        get.type = Type::function({Type::string()}, Type::unionOf(std::move(value)));
        std::vector<Field> fields;
        fields.push_back(std::move(get));
        return makeObjectType(std::move(fields), false, false, {});
    }
    return std::nullopt;
}

std::optional<Type> Analyzer::consoleMember(const std::string& prop) {
    // Surface-aware console (the user-facing typo example): the four
    // members codegen lowers today type as ambient (permissive signatures);
    // the documented but still pending ones keep the ambient fallthrough so
    // their CP3002 skip survives until the console wave lands.
    static const char* const kPending[] = {"assert", "clear", "input", "readKey"};
    for (const char* name : kConsoleImplemented) {
        if (prop == name) return ambient();
    }
    for (const char* name : kPending) {
        if (prop == name) return ambient();
    }
    return std::nullopt;
}

std::optional<Type> Analyzer::jsonMember(const std::string& prop) {
    return lookupMember(kJsonRows, prop);
}

// -----------------------------------------------------------------------------
// Surface helpers (declared in detail/analyzer.h)
// -----------------------------------------------------------------------------

bool isClosedStdNamespace(const std::string& ns) {
    return ns == "Math" || ns == "path" || ns == "os" || ns == "crypto" ||
           ns == "console" || ns == "JSON";
}

std::vector<std::string> stdNamespaceMemberNames(const std::string& ns) {
    if (ns == "Math") return memberNames(kMathRows);
    if (ns == "path") return memberNames(kPathRows);
    if (ns == "os") return memberNames(kOsRows);
    if (ns == "JSON") return memberNames(kJsonRows);
    if (ns == "crypto") return cryptoMemberNames();
    if (ns == "console") {
        std::vector<std::string> names;
        for (const char* name : kConsoleImplemented) names.push_back(name);
        return names;
    }
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
