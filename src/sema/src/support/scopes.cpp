// =============================================================================
// src/sema/src/support/scopes.cpp - scope stack, declaration tables and hoisting.
//
// Owns the lexical scope stack, `lookup`, the `declareBinding` /
// `declareAmbient` / `overrideBinding` primitives, the builtin global table,
// and the hoisting passes (`hoistType` / `hoistFunction` / `registerTypeParams`)
// that make later annotations visible to earlier ones (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

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

std::vector<std::string> Analyzer::visibleNames() const {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        for (const auto& entry : *scope) {
            if (seen.insert(entry.first).second) names.push_back(entry.first);
        }
    }
    return names;
}

void Analyzer::declareBinding(const std::string& name, const Type& type,
                              const SourceSpan& span, const BindingSpec& spec) {
    auto& scope = scopes_.back();
    auto existing = scope.find(name);
    if (existing != scope.end()) {
        // A `var` declaration replaces the `undefined`-typed placeholder the
        // hoisting pass inserted for it in this scope (spec §03).
        if (existing->second.hoistedVar && spec.isVar) {
            scope.erase(existing);
        } else if (existing->second.isFunction && spec.isFunction) {
            // Function overloading (spec §17): a second function with the same
            // name in the same scope is an overload set, not a duplicate. Record
            // every candidate signature so the call site can resolve against the
            // set; expose the name permissively otherwise.
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
        } else {
            report(diagcode::kDuplicateDeclaration, span,
                   "duplicate declaration of '" + name + "'");
            return;
        }
    }
    Binding b;
    b.name = name;
    b.type = type;
    b.isConst = spec.isConst;
    b.isFunction = spec.isFunction;
    b.isVar = spec.isVar;
    b.hoistedVar = spec.hoistedVar;
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
    // Math and JSON are deliberately NOT here: docs/standard-library/04
    // requires `import Math from "std/math"` (and the JSON twin), the same as
    // every other std surface, so a bare `Math` is CP3301 with the exact
    // import statement as a suggestion (stdImportHint, std_check.cpp).
    static const char* const kGlobals[] = {
        "Object", "String",   "Number",   "Boolean",
        "Array",  "parseInt", "parseFloat", "isNaN", "isFinite",
        "print",  "worker",  "parallel", "Promise", "Symbol",   "BigInt",
        // The worker channel types (D80, spec 13): ambient generic types in
        // every unit. `Worker` also has a value binding, so `new Worker(...)`
        // resolves (the surface is checked in codegen/the CLI, which own the
        // module resolution the constructor needs).
        "Worker", "Thread",
        // The core-language event type (D74, spec §14): a global builtin
        // exactly like `Promise` and the timers, no import required.
        "EventEmitter",
        // Core-language builtins tied to the event loop (spec §20, std/03):
        // no import required.
        "setTimeout", "setInterval", "clearTimeout", "clearInterval", "sleep",
        // The JS-compat typed-array family (D69): ambient constructors, no
        // import required. The constructors themselves are checked in
        // src/std/typed-arrays/sema.cpp; codegen maps them to cp3/typed_array.hpp.
        "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array",
        "Uint16Array", "Int32Array", "Uint32Array", "Float32Array",
        "Float64Array", "BigInt64Array", "BigUint64Array", "ArrayBuffer",
        "SharedArrayBuffer", "DataView",
    };
    const SourceSpan none{};
    for (const char* name : kGlobals) {
        declareBinding(name, ambient(), none, BindingSpec{.publish = false});
    }
    // The numeric constants the coercion rules are pinned against.
    declareBinding("NaN", Type::number(), none, BindingSpec{.publish = false});
    declareBinding("Infinity", Type::number(), none,
                   BindingSpec{.publish = false});
    // The family class names also resolve as type annotations (`Int8Array`,
    // `ArrayBuffer`, `DataView`, ...).
    for (const std::string& name : familyTypeNames()) {
        typeNames_.emplace(name, Type::named(TypeKind::Class, name));
    }
    // The `std/date` class name resolves as a type annotation too (`let d:
    // Date`), matching the family names above; the VALUE still requires
    // `import Date from "std/date";`, which is where the import hint points.
    typeNames_.emplace("Date", Type::named(TypeKind::Class, "Date"));
    // `EventEmitter` resolves as a type annotation too: the ambient binding is
    // a constructor value, the nominal class type is what `new` produces
    // (D74; the members are typed in member_check.cpp).
    typeNames_.emplace("EventEmitter", Type::named(TypeKind::Class, "EventEmitter"));
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
            registerEnum(e);
            return;
        }
        case NodeKind::ClassDeclaration: {
            // Classes are nominal types (spec 15); a generic class is a type
            // constructor whose instantiations carry their type arguments in
            // the Type parts.
            auto& c = static_cast<ClassDeclaration&>(s);
            typeNames_[c.name] = Type::named(TypeKind::Class, c.name);
            declaredTypes_.insert(s.nodeId);
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            auto& t = static_cast<TypeAliasDeclaration&>(s);
            // A type alias is a compile-time name for a type; its own type
            // parameters are in scope while the aliased type is resolved.
            declareTypeAlias(t);
            return;
        }
        default:
            return;
    }
}

