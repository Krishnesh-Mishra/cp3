// =============================================================================
// src/sema/src/checks/class_check.cpp - user classes: model, resolution, access.
//
// Implements the spec §15 object-model surface (docs/language-spec/15-classes.md):
// `resolveClasses` builds one `ClassInfo` member table per non-generic class
// declaration before the body/visit pass, `checkClassBodies` checks field
// initializers and method/constructor bodies with `this` bound, and
// `userClassMember` resolves instance/static member reads with the privacy,
// weak-field (spec §11) and CP3305/CP3802 diagnostics. Structural conformance
// and `new` live in class_conform_check.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Resolution
// =============================================================================
void Analyzer::resolveClasses(StmtList& body) {
    for (auto& s : body) {
        if (!s || s->kind != NodeKind::ClassDeclaration) continue;
        auto& c = static_cast<ClassDeclaration&>(*s);
        if (!classResolved_.insert(c.nodeId).second) continue;
        resolveClass(c);
    }
}

void Analyzer::enterClassTypeParams(const TypeParamList& typeParams,
                                    TypeParamScope& saved) {
    for (const auto& tp : typeParams) {
        if (!tp) continue;
        const std::string& name = tp->name;
        if (name.empty()) continue;
        const auto it = typeNames_.find(name);
        std::optional<Type> old;
        if (it != typeNames_.end()) old = it->second;
        saved.push_back(std::make_pair(name, old));
        typeNames_[name] = Type::named(TypeKind::TypeParam, name);
    }
}

void Analyzer::leaveClassTypeParams(const TypeParamScope& saved) {
    for (const auto& [name, old] : saved) {
        if (old) {
            typeNames_[name] = *old;
        } else {
            typeNames_.erase(name);
        }
    }
}

Type Analyzer::substituteClassTypeParams(const Type& t, const ClassInfo& ci,
                                         const std::vector<Type>& args) const {
    if (t.kind == TypeKind::TypeParam) {
        for (std::size_t i = 0; i < ci.typeParamNames.size(); ++i) {
            if (ci.typeParamNames[i] != t.name) continue;
            if (i < args.size()) return args[i];
            return t;
        }
        return t;
    }
    Type out = t;
    for (Type& part : out.parts) {
        part = substituteClassTypeParams(part, ci, args);
    }
    return out;
}

void Analyzer::resolveClass(ClassDeclaration& c) {
    ClassInfo info;
    info.name = c.name;
    info.span = c.span;
    info.declNodeId = c.nodeId;
    info.isAbstract = c.isAbstract;
    for (const auto& tp : c.typeParams) {
        if (tp) info.typeParamNames.push_back(tp->name);
    }
    // A generic class's member types resolve with its parameters in scope.
    TypeParamScope saved;
    enterClassTypeParams(c.typeParams, saved);
    collectClassMembers(c, info);
    leaveClassTypeParams(saved);
    // The doc comments of the class and of its members (class_docs.cpp).
    publishClassDocs(c);
    const std::string name = info.name;
    classes_[name] = std::move(info);
    // The class name is also a value (statics + `new`), declared in the
    // resolve pass so a function above the class can use it.
    typeNames_[name] = Type::named(TypeKind::Class, name);
    declareBinding(name, typeNames_[name], c.span,
                   BindingSpec{.declNodeId = c.nodeId});
    checkAbstractConformance(c, classes_[name]);
}

void Analyzer::collectClassMembers(const ClassDeclaration& c, ClassInfo& info) {
    if (c.extendsType) {
        Type bt = resolveType(*c.extendsType);
        if (bt.kind == TypeKind::Class && !bt.name.empty()) {
            info.baseName = bt.name;
        }
    }
    for (const TypePtr& t : c.implementsTypes) {
        if (t) resolveType(*t);  // self-documenting only (spec §15)
    }
    for (const ClassMemberPtr& mp : c.members) {
        if (mp) collectClassMember(*mp, info);
    }
}

// A class member's name: the key of a property, method or accessor. A
// computed key (`[expr]`) names the member at runtime, which a cp3 class
// (a fixed set of members) cannot express - unless the key is a literal, the
// one form whose value is known at compile time.
std::string Analyzer::classMemberName(Expr* key, bool computed) {
    if (key == nullptr) return {};
    if (computed && key->kind != NodeKind::StringLiteral &&
        key->kind != NodeKind::NumberLiteral) {
        return {};
    }
    return keyName(key);
}

