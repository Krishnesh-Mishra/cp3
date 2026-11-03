// =============================================================================
// src/sema/src/checks/class_conform_check.cpp - class conformance + `new`.
//
// The structural half of the spec §15 object model:
// docs/language-spec/15-classes.md. A class instance satisfies an
// interface/object shape structurally (`classConformsToShape`, with the strict
// method-signature compare `functionTypesEquivalent`), a bare object literal
// satisfies a class only when every field is public and the fresh-literal
// excess-property rule holds (`literalConformsToClass`), and `inferUserConstruct`
// checks `new C(args)` against the constructor signature (CP3413 for abstract
// classes). The diff reporters raise CP3701 / CP3401 / CP3702 / CP3409 / CP3410.
// The class model itself lives in class_check.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Abstract conformance
// =============================================================================

// Is the member `name` implemented by the class (or one of its bases)?
// A concrete class must implement every abstract member of its bases.
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

// =============================================================================
// `new C(args)` (spec §15)
// =============================================================================
// The argument types of a `new` call. A spread argument stands for an unknown
// number of arguments, so its type is unknown and its arity is the codegen's
// business (the spread's elements are checked against the constructor's
// parameters there).
void Analyzer::inferNewArgs(const ExprList& args, std::vector<Type>& types,
                            std::vector<bool>& spread) {
    for (const ExprPtr& a : args) {
        if (!a) {
            types.push_back(Type::undefined());
            spread.push_back(false);
            continue;
        }
        if (a->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*a);
            if (sp.argument) inferExpr(*sp.argument);
            types.push_back(Type::unknown());
            spread.push_back(true);
            continue;
        }
        types.push_back(inferExpr(*a));
        spread.push_back(false);
    }
}

// The callable Error constructors are the same checked signatures as `new`:
// `Error(message?, options?)`, `AggregateError(errors, message?, options?)`.
Type Analyzer::inferErrorCall(CallExpr& call, Identifier& id, Binding& b,
                              const ClassInfo& ci) {
    setType(id, b.type);
    std::vector<Type> argTypes;
    std::vector<bool> spreadArg;
    inferNewArgs(call.arguments, argTypes, spreadArg);
    bool hasSpread = false;
    for (bool isSpread : spreadArg) {
        if (isSpread) hasSpread = true;
    }
    if (!ci.hasCtor) return withType(call, ambient());
    const FuncSig& sig = ci.ctorSig;
    std::size_t required = 0;
    for (bool optional : sig.optional) {
        if (!optional) ++required;
    }
    const bool arityOk =
        hasSpread ||
        (argTypes.size() >= required && argTypes.size() <= sig.params.size());
    if (!arityOk) {
        report(diagcode::kArityMismatch, call.span,
               "constructor of class '" + id.name + "' expects " +
                   arityText(required, sig.params.size(), sig.rest) +
                   " argument(s)");
    } else {
        for (std::size_t i = 0; i < argTypes.size() && i < sig.params.size();
             ++i) {
            if (i < spreadArg.size() && spreadArg[i]) continue;
            const SourceSpan& as =
                i < call.arguments.size() && call.arguments[i]
                    ? call.arguments[i]->span
                    : call.span;
            checkAssign(argTypes[i], sig.params[i], as, false);
        }
    }
    Type result = Type::named(TypeKind::Class, ci.name);
    return withType(call, result);
}

