// =============================================================================
// src/sema/src/assignability.cpp — assignability + object-diff diagnostics.
//
// Owns `wouldAccept` (the structural assignability predicate, which layers the
// private shape model on top of the frozen `isAssignable`), the object-shape
// helper `objectAccepts`, the `checkAssign` entry point that reports
// kTypeMismatch / kUnknownType, and `emitObjectDiff` (kMissingProperty /
// kTypeMismatch / kExcessProperty).
// =============================================================================
#include "analyzer.h"

#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

bool Analyzer::wouldAccept(const Type& from, const Type& to, bool freshLiteral) {
    if (from.isError() || to.isError()) return true;
    // Ambient values (imports, builtins, `new`, out-of-scope constructs we
    // model permissively) and unresolved generic type parameters are not
    // concrete, so treating them like `unknown` keeps M1 honest without
    // emitting spurious cascades.
    if (isAmbientType(from) || from.kind == TypeKind::TypeParam) return true;
    if (from.isUnknown()) {
        return to.isUnknown() || isAmbientType(to) || to.isVoid() ||
               to.kind == TypeKind::TypeParam;
    }
    if (to.isUnknown() || isAmbientType(to) || to.isVoid() ||
        to.kind == TypeKind::TypeParam) {
        return true;
    }
    if (isAssignable(from, to)) return true;
    if (from.kind == TypeKind::Array && to.kind == TypeKind::Array) {
        return wouldAccept(elemOf(from), elemOf(to), false);
    }
    if (isTupleType(to) && from.kind == TypeKind::Array) {
        return false;  // needs the AST; handled by checkArrayLiteral
    }
    if (isTupleType(from) && to.kind == TypeKind::Array) {
        Shape* sh = shapeOf(from);
        if (sh && !sh->tupleElems.empty()) {
            return wouldAccept(Type::unionOf(sh->tupleElems), elemOf(to), false);
        }
        return false;
    }
    if (from.kind == TypeKind::Object && to.kind == TypeKind::Object) {
        return objectAccepts(from, to, freshLiteral);
    }
    if (to.isUnion() || to.isNullable()) {
        for (const Type& m : to.parts) {
            if (wouldAccept(from, m, freshLiteral)) return true;
        }
        return false;
    }
    if (from.isUnion()) {
        for (const Type& m : from.parts) {
            if (!wouldAccept(m, to, false)) return false;
        }
        return true;
    }
    if (from.isNullable()) {
        if (!hasNullish(to)) return false;
        for (const Type& m : from.parts) {
            if (!wouldAccept(m, to, false)) return false;
        }
        return true;
    }
    if (from.kind == TypeKind::Function && to.kind == TypeKind::Function) {
        return true;  // structural function compatibility is deferred
    }
    if (from.kind == TypeKind::Array && isTupleType(to)) return false;
    return false;
}

bool Analyzer::objectAccepts(const Type& from, const Type& to, bool fresh) {
    Shape* fromShape = shapeOf(from);
    Shape* toShape = shapeOf(to);
    if (!toShape) return true;
    if (toShape->open) return true;
    if (!fromShape) return false;
    if (fromShape->open) return true;
    for (const Field& tf : toShape->fields) {
        const Field* ff = findField(fromShape, tf.name);
        if (!ff) {
            if (tf.optional) continue;
            return false;
        }
        if (!wouldAccept(ff->type, tf.type, false)) return false;
    }
    if (fresh) {
        for (const Field& ff : fromShape->fields) {
            if (!findField(toShape, ff.name)) return false;
        }
    }
    for (const Field& tf : toShape->fields) {
        const Field* ff = findField(fromShape, tf.name);
        if (ff && !wouldAccept(ff->type, tf.type, false)) return false;
    }
    return true;
}

void Analyzer::checkAssign(const Type& from, const Type& to,
                           const SourceSpan& span, bool freshLiteral) {
    if (wouldAccept(from, to, freshLiteral)) return;
    if (from.isUnknown() && !to.isUnknown() && !isAmbientType(to) &&
        !to.isVoid()) {
        report(diagcode::kUnknownType, span,
               "value of type 'unknown' is not assignable to '" + pretty(to) +
                   "' without a type check");
        return;
    }
    if (to.kind == TypeKind::Object && !isAmbientType(to) &&
        from.kind == TypeKind::Object) {
        if (emitObjectDiff(from, to, span, freshLiteral)) return;
    }
    if (isTupleType(to) && from.kind == TypeKind::Array) {
        report(diagcode::kTypeMismatch, span,
               "type mismatch: array literal does not match tuple type '" +
                   pretty(to) + "'");
        return;
    }
    report(diagcode::kTypeMismatch, span,
           "type mismatch: cannot assign '" + pretty(from) + "' to '" +
               pretty(to) + "'");
}

bool Analyzer::emitObjectDiff(const Type& from, const Type& to,
                              const SourceSpan& span, bool fresh) {
    Shape* fromShape = shapeOf(from);
    Shape* toShape = shapeOf(to);
    if (!fromShape || !toShape) return false;
    for (const Field& tf : toShape->fields) {
        const Field* ff = findField(fromShape, tf.name);
        if (!ff) {
            if (tf.optional) continue;
            report(diagcode::kMissingProperty, span,
                   "missing required property '" + tf.name +
                       "' in object literal");
            return true;
        }
        if (!wouldAccept(ff->type, tf.type, false)) {
            report(diagcode::kTypeMismatch, span,
                   "type mismatch: property '" + tf.name + "' has type '" +
                       pretty(ff->type) + "' but '" + pretty(tf.type) +
                       "' is required");
            return true;
        }
    }
    if (fresh) {
        for (const Field& ff : fromShape->fields) {
            if (!findField(toShape, ff.name)) {
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