// One property member: its name/key, modifiers and resolved field type.
void Analyzer::collectClassProperty(PropertyDeclaration& p, ClassInfo& info) {
    std::string name = classMemberName(p.key.get(), p.computed);
    if (name.empty()) {
        report(diagcode::kComputedMemberName, p.span,
               "a computed class member name must be a literal: a cp3 class "
               "has a fixed set of members");
        return;
    }
    ClassMemberInfo mi;
    mi.name = name;
    mi.span = p.span;
    mi.isStatic = p.isStatic;
    mi.isPrivate = p.access == AccessModifier::Private ||
                   (!name.empty() && name.front() == '#');
    mi.isProtected = p.access == AccessModifier::Protected;
    mi.isWeak = p.isWeak;
    mi.isReadonly = p.isReadonly;
    mi.isOverride = p.isOverride;
    mi.hasInitializer = p.initializer != nullptr;
    mi.optional = p.optional;
    if (p.type) {
        mi.type = resolveType(*p.type);
    } else if (p.initializer) {
        mi.type = widen(inferExpr(*p.initializer));
    } else {
        mi.type = Type::unknown();
    }
    info.members.push_back(std::move(mi));
}

// One method member: its signature (a same-name method extends the overloads).
void Analyzer::collectClassMethod(MethodDeclaration& md, ClassInfo& info) {
    std::string name = classMemberName(md.key.get(), md.computed);
    if (name.empty()) {
        report(diagcode::kComputedMemberName, md.span,
               "a computed class member name must be a literal: a cp3 class "
               "has a fixed set of members");
        return;
    }
    // A generic method's own type parameters are in scope while its signature
    // is built, so `T` in an annotation resolves to a type parameter.
    TypeParamScope saved;
    enterClassTypeParams(md.typeParams, saved);
    FuncSig sig = buildSig(name, md.params, md.returnType, &md.span);
    leaveClassTypeParams(saved);
    sig.declNodeId = md.nodeId;
    sigs_[md.nodeId] = sig;
    ClassMemberInfo* prev = nullptr;
    for (ClassMemberInfo& cm : info.members) {
        if (cm.isMethod && cm.name == name) {
            prev = &cm;
            break;
        }
    }
    if (prev) {
        // Overload set: resolution stays permissive, C++ picks.
        if (prev->overloads.empty()) {
            prev->overloads.push_back(prev->sig);
        }
        prev->overloads.push_back(sig);
        prev->type = ambient();
        return;
    }
    ClassMemberInfo mi;
    mi.name = name;
    mi.span = md.span;
    mi.isStatic = md.isStatic;
    mi.isPrivate = md.access == AccessModifier::Private ||
                   (!name.empty() && name.front() == '#');
    mi.isProtected = md.access == AccessModifier::Protected;
    mi.isMethod = true;
    mi.isAbstract = md.isAbstract;
    mi.isReadonly = md.isReadonly;
    mi.isOverride = md.isOverride;
    mi.optional = md.optional;
    mi.sig = sig;
    mi.type = Type::function(sig.params, sig.ret);
    info.members.push_back(std::move(mi));
}