// `super(...)` in a derived constructor. TypeScript/JS route the call to the
// implicit constructor of the direct base, which in turn forwards to the
// nearest base that declares one; the arity and argument types therefore come
// from that constructor. A `super()` call in a method, a base class, or a
// non-constructor function is rejected.
Type Analyzer::inferSuperCall(CallExpr& call, Identifier& id) {
    setType(id, Type::voidType());
    std::vector<Type> argTypes;
    std::vector<bool> spreadArg;
    inferNewArgs(call.arguments, argTypes, spreadArg);
    bool hasSpread = false;
    for (bool isSpread : spreadArg) {
        if (isSpread) hasSpread = true;
    }
    if (funcStack_.empty() || !funcStack_.back().isCtor) {
        report(diagcode::kNotCallable, call.span,
               "super(...) is only valid inside a class constructor");
        return withType(call, Type::error());
    }
    if (classStack_.empty()) {
        report(diagcode::kNotCallable, call.span,
               "super(...) is only valid inside a derived class constructor");
        return withType(call, Type::error());
    }
    const ClassInfo* derived = findClass(classStack_.back());
    if (derived == nullptr || derived->baseName.empty()) {
        report(diagcode::kNotCallable, call.span,
               "super(...) is only valid inside a derived class constructor; "
               "class '" +
                   classStack_.back() + "' has no base class");
        return withType(call, Type::error());
    }
    FuncCtx& frame = funcStack_.back();
    if (frame.sawSuper) {
        report(diagcode::kNotCallable, call.span,
               "super(...) may only be called once in a derived constructor");
        return withType(call, Type::error());
    }
    frame.sawSuper = true;
    const ClassInfo* base = findClass(derived->baseName);
    if (base == nullptr) {
        // `extends` resolution already reported an unknown base; keep the
        // call permissive so the body does not cascade.
        return withType(call, Type::voidType());
    }
    const ClassInfo* owner = constructorOwner(*base);
    if (owner == nullptr) {
        // The base chain has only implicit default constructors.
        if (!argTypes.empty() && !hasSpread) {
            report(diagcode::kArityMismatch, call.span,
                   "super(...) in the constructor of class '" + derived->name +
                       "' expects no arguments: base class '" + base->name +
                       "' has no constructor taking arguments");
        }
        return withType(call, Type::voidType());
    }
    const FuncSig& sig = owner->ctorSig;
    std::size_t required = 0;
    for (bool opt : sig.optional) {
        if (!opt) ++required;
    }
    const bool arityOk =
        hasSpread || (argTypes.size() >= required &&
                      (sig.rest || argTypes.size() <= sig.params.size()));
    if (!arityOk) {
        report(diagcode::kArityMismatch, call.span,
               "super(...) in the constructor of class '" + derived->name +
                   "' expects " +
                   arityText(required, sig.params.size(), sig.rest) +
                   " argument(s), from constructor of class '" + owner->name +
                   "'");
        return withType(call, Type::voidType());
    }
    for (std::size_t i = 0; i < argTypes.size() && i < sig.params.size(); ++i) {
        if (spreadArg[i]) continue;  // the spread's type is unknown
        const SourceSpan& as =
            i < call.arguments.size() && call.arguments[i]
                ? call.arguments[i]->span
                : call.span;
        checkAssign(argTypes[i], sig.params[i], as, false);
    }
    return withType(call, Type::voidType());
}

