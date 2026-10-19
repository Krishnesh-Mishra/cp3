// =============================================================================
// src/sema/src/checks/call_check.cpp - call inference and overload resolution.
//
// Owns `inferCall` (the call-expression dispatcher), argument arity/type
// checking (`checkArgs`), the per-callable special cases (`Promise.resolve`,
// `JSON.parse`/`stringify`, the timer builtins) and overload resolution
// (spec §17). Member/index inference lives in member_check.cpp; primary
// expressions live in expr_check.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Calls
// =============================================================================
Type Analyzer::inferCall(CallExpr& call) {
    Type ct = Type::unknown();
    FuncSig localSig;
    const FuncSig* sig = nullptr;
    if (call.callee && call.callee->kind == NodeKind::Identifier) {
        auto& id = static_cast<Identifier&>(*call.callee);
        Binding b;
        if (!lookup(id.name, b)) {
            reportUndefinedCall(call, id);
            return withType(call, Type::error());
        }
        ct = b.type;
        setType(id, ct);
        if (isAmbientType(b.type)) {
            Type builtin;
            if (tryTimerBuiltinCall(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferStringBuiltin(call, id, builtin)) {
                return withType(call, builtin);
            }
            if (tryInferNetworkCall(call, id, builtin)) {
                return withType(call, builtin);
            }
        }
        if (b.overloads.size() >= 2) {
            return inferOverloadCall(call, b, id);
        }
        if (b.isFunction) {
            localSig = b.sig;
            sig = &localSig;
        }
    } else if (call.callee) {
        ct = inferExpr(*call.callee);
        // JSON.parse returns `unknown`: the shape is only known at runtime, so
        // callers must narrow before use (spec §20).
        if (call.callee->kind == NodeKind::MemberExpr) {
            checkParallelCall(call);
            auto& member = static_cast<MemberExpr&>(*call.callee);
            if (Type json; tryInferJsonCall(call, member, json)) return json;
            if (Type promise; tryInferPromiseStaticCall(call, member, promise)) return promise;
            // std/network generics: `res.json<T>()` / `req.json<T>()` (src/std/network/sema.cpp).
            if (Type network; tryInferNetworkMemberCall(call, member, network)) return network;
            if (Type family; tryInferFamilyMethodCall(call, member, family)) return family;
            if (Type buffer; tryInferBufferCall(call, member, buffer)) return buffer;
        }
    }

    for (auto& ta : call.typeArgs) if (ta) resolveType(*ta);

    if (ct.isError()) {
        inferArgs(call);
        return withType(call, Type::error());
    }

    if (sig == nullptr && ct.kind == TypeKind::Function) {
        sig = &localSig;
        sigFromFunctionType(ct, localSig);
    }

    if (sig != nullptr) {
        checkArgs(*sig, call);
        return withType(call, sig->ret);
    }

    // Ambient / unknown callables are permissive.
    if (isAmbientType(ct) || ct.isUnknown()) {
        inferArgs(call);
        return withType(call, ambient());
    }

    report(diagcode::kNotCallable, call.span,
           "value of type '" + pretty(ct) + "' is not callable");
    inferArgs(call);
    return withType(call, Type::error());
}

// Core timer builtins (spec §14, STDLIB docs §03): honest result types so
// `let id = setInterval(...)`, `clearTimeout(id)` and `await sleep(...)` type
// correctly (the ambient table is permissive by default). Returns true when
// `id` is one of them and `out` is set.
bool Analyzer::tryTimerBuiltinCall(CallExpr& call, const Identifier& id, Type& out) {
    if (id.name == "setTimeout" || id.name == "setInterval") {
        inferArgs(call);
        out = Type::number();
        return true;
    }
    if (id.name == "sleep") {
        inferArgs(call);
        out = promiseOf(Type::voidType());
        return true;
    }
    if (id.name == "clearTimeout" || id.name == "clearInterval") {
        inferArgs(call);
        out = Type::voidType();
        return true;
    }
    return false;
}

void Analyzer::reportUndefinedCall(CallExpr& call, Identifier& id) {
    if (id.name == "arguments") {
        reportBannedArguments(id.span);
    } else {
        reportUndefinedName(id.span, id.name);
    }
    inferArgs(call);
}

bool Analyzer::tryInferJsonCall(CallExpr& call, MemberExpr& m, Type& out) {
    if ((m.property == "parse" || m.property == "stringify") && m.object &&
        m.object->kind == NodeKind::Identifier &&
        static_cast<Identifier&>(*m.object).name == "JSON") {
        // JSON.stringify(value, indent?): `unknown` accepts any argument type
        // (M1 cannot represent objects/arrays yet, but must not reject them),
        // and the optional indent is a number. JSON.parse(text) requires a
        // string. Both are enforced in `check` (M2 open bug 9).
        FuncSig sig;
        sig.annotatedReturn = true;
        sig.name = "JSON." + m.property;
        // JSON.parse<T>(...) returns T (explicit) or `unknown` (callers must
        // narrow); JSON.stringify yields a string.
        Type t = Type::unknown();
        if (m.property == "stringify") {
            sig.params = {Type::unknown(), Type::number()};
            sig.optional = {false, true};
            sig.ret = Type::string();
            t = Type::string();
        } else {
            sig.params = {Type::string()};
            sig.optional = {false};
            if (!call.typeArgs.empty() && call.typeArgs.front()) {
                t = resolveType(*call.typeArgs.front());
            }
            sig.ret = t;
        }
        checkArgs(sig, call);
        out = withType(call, t);
        return true;
    }
    return false;
}

// `Promise.resolve(v)` / `Promise.reject(v)` (DECISIONS D40): the value type of
// the promise a call yields, so a `let p = …` initializer lowers to a real
// `cp3::runtime::Promise<T>` instead of falling back to ambient. Codegen
// (emit_async.cpp) derives the exact C++ spelling from this type.
bool Analyzer::tryInferPromiseStaticCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (m.property != "resolve" && m.property != "reject") return false;
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Promise") {
        return false;
    }
    inferArgs(call);
    if (m.property == "reject") {
        // A rejection carries no observable value; cp3 models it as
        // `Promise<void>` (JS types it `Promise<never>`).
        out = withType(call, promiseOf(Type::voidType()));
        return true;
    }
    // `Promise.resolve(v)`: the promise carries `v`'s type. An existing promise
    // passes through unchanged (JS identity); no argument resolves with
    // `undefined`.
    Type value = Type::undefined();
    Expr* arg = call.arguments.empty() ? nullptr : call.arguments.front().get();
    if (arg) {
        if (arg->kind == NodeKind::SpreadElement) {
            out = withType(call, promiseOf(Type::unknown()));
            return true;
        }
        value = typeOfExpr(*arg);
        if (isPromiseType(value)) {
            out = withType(call, value);
            return true;
        }
    }
    out = withType(call, promiseOf(value));
    return true;
}

