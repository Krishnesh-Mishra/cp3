// =============================================================================
// src/codegen/src/emit/equality.cpp - codegen `==`/`===` lowering (STYLE.md §8).
//
// The equality rules over nullish operands: `null == undefined` is true while
// `===` is false; an `std::optional` or the flavored `Maybe<T>` only equals a
// value it holds, and `===` additionally requires the empty slot's flavor to
// agree. Same-representation operands compare directly; differing primitive
// kinds are false either way (D15). `emitBinary` (emit/expr.cpp) dispatches
// here.
// =============================================================================
#include "detail/generator.h"

#include <string>

#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// `maybe <op> nullish` for a Shape::Maybe operand: strict `===` requires the
// flavor to match, loose `==` treats both nullish flavors as equal (JS).
std::string maybeNullishCompare(const std::string& mb, Shape nullish,
                                bool isEqual, bool isStrict) {
    std::string cond;
    if (isStrict) {
        cond = mb + ((nullish == Shape::Null) ? ".isNull()" : ".isUndefined()");
    } else {
        cond = mb + ".isNullish()";
    }
    return isEqual ? "(" + cond + ")" : "(! " + cond + ")";
}

}  // namespace

std::string Generator::emitEquality(const BinaryExpr& b, const CppInfo& L,
                                    const CppInfo& R, const std::string& lhs,
                                    const std::string& rhs) {
    const bool isEqual = (b.op == BinaryOp::Equal || b.op == BinaryOp::StrictEqual);
    const bool isStrict = (b.op == BinaryOp::StrictEqual || b.op == BinaryOp::StrictNotEqual);
    const char* op = isEqual ? "==" : "!=";

    const bool lNullish = (L.shape == Shape::Null || L.shape == Shape::Undefined);
    const bool rNullish = (R.shape == Shape::Null || R.shape == Shape::Undefined);

    // null == undefined is true (loose); === is false. Both are nullish.
    if (lNullish && rNullish) {
        const bool sameKind = (L.shape == R.shape);
        if (isStrict) {
            return sameKind ? (isEqual ? "true" : "false") : (isEqual ? "false" : "true");
        }
        return isEqual ? "true" : "false";
    }

    // optional <-> nullish: an optional only ever holds a value or nothing.
    // Strict `===` only matches when the empty slot's flavor agrees.
    if (L.shape == Shape::Optional && rNullish) {
        if (isStrict && !optionalEmptyMatches(L.emptyKind, R.shape)) {
            return isEqual ? "false" : "true";
        }
        return isEqual ? "(! " + lhs + ".has_value())" : "(" + lhs + ".has_value())";
    }
    if (R.shape == Shape::Optional && lNullish) {
        if (isStrict && !optionalEmptyMatches(R.emptyKind, L.shape)) {
            return isEqual ? "false" : "true";
        }
        return isEqual ? "(! " + rhs + ".has_value())" : "(" + rhs + ".has_value())";
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

    // Maybe vs Maybe, or Maybe vs T: compare the unwrapped value (a nullish
    // side never equals a non-nullish value, so the guard is required).
    if (L.shape == Shape::Maybe || R.shape == Shape::Maybe) {
        const std::string mb = (L.shape == Shape::Maybe) ? lhs : rhs;
        const std::string val = (L.shape == Shape::Maybe) ? rhs : lhs;
        return "((" + mb + ".hasValue()) && (" + mb + ".value() " + op + " " + val + "))";
    }

    // A value union (`number | string` -> std::variant) against a value: `===`
    // only matches when the union currently holds the same alternative, while
    // loose `==` coerces through JS abstract equality (spec 05: `1 == "1"`
    // holds for a union-held value). Both-union comparisons fall through to the
    // direct compare below.
    if ((L.shape == Shape::Union) != (R.shape == Shape::Union)) {
        const std::string uni = (L.shape == Shape::Union) ? lhs : rhs;
        const std::string val = (L.shape == Shape::Union) ? rhs : lhs;
        const std::string helper = isStrict ? "::cp3::runtime::unionStrictEquals("
                                            : "::cp3::runtime::unionLooseEquals(";
        const std::string eq = helper + uni + ", " + val + ")";
        return isEqual ? "(" + eq + ")" : "(! " + eq + ")";
    }

    // Same representation: compare directly. D15 keeps loose `==` same-type.
    if (L.representable && R.representable && L.cpp == R.cpp) {
        return "(" + lhs + " " + op + " " + rhs + ")";
    }

    // Differing primitive kinds: D15 says loose `==` is false; `===` is false
    // too. Preserve operand evaluation with the comma operator.
    return "(" + lhs + ", " + rhs + ", " + (isEqual ? "false" : "true") + ")";
}

}  // namespace cp3