Type Analyzer::inferUserConstruct(Identifier& id, Binding& b, NewExpr& n) {
    const ClassInfo* ci = findClass(id.name);
    setType(id, b.type);
    std::vector<Type> argTypes;
    std::vector<bool> spreadArg;
    inferNewArgs(n.arguments, argTypes, spreadArg);
    bool hasSpread = false;
    for (bool isSpread : spreadArg) {
        if (isSpread) hasSpread = true;
    }
    if (!ci) return withType(n, ambient());
    // `new Box<number>()`: resolve the instantiation arguments once.
    std::vector<Type> instArgs;
    for (const TypePtr& ta : n.typeArgs) {
        if (ta) instArgs.push_back(resolveType(*ta));
    }
    auto instantiated = [&](const Type& t) {
        if (ci->typeParamNames.empty() || instArgs.empty()) return t;
        return substituteClassTypeParams(t, *ci, instArgs);
    };
    if (ci->isAbstract) {
        report(diagcode::kCannotInstantiateAbstract, n.span,
               "cannot instantiate abstract class '" + id.name + "'");
        return withType(n, Type::named(TypeKind::Class, id.name));
    }
    // D25: a generic class needs explicit type arguments at `new`
    // (`new Box<number>()`); the count must match the declaration's.
    if (!ci->typeParamNames.empty() &&
        instArgs.size() != ci->typeParamNames.size()) {
        report(diagcode::kInvalidTypeArgument, n.span,
               "generic class '" + id.name + "' expects " +
                   std::to_string(ci->typeParamNames.size()) +
                   " type argument(s), got " + std::to_string(instArgs.size()));
    }
    if (const ClassInfo* ctorOwner = constructorOwner(*ci)) {
        const FuncSig& sig = ctorOwner->ctorSig;
        std::size_t required = 0;
        for (bool opt : sig.optional) {
            if (!opt) ++required;
        }
        const bool arityOk =
            hasSpread || (argTypes.size() >= required &&
                          (sig.rest || argTypes.size() <= sig.params.size()));
        if (!arityOk) {
            report(diagcode::kArityMismatch, n.span,
                   "constructor of class '" + id.name + "' expects " +
                       arityText(required, sig.params.size(), sig.rest) +
                       " argument(s)");
        } else {
            for (std::size_t i = 0; i < argTypes.size() &&
                                    i < sig.params.size(); ++i) {
                if (spreadArg[i]) continue;  // the spread's type is unknown
                const SourceSpan& as =
                    n.arguments[i] ? n.arguments[i]->span : n.span;
                checkAssign(argTypes[i], instantiated(sig.params[i]), as, false);
            }
        }
    } else if (!argTypes.empty() && !hasSpread) {
        report(diagcode::kArityMismatch, n.span,
               "class '" + id.name + "' has no constructor taking arguments");
    }
    Type inst = Type::named(TypeKind::Class, id.name);
    inst.parts = std::move(instArgs);
    return withType(n, inst);
}

// =============================================================================
// Structural conformance (spec §15: structural, not nominal)
// =============================================================================
// Is the member `name` implemented in `ci` (or in one of its bases)? The
// abstract-conformance check (class_check.cpp) asks this per abstract member.
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

bool Analyzer::functionTypesEquivalent(const Type& a, const Type& b) {
    if (a.kind != TypeKind::Function || b.kind != TypeKind::Function) {
        return false;
    }
    if (a.parts.size() != b.parts.size()) return false;
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        if (!wouldAccept(a.parts[i], b.parts[i], false)) return false;
        if (!wouldAccept(b.parts[i], a.parts[i], false)) return false;
    }
    return true;
}

bool Analyzer::classConformsToShape(const ClassInfo& ci, const Type& to) {
    Shape* toShape = shapeOf(to);
    if (!toShape) return true;
    if (toShape->open) return true;
    if (toShape->tuple) return false;
    for (const Field& tf : toShape->fields) {
        const ClassMemberInfo* m = findClassMemberChain(ci, tf.name, nullptr);
        if (!m) {
            if (tf.optional) continue;
            return false;
        }
        Type mt = classMemberType(*m);
        if (mt.kind == TypeKind::Function &&
            tf.type.kind == TypeKind::Function) {
            if (!functionTypesEquivalent(mt, tf.type)) return false;
            continue;
        }
        if (!wouldAccept(mt, tf.type, false)) return false;
    }
    return true;
}

bool Analyzer::literalConformsToClass(const Type& from, const ClassInfo& ci,
                                      bool fresh) {
    Shape* fs = shapeOf(from);
    // spec §15: a class with any private/protected field cannot be satisfied
    // by a bare object literal at all.
    for (const ClassMemberInfo& m : ci.members) {
        if (m.isStatic) continue;
        if (m.isPrivate || m.isProtected) return false;
    }
    for (const ClassMemberInfo& m : ci.members) {
        if (m.isStatic || m.isMethod) continue;
        const Field* ff = fs ? findField(fs, m.name) : nullptr;
        if (!ff) {
            if (m.hasInitializer || m.optional) continue;
            return false;
        }
        if (!wouldAccept(ff->type, m.type, false)) return false;
    }
    if (fresh && fs) {
        for (const Field& ff : fs->fields) {
            if (!findClassMemberChain(ci, ff.name, nullptr)) return false;
        }
    }
    return true;
}