void Analyzer::inferArgs(CallExpr& call) {
    for (auto& a : call.arguments) inferArg(a.get());
}

void Analyzer::sigFromFunctionType(const Type& ct, FuncSig& localSig) {
    localSig.name = "";
    localSig.annotatedReturn = true;
    if (!ct.parts.empty()) {
        localSig.ret = ct.parts.back();
        for (std::size_t i = 0; i + 1 < ct.parts.size(); ++i) {
            localSig.params.push_back(ct.parts[i]);
            localSig.optional.push_back(false);
        }
    }
    if (ct.name == kVariadicLabel ||
        ct.name == kVariadicOptionalTailLabel) {
        localSig.rest = true;
    }
    if (ct.name == kOptionalTailLabel && !localSig.optional.empty()) {
        localSig.optional.back() = true;
    }
    if (ct.name == kVariadicOptionalTailLabel && !localSig.optional.empty()) {
        localSig.optional.back() = true;
    }
    if (ct.name == kOptionalTail2Label && localSig.optional.size() >= 2) {
        localSig.optional[localSig.optional.size() - 1] = true;
        localSig.optional[localSig.optional.size() - 2] = true;
    }
}

void Analyzer::inferArg(Expr* a) {
    if (!a) return;
    if (a->kind == NodeKind::SpreadElement) {
        auto& sp = static_cast<SpreadElement&>(*a);
        if (sp.argument) inferExpr(*sp.argument);
        return;
    }
    inferExpr(*a);
}

