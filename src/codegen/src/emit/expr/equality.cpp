// =============================================================================
// src/codegen/src/emit/equality.cpp - codegen `==`/`===` lowering (STYLE.md §8).
//
// The equality rules over nullish operands: `null == undefined` is true while
// `===` is false; an `std::optional` or the flavored `Maybe<T>` only equals a
// value it holds, and `===` additionally requires the empty slot's flavor to
// agree. A primitive union operand compares through cp3/core/union.hpp
// (`===` requires the held alternative to match, `==` coerces with ToNumber).
// Same-representation operands compare directly; differing primitive kinds are
// false either way (D15). `emitBinary` (emit/expr.cpp) dispatches here.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// `maybe <op> nullish` for a Shape::Maybe operand. CP3 (v0.2.2): the empty
// slot's flavor is part of the comparison -- `maybe == undefined` is true only
// when the slot holds undefined, `maybe == null` only when it holds null. JS
// coercion of the two flavors does not exist here.
std::string maybeNullishCompare(const std::string& mb, Shape nullish,
                                bool isEqual, bool isStrict) {
    (void)isStrict;
    const std::string cond = mb + ((nullish == Shape::Null) ? ".isNull()" : ".isUndefined()");
    return isEqual ? "(" + cond + ")" : "(! " + cond + ")";
}

// A native integer compares numerically with another native or a number.
bool nativeComparable(const CppInfo& l, const CppInfo& r) {
    return l.shape == Shape::Native &&
           (r.shape == Shape::Native || r.shape == Shape::Number);
}

// A number-backed enum member compared with a plain number compares its
// underlying value (JS parity: `Color.Red === 0` is true).
bool enumNumberCompare(const CppInfo& l, const CppInfo& r) {
    return (l.shape == Shape::Enum && r.cpp == "double") ||
           (r.shape == Shape::Enum && l.cpp == "double");
}

}  // namespace

// A primitive union (`std::variant`, Shape::Union): the union side is always
// passed first, so the runtime helper can `visit` it; two unions compare
// through the held alternatives (the helper visits both, so the two
// alternative sets may differ).
std::string Generator::unionEqualityText(const BinaryExpr& b, const CppInfo& L,
                                         const CppInfo& R, const std::string& lhs,
                                         const std::string& rhs) {
    const bool isEqual = (b.op == BinaryOp::Equal || b.op == BinaryOp::StrictEqual);
    const bool isStrict = (b.op == BinaryOp::StrictEqual || b.op == BinaryOp::StrictNotEqual);

    const bool lUnion = (L.shape == Shape::Union);
    const bool rUnion = (R.shape == Shape::Union);
    if (lUnion && rUnion) {
        // Two unions: the held alternatives decide the answer. `===` needs
        // the same alternative on both sides; `==` coerces them with ToNumber
        // (ECMAScript abstract equality, the same rule the one-sided helper
        // uses).
        const std::string call = "::cp3::runtime::unions::variantEq(" + lhs +
                                 ", " + rhs + ", " +
                                 (isStrict ? "true" : "false") + ")";
        return isEqual ? "(" + call + ")" : "(! " + call + ")";
    }
    const std::string uni = lUnion ? lhs : rhs;
    const std::string val = lUnion ? rhs : lhs;
    const CppInfo& other = lUnion ? R : L;
    // A union never holds a nullish alternative (`null`/`undefined` are not
    // primitive members), so a nullish other side never matches.
    if (other.shape == Shape::Null || other.shape == Shape::Undefined) {
        return isEqual ? "false" : "true";
    }
    if (isPrimitiveShape(other.shape)) {
        const std::string call = std::string("::cp3::runtime::unions::") +
                                 (isStrict ? "strictEq" : "looseEq") + "(" + uni +
                                 ", " + val + ")";
        return isEqual ? "(" + call + ")" : "(! " + call + ")";
    }
    // A union against a non-primitive value (an object, an array, a class
    // instance): cp3 has no ToPrimitive for its object models, and D15 keeps a
    // type mismatch false - the same answer the fixed-type path gives
    // (`1 == { a: 1 }` is false there too). The operands are still evaluated.
    return "((void)" + lhs + ", (void)" + rhs + ", " +
           (isEqual ? "false" : "true") + ")";
}

