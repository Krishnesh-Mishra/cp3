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

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

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
    // A generic class's member types resolve with its parameters in scope, so
    // `value: T` becomes TypeParam("T") in the member table.
    TypeParamScope saved;
    enterClassTypeParams(c.typeParams, saved);
    collectClassMembers(c, info);
    leaveClassTypeParams(saved);
    // The doc comments of the class and of its members (class_docs.cpp).
    publishClassDocs(c);
    const std::string name = info.name;
    classes_[name] = std::move(info);
    // The class name is also a value (statics + `new`); declaring it in the
    // resolve pass keeps functions declared above the class able to use it.
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

// One property member: its name/key, modifiers and resolved field type.
void Analyzer::collectClassProperty(PropertyDeclaration& p, ClassInfo& info) {
    std::string name = keyName(p.key.get());
    if (name.empty()) {
        notImplemented(p.span, "computed class member names");
        return;
    }
    ClassMemberInfo mi;
    mi.name = name;
    mi.span = p.span;
    mi.isStatic = p.isStatic;
    mi.isPrivate = p.access == AccessModifier::Private;
    mi.isProtected = p.access == AccessModifier::Protected;
    mi.isWeak = p.isWeak;
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
    std::string name = keyName(md.key.get());
    if (name.empty()) {
        notImplemented(md.span, "computed class member names");
        return;
    }
    if (!md.typeParams.empty()) {
        notImplemented(md.span, "generic methods");
    }
    FuncSig sig = buildSig(name, md.params, md.returnType, &md.span);
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
    mi.isPrivate = md.access == AccessModifier::Private;
    mi.isProtected = md.access == AccessModifier::Protected;
    mi.isMethod = true;
    mi.isAbstract = md.isAbstract;
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
            return;
        }
        case NodeKind::AccessorDeclaration: {
            auto& ad = static_cast<AccessorDeclaration&>(m);
            // D21: TS-style accessors. The getter's return type governs reads;
            // the setter writes through the same type.
            if (ad.accessor != AccessorKind::Get) return;
            std::string name = keyName(ad.key.get());
            if (name.empty()) return;
            ClassMemberInfo mi;
            mi.name = name;
            mi.span = ad.span;
            mi.isStatic = ad.isStatic;
            mi.isPrivate = ad.access == AccessModifier::Private;
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
bool Analyzer::memberImplementedInClass(const ClassInfo& ci,
                                        const std::string& name) const {
    const ClassInfo* k = &ci;
    int guard = 0;
    while (k && guard++ < 64) {
        const ClassMemberInfo* m = findClassMember(*k, name);
        if (m) return !m->isMethod || !m->isAbstract;
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    return false;
}

void Analyzer::checkAbstractConformance(const ClassDeclaration& c,
                                        const ClassInfo& ci) {
    if (ci.isAbstract) return;
    std::string missing;
    const ClassInfo* k = ci.baseName.empty() ? nullptr : findClass(ci.baseName);
    int guard = 0;
    while (k && guard++ < 64 && missing.empty()) {
        for (const ClassMemberInfo& m : k->members) {
            if (!m.isAbstract) continue;
            if (!memberImplementedInClass(ci, m.name)) {
                missing = m.name;
                break;
            }
        }
        k = k->baseName.empty() ? nullptr : findClass(k->baseName);
    }
    if (!missing.empty()) {
        report(diagcode::kMissingInterfaceMember, c.span,
               "class '" + c.name + "' must implement abstract member '" +
                   missing + "'");
    }
}

void Analyzer::checkClassBodies(ClassDeclaration& c) {
    if (!findClass(c.name)) return;
    // Generic class bodies check once, with the parameters bound to TypeParam;
    // instantiation arguments are substituted at member reads.
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
        default:
            return;
    }
}

void Analyzer::analyzeMethodBody(MethodDeclaration& md) {
    const FuncSig& sig = sigs_[md.nodeId];
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
    if (md.body) visitStmt(*md.body);
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
}

// The accessor body (D21): `this` plus the setter's parameter are in scope.
// The getter's declared return type governs reads (collectClassMember).
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
    visitStmt(*ad.body);
    funcStack_.pop_back();
    popScope();
}

void Analyzer::analyzeCtorBody(ConstructorDeclaration& cd) {
    const FuncSig& sig = sigs_[cd.nodeId];
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
    funcStack_.push_back(ctx);
    if (cd.body) visitStmt(*cd.body);
    funcStack_.pop_back();
    popScope();
}

Type Analyzer::thisType() const {
    if (classStack_.empty()) return ambient();
    return Type::named(TypeKind::Class, classStack_.back());
}

// =============================================================================
// Member lookup + access
// =============================================================================
const ClassInfo* Analyzer::findClass(const std::string& name) const {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
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
    return Type::function(m.sig.params, m.sig.ret);
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

}  // namespace cp3