void Analyzer::hoistFunction(FunctionDeclaration& fn) {
    if (declaredFns_.count(fn.nodeId) != 0) return;
    declaredFns_.insert(fn.nodeId);

    // Generic functions (spec §16): the type parameters are registered before
    // the signature is built so `T` resolves to a TypeParam (carrying its
    // `extends` constraint in `parts[0]`) inside every annotation.
    registerTypeParams(fn.typeParams);

    FuncSig sig = buildSig(fn.name, fn.params, fn.returnType, &fn.span);
    sig.declNodeId = fn.nodeId;
    for (auto& tp : fn.typeParams) {
        if (!tp) continue;
        auto it = typeNames_.find(tp->name);
        if (it != typeNames_.end()) sig.typeParams.push_back(it->second);
    }
    if (fn.isAsync) {
        // spec §14: calling an async function yields a Promise. The annotation
        // is `Promise<T>`, and the body's `return` statements are checked
        // against T (the unwrapped value type).
        sig.isAsync = true;
        if (sig.annotatedReturn && isPromiseType(sig.ret)) {
            sig.asyncValueRet = promiseValue(sig.ret);
        } else if (sig.annotatedReturn && !sig.ret.isUnknown() && !sig.ret.isError()) {
            // Permissive: `async function f(): T` behaves like `Promise<T>`.
            sig.asyncValueRet = sig.ret;
            sig.ret = promiseOf(sig.ret);
        } else {
            sig.annotatedReturn = false;
            sig.ret = promiseOf(Type::unknown());
            sig.asyncValueRet = Type::unknown();
        }
    }
    Type fnType = Type::function(sig.params, sig.ret);
    declareBinding(fn.name, fnType, fn.span,
                   BindingSpec{.isConst = true, .isFunction = true,
                               .declNodeId = fn.nodeId, .sig = &sig});
    sigs_[fn.nodeId] = sig;
    publishFunction(sig, fn.name);
    publishDoc(fn.nodeId, fn.span);
    setType(fn, fnType);
}

void Analyzer::registerTypeParams(TypeParamList& typeParams) {
    for (auto& tp : typeParams) {
        if (!tp) continue;
        Type t = Type::named(TypeKind::TypeParam, tp->name);
        if (tp->constraint) {
            Type constraint = resolveType(*tp->constraint);
            // Remember a named constraint's spelling so a violation diagnostic
            // can print `Shape` rather than the resolved shape's `object`.
            if (tp->constraint->kind == NodeKind::NamedType) {
                typeParamConstraintNames_[typeKey(constraint)] =
                    static_cast<NamedType&>(*tp->constraint).name;
            }
            t.parts.push_back(std::move(constraint));
        }
        // Bind the name to the type parameter unless a real declared type
        // already owns it (a type parameter must never shadow `Point`, say).
        auto it = typeNames_.find(tp->name);
        if (it == typeNames_.end() || it->second.kind == TypeKind::TypeParam) {
            typeNames_[tp->name] = std::move(t);
        }
    }
}