void Analyzer::collectClassMember(ClassMember& m, ClassInfo& info) {
    switch (m.kind) {
        case NodeKind::PropertyDeclaration:
            collectClassProperty(static_cast<PropertyDeclaration&>(m), info);
            return;
        case NodeKind::MethodDeclaration:
            collectClassMethod(static_cast<MethodDeclaration&>(m), info);
            return;
        case NodeKind::ConstructorDeclaration: {
            auto& cd = static_cast<ConstructorDeclaration&>(m);
            TypePtr none;
            FuncSig sig = buildSig("constructor", cd.params, none, nullptr);
            sig.declNodeId = cd.nodeId;
            sigs_[cd.nodeId] = sig;
            info.ctorSig = sig;
            info.hasCtor = true;
            // Constructor parameter properties (`constructor(public x: T)`)
            // declare an instance field and initialize it from the argument.
            for (const auto& pp : cd.params) {
                if (!pp) continue;
                if (pp->access == AccessModifier::None && !pp->isReadonly) {
                    continue;
                }
                if (pp->isRest || !pp->name ||
                    pp->name->kind != NodeKind::Identifier) {
                    report(diagcode::kUnexpectedToken, pp->span,
                           "a constructor parameter property must be a simple "
                           "identifier");
                    continue;
                }
                const auto& id = static_cast<const Identifier&>(*pp->name);
                bool duplicate = false;
                for (const ClassMemberInfo& existing : info.members) {
                    if (existing.name == id.name) duplicate = true;
                }
                if (duplicate) {
                    report(diagcode::kDuplicateMember, pp->span,
                           "duplicate class member '" + id.name + "'");
                    continue;
                }
                ClassMemberInfo field;
                field.name = id.name;
                field.span = pp->span;
                field.isPrivate = pp->access == AccessModifier::Private;
                field.isProtected = pp->access == AccessModifier::Protected;
                field.isReadonly = pp->isReadonly;
                field.hasInitializer = true;
                field.optional = pp->optional;
                if (pp->type) {
                    field.type = resolveType(*pp->type);
                } else if (pp->defaultValue) {
                    field.type = widen(inferExpr(*pp->defaultValue));
                } else {
                    report(diagcode::kTypeMismatch, pp->span,
                           "parameter property '" + id.name +
                               "' needs a type annotation or a default value");
                    field.type = Type::unknown();
                }
                if (pp->optional) field.type = nullableIf(true, field.type);
                info.members.push_back(std::move(field));
            }
            return;
        }
        case NodeKind::AccessorDeclaration: {
            auto& ad = static_cast<AccessorDeclaration&>(m);
            // D21: TS-style accessors. The getter's return type governs reads;
            // the setter writes through the same type.
            std::string name = classMemberName(ad.key.get(), ad.computed);
            if (name.empty()) {
                report(diagcode::kComputedMemberName, ad.span,
                       "a computed class member name must be a literal: a cp3 "
                       "class has a fixed set of members");
                return;
            }
            if (ad.accessor != AccessorKind::Get) return;
            ClassMemberInfo mi;
            mi.name = name;
            mi.span = ad.span;
            mi.isStatic = ad.isStatic;
            mi.isPrivate = ad.access == AccessModifier::Private ||
                           (!name.empty() && name.front() == '#');
            mi.isProtected = ad.access == AccessModifier::Protected;
            mi.hasInitializer = true;  // a getter satisfies every read
            mi.type = ad.returnType ? resolveType(*ad.returnType)
                                    : Type::unknown();
            info.members.push_back(std::move(mi));
            return;
        }
        default:
            return;
    }
}

// =============================================================================
// Abstract conformance + bodies
// =============================================================================
void Analyzer::checkClassHierarchy(const ClassDeclaration& c,
                                   const ClassInfo& ci) {
    (void)c;
    const ClassInfo* base =
        ci.baseName.empty() ? nullptr : findClass(ci.baseName);
    for (const ClassMemberInfo& m : ci.members) {
        if (!m.isOverride) continue;
        const ClassMemberInfo* inherited =
            base ? findClassMemberChain(*base, m.name, nullptr) : nullptr;
        if (inherited == nullptr) {
            report(diagcode::kMemberNotFound, m.span,
                   "member '" + m.name + "' is marked override but " +
                       (base ? "base class '" + base->name + "' has no such "
                               "member"
                             : "the class has no base class"));
            continue;
        }
        if (m.isMethod != inherited->isMethod) {
            report(diagcode::kTypeMismatch, m.span,
                   "member '" + m.name +
                       "' cannot override a member of a different kind");
        }
    }
}

void Analyzer::checkClassBodies(ClassDeclaration& c) {
    if (const ClassInfo* ci = findClass(c.name)) {
        checkClassHierarchy(c, *ci);
    }
    if (!findClass(c.name)) return;
    // Generic class bodies check once, with the parameters bound to TypeParam.
    TypeParamScope saved;
    enterClassTypeParams(c.typeParams, saved);
    classStack_.push_back(c.name);
    for (const ClassMemberPtr& mp : c.members) {
        if (mp) checkClassMemberBody(*mp);
    }
    classStack_.pop_back();
    leaveClassTypeParams(saved);
}