void Analyzer::checkArgs(const FuncSig& sig, CallExpr& call) {
    bool hasSpread = false;
    for (auto& a : call.arguments) {
        if (a && a->kind == NodeKind::SpreadElement) hasSpread = true;
    }
    std::size_t required = 0;
    for (std::size_t i = 0; i < sig.params.size(); ++i) {
        if (i < sig.optional.size() && sig.optional[i]) continue;
        ++required;
    }
    const std::size_t given = call.arguments.size();
    const std::size_t maxArgs =
        sig.rest ? static_cast<std::size_t>(-1) : sig.params.size();
    if (!hasSpread && (given < required || given > maxArgs)) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: expected " +
                   arityText(required, maxArgs, sig.rest) + ", got " +
                   std::to_string(given) + " argument" +
                   (given == 1 ? "" : "s"));
    }
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        Expr* a = call.arguments[i].get();
        if (!a) continue;
        if (a->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*a);
            if (sp.argument) inferExpr(*sp.argument);
            continue;
        }
        Type at = inferExpr(*a);
        Type pt = Type::unknown();
        if (sig.rest && sig.params.size() > 0 && i >= sig.params.size()) {
            pt = sig.params.back();
        } else if (i < sig.params.size()) {
            pt = sig.params[i];
        }
        // An explicit `undefined` for an optional/default parameter is the same
        // as omitting the argument (TS accepts it; the default applies), so it
        // is not an assignability error.
        const bool undefinedForOptional =
            a->kind == NodeKind::UndefinedLiteral && i < sig.optional.size() &&
            sig.optional[i];
        if (!undefinedForOptional) {
            checkAssign(at, pt, a->span, false);
        }
    }
}

std::string Analyzer::arityText(std::size_t required, std::size_t maxArgs,
                                bool rest) const {
    if (rest) return "at least " + std::to_string(required);
    if (maxArgs == required) return std::to_string(required);
    return std::to_string(required) + " to " + std::to_string(maxArgs);
}

// =============================================================================
// Overload resolution (spec §17)
// =============================================================================
Type Analyzer::inferOverloadCall(CallExpr& call, const Binding& binding,
                                 Identifier& id) {
    std::vector<Type> argTypes;
    bool hasSpread = false;
    collectArgTypes(call, argTypes, hasSpread);
    for (const FuncSig& sig : binding.overloads) {
        if (overloadViable(sig, argTypes, hasSpread)) {
            // A viable overload is accepted permissively (M1 does not yet
            // implement overload-dispatch typing); the ambient type keeps the
            // rest of the file from cascading, exactly as before.
            return withType(call, ambient());
        }
    }
    report(diagcode::kNoMatchingOverload, call.span,
           "no overload of '" + id.name +
               "' matches the supplied arguments");
    return withType(call, Type::error());
}

void Analyzer::collectArgTypes(CallExpr& call, std::vector<Type>& out,
                               bool& hasSpread) {
    out.reserve(call.arguments.size());
    for (auto& a : call.arguments) {
        if (!a) {
            out.push_back(Type::unknown());
            continue;
        }
        if (a->kind == NodeKind::SpreadElement) {
            hasSpread = true;
            auto& sp = static_cast<SpreadElement&>(*a);
            if (sp.argument) inferExpr(*sp.argument);
            out.push_back(Type::unknown());
            continue;
        }
        out.push_back(inferExpr(*a));
    }
}

bool Analyzer::overloadViable(const FuncSig& sig,
                              const std::vector<Type>& argTypes,
                              bool hasSpread) {
    std::size_t required = 0;
    for (std::size_t i = 0; i < sig.params.size(); ++i) {
        if (i < sig.optional.size() && sig.optional[i]) continue;
        ++required;
    }
    const std::size_t given = argTypes.size();
    const std::size_t maxArgs =
        sig.rest ? static_cast<std::size_t>(-1) : sig.params.size();
    if (!hasSpread && (given < required || given > maxArgs)) return false;
    for (std::size_t i = 0; i < argTypes.size(); ++i) {
        Type pt = Type::unknown();
        if (sig.rest && !sig.params.empty() && i >= sig.params.size()) {
            pt = sig.params.back();
        } else if (i < sig.params.size()) {
            pt = sig.params[i];
        }
        if (!wouldAccept(argTypes[i], pt, false)) return false;
    }
    return true;
}

}  // namespace cp3
