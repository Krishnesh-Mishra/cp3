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

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// `new C(args)` (spec §15)
// =============================================================================
Type Analyzer::inferUserConstruct(Identifier& id, Binding& b, NewExpr& n) {
    const ClassInfo* ci = findClass(id.name);
    setType(id, b.type);
    std::vector<Type> argTypes;
    for (const ExprPtr& a : n.arguments) {
        if (!a) {
            argTypes.push_back(Type::undefined());
            continue;
        }
        if (a->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*a);
            if (sp.argument) inferExpr(*sp.argument);
            argTypes.push_back(Type::unknown());
            continue;
        }
        argTypes.push_back(inferExpr(*a));
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
    if (ci->hasCtor) {
        const FuncSig& sig = ci->ctorSig;
        std::size_t required = 0;
        for (bool opt : sig.optional) {
            if (!opt) ++required;
        }
        if (argTypes.size() < required ||
            (!sig.rest && argTypes.size() > sig.params.size())) {
            report(diagcode::kArityMismatch, n.span,
                   "constructor of class '" + id.name + "' expects " +
                       arityText(required, sig.params.size(), sig.rest) +
                       " argument(s)");
        } else {
            for (std::size_t i = 0; i < argTypes.size() &&
                                    i < sig.params.size(); ++i) {
                const SourceSpan& as =
                    n.arguments[i] ? n.arguments[i]->span : n.span;
                checkAssign(argTypes[i], instantiated(sig.params[i]), as, false);
            }
        }
    } else if (!argTypes.empty()) {
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