void Analyzer::checkClassMemberBody(ClassMember& m) {
    switch (m.kind) {
        case NodeKind::PropertyDeclaration: {
            auto& p = static_cast<PropertyDeclaration&>(m);
            // Unannotated initializers were typed during resolution.
            if (!p.initializer || !p.type) return;
            pushScope();
            declareBinding("this", thisType(), p.span,
                           BindingSpec{.publish = false});
            Type ft = resolveType(*p.type);
            Type it = inferExpr(*p.initializer);
            checkAssign(it, ft, p.initializer->span,
                        p.initializer->kind == NodeKind::ObjectLiteral);
            popScope();
            return;
        }
        case NodeKind::MethodDeclaration: {
            auto& md = static_cast<MethodDeclaration&>(m);
            if (md.body) analyzeMethodBody(md);
            return;
        }
        case NodeKind::ConstructorDeclaration:
            analyzeCtorBody(static_cast<ConstructorDeclaration&>(m));
            return;
        case NodeKind::AccessorDeclaration:
            analyzeAccessorBody(static_cast<AccessorDeclaration&>(m));
            return;
        case NodeKind::StaticBlock: {
            auto& sb = static_cast<StaticBlock&>(m);
            // A static block has no `this`; static members are reached through
            // the class name. It is checked in the enclosing class scope.
            pushScope();
            if (sb.body) visitStmt(*sb.body);
            popScope();
            return;
        }
        default:
            return;
    }
}

void Analyzer::analyzeMethodBody(MethodDeclaration& md) {
    const FuncSig& sig = sigs_[md.nodeId];
    // A generic method's type parameters are in scope in its body (§16).
    TypeParamScope saved;
    enterClassTypeParams(md.typeParams, saved);
    pushScope();
    for (std::size_t i = 0; i < md.params.size() && i < sig.params.size(); ++i) {
        Parameter* p = md.params[i].get();
        if (!p) continue;
        if (p->isRest) {
            declarePattern(p->name.get(), Type::array(sig.params[i]), false,
                           p->nodeId);
        } else {
            declarePattern(p->name.get(), nullableIf(p->optional, sig.params[i]),
                           false, p->nodeId);
        }
    }
    declareBinding("this", thisType(), md.span, BindingSpec{.publish = false});
    FuncCtx ctx;
    ctx.annotated = sig.annotatedReturn;
    ctx.ret = sig.ret;
    funcStack_.push_back(ctx);
    // A method is a function of its own: a `break`/`continue` inside it binds
    // to its own loops, never to a loop around the class declaration.
    const FunctionContext outer = enterFunctionBody();
    if (md.body) visitStmt(*md.body);
    leaveFunctionBody(outer);
    funcStack_.pop_back();
    popScope();
    if (sig.annotatedReturn && !sig.ret.isVoid() && !sig.ret.isUndefined() &&
        sig.ret.kind != TypeKind::Unknown) {
        if (md.body && !allPathsReturn(md.body.get())) {
            report(diagcode::kMissingReturn, md.span,
                   "method '" + sig.name +
                       "' is declared to return a value but may fall off "
                       "the end of its body");
        }
    }
    leaveClassTypeParams(saved);
}

// The accessor body (D21): `this` plus the setter's parameter are in scope.
void Analyzer::analyzeAccessorBody(AccessorDeclaration& ad) {
    if (!ad.body) return;
    pushScope();
    declareBinding("this", thisType(), ad.span, BindingSpec{.publish = false});
    for (const auto& p : ad.params) {
        if (!p) continue;
        Type t = p->type ? resolveType(*p->type) : Type::unknown();
        if (p->optional && !t.isUnknown()) t = nullableIf(true, t);
        declarePattern(p->name.get(), t, false, p->nodeId);
    }
    FuncCtx ctx;
    ctx.annotated = ad.returnType != nullptr;
    ctx.ret = ad.returnType ? resolveType(*ad.returnType) : Type::unknown();
    funcStack_.push_back(ctx);
    const FunctionContext outer = enterFunctionBody();
    visitStmt(*ad.body);
    leaveFunctionBody(outer);
    funcStack_.pop_back();
    popScope();
}

