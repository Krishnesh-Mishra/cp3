// =============================================================================
// src/sema/src/member_check.cpp — member / index / call expression inference.
//
// Owns property access (`inferMember` / `memberOf` / `memberOfProbe`),
// subscript access (`inferIndex` / `indexOf` / `indexOfObject` /
// `tupleIndexAt`) and call inference (`inferCall` + argument arity/type
// checking). Primary expressions live in expr_check.cpp (STYLE.md §8).
// =============================================================================
#include "analyzer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Member access
// =============================================================================
Type Analyzer::inferMember(MemberExpr& m) {
    Type ot = m.object ? inferExpr(*m.object) : Type::unknown();
    Type result = memberOf(ot, m.property, m.span, m.optional);
    return withType(m, result);
}

Type Analyzer::memberOf(const Type& ot, const std::string& prop,
                        const SourceSpan& span, bool optional) {
    if (ot.isError()) return Type::error();
    if (isAmbientType(ot)) return ambient();
    if (ot.isUnknown()) {
        report(diagcode::kUnknownType, span,
               "property '" + prop + "' does not exist on type 'unknown'");
        return Type::unknown();
    }
    if (ot.isUnion() || ot.isNullable()) {
        std::vector<Type> results;
        bool sawNullish = optional;
        bool emitted = false;
        for (const Type& m : ot.parts) {
            if (m.isNullish()) {
                sawNullish = true;
                continue;
            }
            Type mr = memberOfProbe(m, prop, span);
            if (mr.isError()) continue;
            if (mr.isUnknown() && !emitted) {
                emitted = true;
            }
            results.push_back(mr);
        }
        Type r = results.empty() ? Type::unknown() : Type::unionOf(results);
        if (sawNullish && !r.isUnknown()) {
            r = unify(r, Type::undefined());
        }
        return r;
    }
    return memberOfProbe(ot, prop, span);
}

// Same as memberOf but for a single (non-union) type; emits CP3802 for a
// genuinely missing property on a fixed-shape object.
Type Analyzer::memberOfProbe(const Type& ot, const std::string& prop,
                             const SourceSpan& span) {
    switch (ot.kind) {
        case TypeKind::Object: {
            if (isAmbientType(ot)) return ambient();
            Shape* sh = shapeOf(ot);
            if (!sh) return ambient();
            if (sh->open) return ambient();
            if (sh->tuple) {
                return ambient();
            }
            if (const Field* f = findField(sh, prop)) return f->type;
            report(diagcode::kUndeclaredProperty, span,
                   "undeclared property '" + prop +
                       "' on a fixed-shape object");
            return Type::unknown();
        }
        case TypeKind::Array:
            return arrayMember(ot, prop);
        case TypeKind::String:
            return stringMember(prop);
        case TypeKind::Number:
        case TypeKind::Boolean:
            return ambient();
        case TypeKind::Literal:
            return memberOfProbe(ot.literalBaseType(), prop, span);
        default:
            return ambient();
    }
}

Type Analyzer::arrayMember(const Type& arr, const std::string& prop) {
    Type elem = elemOf(arr);
    if (prop == "length") return Type::number();
    if (prop == "push" || prop == "unshift") {
        Type f = Type::function({elem}, Type::number());
        f.name = kVariadicLabel;
        return f;
    }
    if (prop == "pop" || prop == "shift") return Type::function({}, elem);
    if (prop == "join") return Type::function({Type::string()}, Type::string());
    return ambient();
}

Type Analyzer::stringMember(const std::string& prop) {
    if (prop == "length") return Type::number();
    if (prop == "charAt" || prop == "at") {
        return Type::function({Type::number()}, Type::string());
    }
    if (prop == "charCodeAt") {
        return Type::function({Type::number()}, Type::number());
    }
    if (prop == "startsWith" || prop == "endsWith" || prop == "includes") {
        return Type::function({Type::string()}, Type::boolean());
    }
    if (prop == "repeat") return Type::function({Type::number()}, Type::string());
    if (prop == "padStart" || prop == "padEnd") {
        return Type::function({Type::number(), Type::string()}, Type::string());
    }
    if (prop == "toUpperCase" || prop == "toLowerCase" || prop == "trim") {
        return Type::function({}, Type::string());
    }
    if (prop == "indexOf" || prop == "lastIndexOf") {
        return Type::function({Type::string()}, Type::number());
    }
    return ambient();
}

