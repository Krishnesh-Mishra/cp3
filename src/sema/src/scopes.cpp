// =============================================================================
// src/sema/src/scopes.cpp — scope stack, declaration tables and hoisting.
//
// Owns the lexical scope stack, `lookup`, the `declareBinding` /
// `declareAmbient` / `overrideBinding` primitives, the builtin global table,
// and the hoisting passes (`hoistType` / `hoistFunction` / `registerTypeParams`)
// that make later annotations visible to earlier ones (STYLE.md §8).
// =============================================================================
#include "analyzer.h"

#include <cstdint>
#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Scope stack + name tables
// =============================================================================
void Analyzer::pushScope() { scopes_.emplace_back(); }

void Analyzer::popScope() {
    if (!scopes_.empty()) scopes_.pop_back();
}

bool Analyzer::lookup(const std::string& name, Binding& out) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            out = found->second;
            return true;
        }
    }
    return false;
}

void Analyzer::declareBinding(const std::string& name, const Type& type,
                              const SourceSpan& span, const BindingSpec& spec) {
    auto& scope = scopes_.back();
    auto existing = scope.find(name);
    if (existing != scope.end()) {
        // Function overloading (spec §17): a second function with the same
        // name in the same scope is an overload set, not a duplicate. Record
        // every candidate signature so the call site can resolve against the
        // set; expose the name permissively otherwise.
        if (existing->second.isFunction && spec.isFunction) {
            if (existing->second.overloads.empty() &&
                existing->second.sig.declNodeId != 0) {
                existing->second.overloads.push_back(existing->second.sig);
            }
            if (spec.sig) {
                existing->second.overloads.push_back(*spec.sig);
            }
            existing->second.type = ambient();
            existing->second.isFunction = false;
            return;
        }
        report(diagcode::kDuplicateDeclaration, span,
               "duplicate declaration of '" + name + "'");
        return;
    }
    Binding b;
    b.name = name;
    b.type = type;
    b.isConst = spec.isConst;
    b.isFunction = spec.isFunction;
    b.isAmbient = isAmbientType(type);
    b.publish = spec.publish;
    b.declNodeId = spec.declNodeId;
    b.span = span;
    if (spec.sig) b.sig = *spec.sig;

    if (spec.publish && spec.declNodeId != 0) publishSymbol(b);
    scope.emplace(name, std::move(b));
}

void Analyzer::declareAmbient(const std::string& name, const SourceSpan& span,
                              std::uint32_t nodeId) {
    if (name.empty()) return;
    declareBinding(name, ambient(), span, BindingSpec{.declNodeId = nodeId});
}

void Analyzer::overrideBinding(const std::string& name, const Type& type,
                               const SourceSpan& span) {
    Binding b;
    b.name = name;
    b.type = type;
    b.isAmbient = isAmbientType(type);
    b.publish = false;
    b.span = span;
    scopes_.back()[name] = std::move(b);
}

void Analyzer::declareBuiltins() {
    static const char* const kGlobals[] = {
        "Object", "Math",    "JSON",   "String",   "Number",   "Boolean",
        "Array",  "parseInt", "parseFloat", "isNaN", "isFinite",
        "print",  "worker",  "parallel", "Promise", "Symbol",   "BigInt",
        // Core-language builtins tied to the event loop (spec §20, std/03):
        // no import required.
        "setTimeout", "setInterval", "clearTimeout", "clearInterval", "sleep",
    };
    const SourceSpan none{};
    for (const char* name : kGlobals) {
        declareBinding(name, ambient(), none, BindingSpec{.publish = false});
    }
}

// =============================================================================
// Hoisting
// =============================================================================
void Analyzer::hoistType(Stmt& s) {
    switch (s.kind) {
        case NodeKind::InterfaceDeclaration: {
            registerInterface(static_cast<InterfaceDeclaration&>(s));
            declaredTypes_.insert(s.nodeId);
            return;
        }
        case NodeKind::EnumDeclaration: {
            auto& e = static_cast<EnumDeclaration&>(s);
            typeNames_[e.name] = ambient();
            declaredTypes_.insert(s.nodeId);
            return;
        }
        case NodeKind::ClassDeclaration: {
            auto& c = static_cast<ClassDeclaration&>(s);
            typeNames_[c.name] = ambient();
            declaredTypes_.insert(s.nodeId);
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            auto& t = static_cast<TypeAliasDeclaration&>(s);
            typeNames_[t.name] = t.type ? resolveType(*t.type) : Type::unknown();
            declaredTypes_.insert(s.nodeId);
            return;
        }
        default:
            return;
    }
}

void Analyzer::hoistFunction(FunctionDeclaration& fn) {
    if (declaredFns_.count(fn.nodeId) != 0) return;
    declaredFns_.insert(fn.nodeId);

    if (fn.isAsync) {
        // async/await is out of scope for M1: report CP3002 once and expose a
        // permissive callable so the rest of the file does not cascade into
        // spurious diagnostics.
        notImplemented(fn.span, "async functions");
        TypePtr noReturn;
        FuncSig sig = buildSig(fn.name, fn.params, noReturn, nullptr);
        sig.annotatedReturn = false;
        sig.ret = Type::unknown();
        sig.declNodeId = fn.nodeId;
        sigs_[fn.nodeId] = sig;
        declareBinding(fn.name, ambient(), fn.span,
                       BindingSpec{.declNodeId = fn.nodeId});
        setType(fn, ambient());
        return;
    }

    if (!fn.typeParams.empty()) {
        notImplemented(fn.span, "generic functions");
        // Register the type parameters so `T` resolves to a TypeParam rather
        // than cascading into CP3302.
        registerTypeParams(fn.typeParams);
    }

    FuncSig sig = buildSig(fn.name, fn.params, fn.returnType, &fn.span);
    sig.declNodeId = fn.nodeId;
    if (!fn.typeParams.empty()) {
        // Generic function bodies are out of scope for M1. Keep the resolved
        // return type for callers but do not enforce return statements against
        // it (avoids spurious cascades after CP3002).
        sig.annotatedReturn = false;
    }
    Type fnType = Type::function(sig.params, sig.ret);
    declareBinding(fn.name, fnType, fn.span,
                   BindingSpec{.isConst = true, .isFunction = true,
                               .declNodeId = fn.nodeId, .sig = &sig});
    sigs_[fn.nodeId] = sig;
    publishFunction(sig, fn.name);
    setType(fn, fnType);
}

void Analyzer::registerTypeParams(TypeParamList& typeParams) {
    for (auto& tp : typeParams) {
        if (!tp) continue;
        typeNames_.emplace(tp->name, Type::named(TypeKind::TypeParam, tp->name));
    }
}

}  // namespace cp3
