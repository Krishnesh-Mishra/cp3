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

// A primitive union (`std::variant`, Shape::Union): the union side is always
// passed first, so the runtime helper can `visit` it; two unions of the same
// C++ spelling compare directly. Impossible pairs report through
// `notImplemented` and answer with a constant (D15).
std::string Generator::unionEqualityText(const BinaryExpr& b, const CppInfo& L,
                                         const CppInfo& R, const std::string& lhs,
                                         const std::string& rhs) {
    const bool isEqual = (b.op == BinaryOp::Equal || b.op == BinaryOp::StrictEqual);
    const bool isStrict = (b.op == BinaryOp::StrictEqual || b.op == BinaryOp::StrictNotEqual);
    const char* op = isEqual ? "==" : "!=";

    const bool lUnion = (L.shape == Shape::Union);
    const bool rUnion = (R.shape == Shape::Union);
    if (lUnion && rUnion) {
        if (L.cpp == R.cpp) {
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
        notImplemented(b, "== between different union types");
        return isEqual ? "false" : "true";
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
    notImplemented(b, "== involving a union and a non-primitive value");
    return isEqual ? "false" : "true";
}

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

    // A primitive union (`std::variant`, Shape::Union): see `unionEqualityText`.
    if (L.shape == Shape::Union || R.shape == Shape::Union) {
        return unionEqualityText(b, L, R, lhs, rhs);
    }

    // A number-backed enum member compared with a plain number compares its
    // underlying value (JS parity: `Color.Red === 0` is true).
    if ((L.shape == Shape::Enum && R.cpp == "double") ||
        (R.shape == Shape::Enum && L.cpp == "double")) {
        const std::string& member = (L.shape == Shape::Enum) ? lhs : rhs;
        const std::string& other = (L.shape == Shape::Enum) ? rhs : lhs;
        return "((static_cast<double>(" + member + ")) " + op + " " + other + ")";
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