// =============================================================================
// Subscript access
// =============================================================================
Type Analyzer::inferIndex(IndexExpr& ix) {
    Type ot = ix.object ? inferExpr(*ix.object) : Type::unknown();
    Type result = indexOf(ot, ix);
    return withType(ix, result);
}

Type Analyzer::indexOf(const Type& ot, IndexExpr& ix) {
    Type it = ix.index ? inferExpr(*ix.index) : Type::unknown();
    (void)it;
    if (ot.isError()) return Type::error();
    if (isAmbientType(ot)) return ambient();
    if (ot.isUnknown()) {
        report(diagcode::kUnknownType, ix.span,
               "cannot index into type 'unknown'");
        return Type::unknown();
    }
    if (ot.isUnion() || ot.isNullable()) {
        std::vector<Type> results;
        for (const Type& m : ot.parts) {
            if (m.isNullish()) continue;
            Type r = indexOfProbe(m, ix);
            if (!r.isError()) results.push_back(r);
        }
        return results.empty() ? Type::unknown() : Type::unionOf(results);
    }
    return indexOfProbe(ot, ix);
}

Type Analyzer::indexOfProbe(const Type& ot, IndexExpr& ix) {
    switch (ot.kind) {
        case TypeKind::Array:
            return elemOf(ot);
        case TypeKind::String:
            return Type::number();
        case TypeKind::Object:
            return indexOfObject(ot, ix);
        case TypeKind::Literal:
            return indexOfProbe(ot.literalBaseType(), ix);
        default:
            return ambient();
    }
}

Type Analyzer::indexOfObject(const Type& ot, IndexExpr& ix) {
    if (isAmbientType(ot)) return ambient();
    Shape* sh = shapeOf(ot);
    if (sh && sh->open) return ambient();
    if (sh && sh->tuple) return tupleIndexAt(*sh, ix);
    report(diagcode::kIndexSignatureBanned, ix.span,
           "index access on a plain object is not supported; its keys are "
           "fixed at compile time (use Map<K, V> for dynamic keys)");
    return Type::unknown();
}

Type Analyzer::tupleIndexAt(const Shape& sh, IndexExpr& ix) {
    if (ix.index && ix.index->kind == NodeKind::NumberLiteral) {
        auto& n = static_cast<NumberLiteral&>(*ix.index);
        long idx = static_cast<long>(n.value);
        if (idx >= 0 && static_cast<std::size_t>(idx) < sh.tupleElems.size()) {
            return sh.tupleElems[static_cast<std::size_t>(idx)];
        }
    }
    std::vector<Type> es = sh.tupleElems;
    return es.empty() ? Type::unknown() : Type::unionOf(std::move(es));
}

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
            Type json;
            if (tryInferJsonCall(call, static_cast<MemberExpr&>(*call.callee),
                                 json)) {
                return json;
            }
        }
    }

    for (auto& ta : call.typeArgs) {
        if (ta) resolveType(*ta);
    }

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

void Analyzer::reportUndefinedCall(CallExpr& call, Identifier& id) {
    if (id.name == "arguments") {
        reportBannedArguments(id.span);
    } else {
        report(diagcode::kUndefinedIdentifier, id.span,
               "cannot find name '" + id.name + "'");
    }
    inferArgs(call);
}

bool Analyzer::tryInferJsonCall(CallExpr& call, MemberExpr& m, Type& out) {
    if ((m.property == "parse" || m.property == "stringify") && m.object &&
        m.object->kind == NodeKind::Identifier &&
        static_cast<Identifier&>(*m.object).name == "JSON") {
        inferArgs(call);
        // JSON.parse<T>(...) returns T (explicit) or `unknown` (callers must
        // narrow); JSON.stringify yields a string.
        Type t = Type::unknown();
        if (m.property == "stringify") {
            t = Type::string();
        } else if (!call.typeArgs.empty() && call.typeArgs.front()) {
            t = resolveType(*call.typeArgs.front());
        }
        out = withType(call, t);
        return true;
    }
    return false;
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
    if (ct.name == kVariadicLabel) localSig.rest = true;
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
        checkAssign(at, pt, a->span, false);
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