std::string Generator::emitEquality(const BinaryExpr& b, const CppInfo& L,
                                    const CppInfo& R, const std::string& lhs,
                                    const std::string& rhs) {
    const bool isEqual = (b.op == BinaryOp::Equal || b.op == BinaryOp::StrictEqual);
    const bool isStrict = (b.op == BinaryOp::StrictEqual || b.op == BinaryOp::StrictNotEqual);
    const char* op = isEqual ? "==" : "!=";

    const bool lNullish = (L.shape == Shape::Null || L.shape == Shape::Undefined);
    const bool rNullish = (R.shape == Shape::Null || R.shape == Shape::Undefined);

    // Two bare nullish literals (null/null, null/undefined, undefined/undefined):
    // the JS rule coerces null == undefined to true; cp3 matches only the
    // same flavor (spec 04 / v0.2.2).
    if (lNullish && rNullish) {
        const bool sameKind = (L.shape == R.shape);
        if (isEqual) return sameKind ? "true" : "false";
        return sameKind ? "false" : "true";
    }

    // optional <-> nullish: an optional matches a nullish target only when the
    // flavor stored in its empty slot agrees; otherwise it does not match the
    // nullish term at all.
    if (L.shape == Shape::Optional && rNullish) {
        const bool emptyOk = optionalEmptyMatches(L.emptyKind, R.shape);
        return (emptyOk == isEqual) ? "(! " + lhs + ".has_value())" : "(" + lhs + ".has_value())";
    }
    if (R.shape == Shape::Optional && lNullish) {
        const bool emptyOk = optionalEmptyMatches(R.emptyKind, L.shape);
        return (emptyOk == isEqual) ? "(! " + rhs + ".has_value())" : "(" + rhs + ".has_value())";
    }

    // Maybe (three-way nullable) <-> nullish: the flavor is stored, so strict
    // `===` can match exactly and loose `==` matches either flavor.
    if (L.shape == Shape::Maybe && rNullish) {
        return maybeNullishCompare(lhs, R.shape, isEqual, isStrict);
    }
    if (R.shape == Shape::Maybe && lNullish) {
        return maybeNullishCompare(rhs, L.shape, isEqual, isStrict);
    }

    // optional vs optional, or optional vs T: compare the unwrapped value.
    if (L.shape == Shape::Optional || R.shape == Shape::Optional) {
        const std::string opt = (L.shape == Shape::Optional) ? lhs : rhs;
        const std::string val = (L.shape == Shape::Optional) ? rhs : lhs;
        return "((" + opt + ".has_value()) && (*" + opt + " " + op + " " + val + "))";
    }

    // Maybe vs Maybe, or a Maybe vs T: compare the unwrapped value (guarded).
    if (L.shape == Shape::Maybe || R.shape == Shape::Maybe) {
        const std::string mb = (L.shape == Shape::Maybe) ? lhs : rhs;
        const std::string val = (L.shape == Shape::Maybe) ? rhs : lhs;
        return "((" + mb + ".hasValue()) && (" + mb + ".value() " + op + " " + val + "))";
    }

    // A primitive union on either side: see `unionEqualityText`.
    if (L.shape == Shape::Union || R.shape == Shape::Union) {
        return unionEqualityText(b, L, R, lhs, rhs);
    }

    if (enumNumberCompare(L, R)) {
        const std::string& member = (L.shape == Shape::Enum) ? lhs : rhs;
        const std::string& other = (L.shape == Shape::Enum) ? rhs : lhs;
        return "((static_cast<double>(" + member + ")) " + op + " " + other + ")";
    }
    if (nativeComparable(L, R) || nativeComparable(R, L)) {
        return "(" + lhs + " " + op + " " + rhs + ")";
    }

    // Same representation: compare directly. D15 keeps loose `==` same-type.
    if (L.representable && R.representable && L.cpp == R.cpp) {
        return "(" + lhs + " " + op + " " + rhs + ")";
    }

    // Differing primitive kinds: D15 says loose `==` is false; `===` is false
    // too. Preserve operand evaluation (as `(void)`, so a simple operand does
    // not trip -Wunused-value) with the comma operator.
    return "((void)" + lhs + ", (void)" + rhs + ", " +
           (isEqual ? "false" : "true") + ")";
}

}  // namespace cp3
