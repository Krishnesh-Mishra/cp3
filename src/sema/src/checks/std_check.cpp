// =============================================================================
// src/sema/src/checks/std_check.cpp - std-namespace member tables + `String(x)`.
//
// Owns the typed views of the ambient std surfaces that `import ... from
// "std/..."` (or the builtin table) names, per STDLIB docs §04: `Math`, `path`,
// `os`, `process` (+ `JSON`, whose calls stay routed through
// `tryInferJsonCall` for its generics). Typing these names is what turns
// `let r = Math.random()` into a real declaration and enforces the builtin
// signatures in `check` (M2 open bug 9: `Math.abs("x")`, `Math.round()`).
//
// Anything not listed here keeps the old permissive ambient behavior, so the
// pinned check-mode signature cases (`process.exec`/`spawn`, `fs.*`, `env.*`)
// pass unchanged.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// `(...values: number[]) => number` - at least one argument.
Type variadicNumberToNumber() {
    Type fn = Type::function({Type::number()}, Type::number());
    fn.name = kVariadicLabel;
    return fn;
}

// `(x: number) => number`.
Type numberUnary() { return Type::function({Type::number()}, Type::number()); }

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
    return std::nullopt;
}

std::optional<Type> Analyzer::mathMember(const std::string& prop) {
    if (prop == "PI" || prop == "E") return Type::number();
    if (prop == "random") return Type::function({}, Type::number());
    if (prop == "min" || prop == "max") return variadicNumberToNumber();
    if (prop == "pow" || prop == "gcd" || prop == "lcm") {
        return Type::function({Type::number(), Type::number()}, Type::number());
    }
    if (prop == "clamp" || prop == "lerp") {
        return Type::function(
            {Type::number(), Type::number(), Type::number()}, Type::number());
    }
    static const char* const kUnary[] = {"sqrt", "abs",   "floor", "ceil",
                                         "round", "trunc", "cbrt",  "log",
                                         "log2",  "log10", "sin",   "cos",
                                         "tan"};
    for (const char* name : kUnary) {
        if (prop == name) return numberUnary();
    }
    return std::nullopt;
}

std::optional<Type> Analyzer::pathMember(const std::string& prop) {
    if (prop == "sep") return Type::string();
    if (prop == "join" || prop == "resolve") {
        Type fn = Type::function({Type::string()}, Type::string());
        fn.name = kVariadicLabel;
        return fn;
    }
    if (prop == "extname" || prop == "dirname" || prop == "normalize") {
        return Type::function({Type::string()}, Type::string());
    }
    if (prop == "basename") {
        return withOptionalTail(
            Type::function({Type::string(), Type::string()}, Type::string()));
    }
    if (prop == "isAbsolute") {
        return Type::function({Type::string()}, Type::boolean());
    }
    return std::nullopt;
}

std::optional<Type> Analyzer::osMember(const std::string& prop) {
    if (prop == "platform" || prop == "homedir" || prop == "tmpdir" ||
        prop == "hostname") {
        return Type::function({}, Type::string());
    }
    if (prop == "cpus" || prop == "totalMemory" || prop == "freeMemory") {
        return Type::function({}, Type::number());
    }
    return std::nullopt;
}

std::optional<Type> Analyzer::processMember(const std::string& prop) {
    if (prop == "exit") {
        return Type::function({Type::number()}, Type::voidType());
    }
    if (prop == "argv") return Type::array(Type::string());
    if (prop == "env") {
        // A minimal fixed shape: `process.env.get(name): string | undefined`.
        // (`exec`/`spawn`/`argv` streaming stay ambient this wave.)
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
