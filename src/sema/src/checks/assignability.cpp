// =============================================================================
// src/sema/src/checks/assignability.cpp - assignability + object-diff diagnostics.
//
// Owns `wouldAccept` (the structural assignability predicate, which layers the
// private shape model on top of the frozen `isAssignable`), the object-shape
// helper `objectAccepts`, the `checkAssign` entry point that reports
// kTypeMismatch / kUnknownType, and `emitObjectDiff` (kMissingProperty /
// kTypeMismatch / kExcessProperty).
// =============================================================================
#include "detail/analyzer.h"

#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "cp3/native_types.hpp"

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
    // Native numeric conversions under strictNumericCasts (spec 03 / D65):
    // only the exact same native type is accepted, and a literal still adopts
    // the declared type when it fits exactly (Rust literal inference). With
    // the flag off, the frozen isAssignable follows the C++ implicit rule.
    const bool nativeScalarTo = isNativeType(to) || to.kind == TypeKind::Number;
    const bool nativeScalarFrom =
        isNativeType(from) || from.kind == TypeKind::Number ||
        (from.isLiteral() && from.literalBase == TypeKind::Number);
    if (options_.strictNumericCasts && nativeScalarFrom && nativeScalarTo &&
        (isNativeType(from) || isNativeType(to))) {
        if (from.isLiteral() && from.literalBase == TypeKind::Number &&
            isNativeType(to)) {
            return nativeCanRepresentLiteral(to, from.numberValue);
        }
        if (isNativeType(from) && isNativeType(to)) {
            return from.name == to.name;
        }
        return false;
    }
    if (isAssignable(from, to)) return true;
    // User classes (spec 15): structural conformance to an
    // interface/object shape, literal-to-class, and derived-to-base.
    if (from.kind == TypeKind::Class && to.kind == TypeKind::Object) {
        if (const ClassInfo* ci = findClass(from.name)) {
            return classConformsToShape(*ci, to);
        }
    }
    if (from.kind == TypeKind::Object && to.kind == TypeKind::Class) {
        if (const ClassInfo* ci = findClass(to.name)) {
            return literalConformsToClass(from, *ci, freshLiteral);
        }
    }
    if (from.kind == TypeKind::Class && to.kind == TypeKind::Class &&
        !to.name.empty()) {
        if (const ClassInfo* ci = findClass(from.name)) {
            return classIsOrDerivesFrom(*ci, to.name);
        }
    }
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
    if (emitClassAssignDiff(from, to, span, freshLiteral)) return;
    if (isTupleType(to) && from.kind == TypeKind::Array) {
        report(diagcode::kTypeMismatch, span,
               "type mismatch: array literal does not match tuple type '" +
                   pretty(to) + "'");
        return;
    }
    // A failed conversion between native/number scalars is worth naming the
    // flag that refused it.
    if (options_.strictNumericCasts && (isNativeType(from) || isNativeType(to))) {
        const bool scalarFrom =
            isNativeType(from) || from.kind == TypeKind::Number ||
            (from.isLiteral() && from.literalBase == TypeKind::Number);
        const bool scalarTo = isNativeType(to) || to.kind == TypeKind::Number;
        if (scalarFrom && scalarTo) {
            report(diagcode::kTypeMismatch, span,
                   "strictNumericCasts refuses an implicit numeric conversion "
                   "from '" +
                       pretty(from) + "' to '" + pretty(to) +
                       "'; convert one side explicitly");
            return;
        }
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