void Analyzer::analyzeCtorBody(ConstructorDeclaration& cd) {
    const FuncSig& sig = sigs_[cd.nodeId];
    const ClassInfo* ci =
        classStack_.empty() ? nullptr : findClass(classStack_.back());
    const bool derived = ci != nullptr && !ci->baseName.empty();
    pushScope();
    for (std::size_t i = 0; i < cd.params.size() && i < sig.params.size(); ++i) {
        Parameter* p = cd.params[i].get();
        if (!p) continue;
        if (p->isRest) {
            declarePattern(p->name.get(), Type::array(sig.params[i]), false,
                           p->nodeId);
        } else {
            declarePattern(p->name.get(), nullableIf(p->optional, sig.params[i]),
                           false, p->nodeId);
        }
    }
    declareBinding("this", thisType(), cd.span, BindingSpec{.publish = false});
    FuncCtx ctx;
    ctx.annotated = false;
    ctx.ret = Type::voidType();
    ctx.isCtor = true;
    funcStack_.push_back(ctx);
    const FunctionContext outer = enterFunctionBody();
    if (cd.body) visitStmt(*cd.body);
    if (derived && cd.body != nullptr && !funcStack_.back().sawSuper) {
        report(diagcode::kNotCallable, cd.span,
               "constructor of class '" + classStack_.back() +
                   "' must call super(...) because it derives from '" +
                   ci->baseName + "'");
    }
    leaveFunctionBody(outer);
    funcStack_.pop_back();
    popScope();
}

Type Analyzer::thisType() const {
    if (classStack_.empty()) return ambient();
    return Type::named(TypeKind::Class, classStack_.back());
}

bool Analyzer::isErrorType(const Type& type) const {
    if (type.kind != TypeKind::Class) return false;
    const ClassInfo* ci = findClass(type.name);
    int guard = 0;
    while (ci != nullptr && guard++ < 64) {
        if (isErrorClassName(ci->name)) return true;
        ci = ci->baseName.empty() ? nullptr : findClass(ci->baseName);
    }
    return false;
}

// =============================================================================
// Member lookup + access
// =============================================================================
const ClassInfo* Analyzer::findClass(const std::string& name) const {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

const ClassInfo* Analyzer::constructorOwner(const ClassInfo& ci) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        if (k->hasCtor) return k;
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    return nullptr;
}

const ClassMemberInfo* Analyzer::findClassMember(const ClassInfo& ci,
                                                 const std::string& name) const {
    for (const ClassMemberInfo& m : ci.members) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

const ClassMemberInfo* Analyzer::findClassMemberChain(
    const ClassInfo& ci, const std::string& name, const ClassInfo** owner) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k && guard++ < 64) {
        const ClassMemberInfo* m = findClassMember(*k, name);
        if (m) {
            if (owner) *owner = k;
            return m;
        }
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    if (owner) *owner = nullptr;
    return nullptr;
}

bool Analyzer::classIsOrDerivesFrom(const ClassInfo& ci,
                                    const std::string& base) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k && guard++ < 64) {
        if (k->name == base) return true;
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    return false;
}

bool Analyzer::classAccessAllowed(const ClassInfo& owner,
                                  const ClassMemberInfo& m) const {
    if (!m.isPrivate && !m.isProtected) return true;
    for (const std::string& frame : classStack_) {
        if (m.isPrivate) {
            if (frame == owner.name) return true;
        } else {
            const ClassInfo* f = findClass(frame);
            if (f && classIsOrDerivesFrom(*f, owner.name)) return true;
        }
    }
    return false;
}

Type Analyzer::classMemberType(const ClassMemberInfo& m) const {
    if (!m.isMethod) return m.type;
    if (!m.overloads.empty()) return ambient();
    Type t = Type::function(m.sig.params, m.sig.ret);
    // A rest parameter takes an unbounded argument list, which the fixed-arity
    // function type cannot express: the tag widens the signature's `rest` flag
    // at the call site (the marker the variadic array mutators use, scopes.h).
    if (m.sig.rest) t.name = kVariadicOptionalTailLabel;
    return t;
}