// =============================================================================
// Diff diagnostics (what `checkAssign` reports when conformance fails)
// =============================================================================
bool Analyzer::emitClassAssignDiff(const Type& from, const Type& to,
                                   const SourceSpan& span, bool fresh) {
    if (from.kind == TypeKind::Class && to.kind == TypeKind::Object) {
        const ClassInfo* ci = findClass(from.name);
        if (ci) return emitClassToShapeDiff(*ci, to, span);
    }
    if (to.kind == TypeKind::Class && from.kind == TypeKind::Object) {
        const ClassInfo* ci = findClass(to.name);
        if (ci) return emitLiteralToClassDiff(from, *ci, span, fresh);
    }
    return false;
}

bool Analyzer::emitClassToShapeDiff(const ClassInfo& ci, const Type& to,
                                    const SourceSpan& span) {
    Shape* toShape = shapeOf(to);
    if (!toShape) return false;
    for (const Field& tf : toShape->fields) {
        const ClassMemberInfo* m = findClassMemberChain(ci, tf.name, nullptr);
        if (!m) {
            if (tf.optional) continue;
            report(diagcode::kMissingInterfaceMember, span,
                   "missing required member '" + tf.name + "' in class '" +
                       ci.name + "'");
            return true;
        }
        Type mt = classMemberType(*m);
        bool ok = mt.kind == TypeKind::Function &&
                          tf.type.kind == TypeKind::Function
                      ? functionTypesEquivalent(mt, tf.type)
                      : wouldAccept(mt, tf.type, false);
        if (!ok) {
            report(diagcode::kTypeMismatch, span,
                   "type mismatch: member '" + tf.name + "' of class '" +
                       ci.name + "' has type '" + pretty(mt) + "' but '" +
                       pretty(tf.type) + "' is required");
            return true;
        }
    }
    return false;
}

bool Analyzer::emitLiteralToClassDiff(const Type& from, const ClassInfo& ci,
                                      const SourceSpan& span, bool fresh) {
    Shape* fs = shapeOf(from);
    for (const ClassMemberInfo& m : ci.members) {
        if (m.isStatic) continue;
        if (m.isPrivate || m.isProtected) {
            report(diagcode::kPrivateFieldInLiteral, span,
                   "object literal cannot satisfy class '" + ci.name +
                       "': field '" + m.name + "' is " +
                       (m.isPrivate ? "private" : "protected"));
            return true;
        }
    }
    for (const ClassMemberInfo& m : ci.members) {
        if (m.isStatic || m.isMethod) continue;
        const Field* ff = fs ? findField(fs, m.name) : nullptr;
        if (!ff) {
            if (m.hasInitializer || m.optional) continue;
            report(diagcode::kMissingProperty, span,
                   "missing required property '" + m.name +
                       "' in object literal");
            return true;
        }
        if (!wouldAccept(ff->type, m.type, false)) {
            report(diagcode::kTypeMismatch, span,
                   "type mismatch: property '" + m.name + "' has type '" +
                       pretty(ff->type) + "' but '" + pretty(m.type) +
                       "' is required");
            return true;
        }
    }
    if (fresh && fs) {
        for (const Field& ff : fs->fields) {
            if (!findClassMemberChain(ci, ff.name, nullptr)) {
                report(diagcode::kExcessProperty, span,
                       "excess property '" + ff.name +
                           "' is not part of the target type");
                return true;
            }
        }
    }
    return false;
}

}  // namespace cp3