// =============================================================================
// `var` hoisting (spec §03)
//
// A `var` binding belongs to the enclosing function scope (the module scope at
// the top level) and a read before its declaration sees `undefined`. This pass
// pre-declares every `var` name in the statement list - descending through
// nested blocks, loops and try/switch bodies, but never into nested functions -
// with type `undefined`. The real declaration later takes the placeholder's
// place (same scope) or shadows it (nested block); see declareBinding.
// =============================================================================
void Analyzer::hoistVars(StmtList& body) {
    for (auto& s : body) {
        if (s) hoistVarsIn(*s);
    }
}

void Analyzer::hoistVarBody(StmtPtr& body) {
    if (body) hoistVarsIn(*body);
}

void Analyzer::hoistVarsIn(Stmt& s) {
    switch (s.kind) {
        case NodeKind::VariableDeclaration: {
            auto& vd = static_cast<VariableDeclaration&>(s);
            if (vd.kind != VarKind::Var) return;
            for (auto& d : vd.declarations) {
                if (!d || !d->id || d->id->kind != NodeKind::Identifier) continue;
                auto& id = static_cast<Identifier&>(*d->id);
                if (scopes_.back().count(id.name) != 0) continue;
                declareBinding(id.name, Type::undefined(), id.span,
                               BindingSpec{.isVar = true, .hoistedVar = true,
                                           .publish = false});
            }
            return;
        }
        case NodeKind::BlockStatement:
            return hoistVars(static_cast<BlockStatement&>(s).body);
        case NodeKind::IfStatement: {
            auto& is = static_cast<IfStatement&>(s);
            hoistVarBody(is.thenBranch);
            hoistVarBody(is.elseBranch);
            return;
        }
        case NodeKind::WhileStatement:
            return hoistVarBody(static_cast<WhileStatement&>(s).body);
        case NodeKind::DoWhileStatement:
            return hoistVarBody(static_cast<DoWhileStatement&>(s).body);
        case NodeKind::ForStatement: {
            auto& f = static_cast<ForStatement&>(s);
            if (f.init && f.init->kind == NodeKind::VariableDeclaration) {
                hoistVarsIn(static_cast<Stmt&>(*f.init));
            }
            return hoistVarBody(f.body);
        }
        case NodeKind::ForOfStatement: {
            auto& f = static_cast<ForOfStatement&>(s);
            if (f.left && f.left->kind == NodeKind::VariableDeclaration) {
                hoistVarsIn(static_cast<Stmt&>(*f.left));
            }
            return hoistVarBody(f.body);
        }
        case NodeKind::ForInStatement: {
            auto& f = static_cast<ForInStatement&>(s);
            if (f.left && f.left->kind == NodeKind::VariableDeclaration) {
                hoistVarsIn(static_cast<Stmt&>(*f.left));
            }
            return hoistVarBody(f.body);
        }
        case NodeKind::TryStatement: {
            auto& t = static_cast<TryStatement&>(s);
            hoistVarBody(t.tryBlock);
            if (t.handler) hoistVarBody(t.handler->body);
            hoistVarBody(t.finallyBlock);
            return;
        }
        case NodeKind::SwitchStatement: {
            for (auto& c : static_cast<SwitchStatement&>(s).cases) {
                if (!c) continue;
                for (auto& st : c->statements) {
                    if (st) hoistVarsIn(*st);
                }
            }
            return;
        }
        case NodeKind::LabeledStatement:
            return hoistVarBody(static_cast<LabeledStatement&>(s).body);
        default:
            return;  // expression/return/etc. have no nested statements
    }
}

}  // namespace cp3