Type Analyzer::userClassMember(const Type& cls, const std::string& prop,
                               const SourceSpan& span) {
    const ClassInfo* ci = findClass(cls.name);
    if (!ci) return ambient();
    const ClassInfo* owner = nullptr;
    const ClassMemberInfo* m = findClassMemberChain(*ci, prop, &owner);
    if (!m) {
        if (assignTarget_) {
            report(diagcode::kUndeclaredProperty, span,
                   "cannot assign to undeclared property '" + prop +
                       "' of class '" + ci->name + "'");
        } else {
            std::vector<std::string> candidates;
            candidates.reserve(ci->members.size());
            for (const ClassMemberInfo& cm : ci->members) {
                candidates.push_back(cm.name);
            }
            reportMemberMiss("class '" + ci->name + "'", prop, span, candidates);
        }
        return Type::unknown();
    }
    if (!classAccessAllowed(*owner, *m)) {
        report(diagcode::kMemberNotFound, span,
               "property '" + prop + "' of class '" + owner->name + "' is " +
                   (m->isPrivate ? "private" : "protected"));
        return Type::unknown();
    }
    if (assignTarget_ && m->isReadonly) {
        const bool inOwnConstructor =
            !classStack_.empty() && classStack_.back() == owner->name &&
            !funcStack_.empty() && funcStack_.back().isCtor;
        if (!inOwnConstructor) {
            report(diagcode::kTypeMismatch, span,
                   "cannot assign to readonly property '" + prop +
                       "' outside its class constructor");
            return Type::unknown();
        }
    }
    Type t = classMemberType(*m);
    // A generic class instantiation substitutes its type arguments into the
    // member type (`Box<number>.value` is number, not T).
    if (!ci->typeParamNames.empty() && !cls.parts.empty()) {
        t = substituteClassTypeParams(t, *ci, cls.parts);
    }
    if (m->isWeak && !assignTarget_) {
        weakFieldHit_ = true;
        if (!hasNullish(t)) t = unify(t, Type::undefined());
    }
    return t;
}

// -----------------------------------------------------------------------------
// Stage 1: cycle-capability analysis (automatic cycle handling).
//
// Builds the strong-edge class graph from field types (arrays, Map/Set
// values, unions, function parts and closures included; `weak` fields are not
// strong edges) and marks every class that can reach itself through strong
// edges. An `unknown`-typed field is an opaque sink: it marks its source
// cycle-capable (fail closed). Report-only in v0.2: the set is published to
// SemaResult::cycleCapableClasses; no diagnostic is emitted from it yet, and
// plain refcounting is unchanged for every type.
// -----------------------------------------------------------------------------
void Analyzer::computeCycleCapable() {
    std::unordered_map<std::string, std::unordered_set<std::string>> graph;
    std::unordered_set<std::string> unknownRisk;
    std::function<void(const Type&, std::unordered_set<std::string>&)> walk =
        [&](const Type& t, std::unordered_set<std::string>& out) {
            if (t.isUnknown()) {
                out.insert("$unknown");
                return;
            }
            if (t.kind == TypeKind::Class) {
                if (classes_.count(t.name)) out.insert(t.name);
            }
            for (const Type& p : t.parts) walk(p, out);
        };
    for (const auto& [name, info] : classes_) {
        auto& edges = graph[name];
        for (const auto& m : info.members) {
            if (m.isMethod || m.isStatic) continue;
            if (m.isWeak) continue;  // weak edges do not count toward cycles
            std::unordered_set<std::string> refs;
            walk(m.type, refs);
            for (const std::string& r : refs) {
                if (r == "$unknown") unknownRisk.insert(name);
                else if (classes_.count(r)) edges.insert(r);
            }
        }
    }
    // Reachability: a class is cycle-capable when it can reach itself through
    // strong edges (covers self-loops and mutual/longer cycles).
    cycleCapableClasses_.clear();
    for (const auto& [name, info] : classes_) {
        (void)info;
        if (unknownRisk.count(name)) {
            cycleCapableClasses_.insert(name);
            continue;
        }
        std::unordered_set<std::string> seen;
        std::vector<std::string> work(graph[name].begin(), graph[name].end());
        bool reachesSelf = false;
        while (!work.empty() && !reachesSelf) {
            std::string cur = work.back();
            work.pop_back();
            if (cur == name) {
                reachesSelf = true;
                break;
            }
            if (!seen.insert(cur).second) continue;
            for (const std::string& nxt : graph[cur]) work.push_back(nxt);
        }
        if (reachesSelf) cycleCapableClasses_.insert(name);
    }
    out_.cycleCapableClasses.assign(cycleCapableClasses_.begin(),
                                    cycleCapableClasses_.end());
    std::sort(out_.cycleCapableClasses.begin(), out_.cycleCapableClasses.end());
}

}  // namespace cp3
