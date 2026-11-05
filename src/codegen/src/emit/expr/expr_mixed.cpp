// =============================================================================
// src/codegen/src/emit/expr_mixed.cpp - the mixed-type webserverion lowerings
// (STYLE.md section 8).
//
// The webserverion sides of the value model: a logical operator or a ternary
// whose two sides have different C++ types. The choice of the result type and
// the wrapping of each side live in detail/expr_mixed.h's contract; this unit
// spells them out.
//
//   * a primitive union (`0 || "x"`, `c ? 1 : "x"`) lowers to the canonical
//     `std::variant` (cp3/core/union.hpp): each side is wrapped into it, so
//     the webserverion has the variant's type and the value model keeps the
//     alternative it holds;
//   * a nullish side (`x || null`, `c ? 1 : null`) lowers to the optional /
//     `Maybe<T>` the nullish union lowers to: the value side is wrapped, the
//     nullish side is the empty slot (the flavor of the nullish value is
//     kept, so `null` and `undefined` stay distinct).
//
// A union that mixes a primitive with a non-primitive has no C++ spelling in
// the value model; the helper returns "" and the caller keeps its marker.
// =============================================================================
#include "detail/expr/expr_mixed.h"

#include <string>

#include "detail/generator/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// A nullish value (`null` / `undefined`), the side that folds into the empty
// slot of an optional.
bool isNullishShape(Shape s) {
    return s == Shape::Null || s == Shape::Undefined;
}

// A value the optional / `Maybe` can hold directly: not an optional itself,
// not a union, not a value the model cannot spell.
bool isPlainValue(const CppInfo& c) {
    if (!c.representable) return false;
    if (c.shape == Shape::Optional || c.shape == Shape::Maybe) return false;
    if (c.shape == Shape::Union || c.shape == Shape::Unsupported) return false;
    return true;
}

// The empty slot of a nullish union: `std::nullopt` for the one-flavor
// optional, the flavored `Maybe` constructor for the three-way one.
std::string emptySlotText(const CppInfo& whole, Shape nullish) {
    if (whole.shape == Shape::Optional) return "std::nullopt";
    if (nullish == Shape::Null) return whole.cpp + "(nullptr)";
    return whole.cpp + "(cp3::runtime::undefined)";
}

// A value inside a nullish union: the optional / `Maybe` wrapper around the
// value's own type.
std::string wrappedValueText(const CppInfo& whole, const CppInfo& value,
                              const std::string& text) {
    if (whole.shape == Shape::Optional) {
        return "std::optional<" + value.cpp + ">(" + text + ")";
    }
    return "cp3::runtime::Maybe<" + value.cpp + ">(" + text + ")";
}

// The two sides of a nullish union: which side holds the value and which one
// is the nullish empty slot. False when the two sides are not one value and
// one nullish value.
bool nullishPair(const MixedBranches& m, const CppInfo*& value,
                 const CppInfo*& nullish, bool& leftIsNullish) {
    const bool leftNullish = isNullishShape(m.left.shape);
    const bool rightNullish = isNullishShape(m.right.shape);
    if (leftNullish == rightNullish) return false;
    leftIsNullish = leftNullish;
    value = leftNullish ? &m.right : &m.left;
    nullish = leftNullish ? &m.left : &m.right;
    return isPlainValue(*value);
}

}  // namespace

// A ternary with non-uniform branch types.
std::string mixedConditional(const MixedBranches& m) {
    // A primitive union (`c ? 1 : "x"`): both sides are wrapped into the
    // canonical variant, so the ternary has one type.
    if (m.whole.representable && m.whole.shape == Shape::Union &&
        isPrimitiveShape(m.left.shape) && isPrimitiveShape(m.right.shape)) {
        const std::string cast = "static_cast<" + m.whole.cpp + ">";
        return "(" + m.conditionText + " ? " + cast + "(" + m.leftText + ") : " +
               cast + "(" + m.rightText + "))";
    }
    // A nullish side (`c ? 1 : null`): the result is the optional the union of
    // the two branch types lowers to; the nullish branch is the empty slot.
    if (m.whole.representable &&
        (m.whole.shape == Shape::Optional || m.whole.shape == Shape::Maybe)) {
        const CppInfo* value = nullptr;
        const CppInfo* nullish = nullptr;
        bool leftIsNullish = false;
        if (nullishPair(m, value, nullish, leftIsNullish)) {
            const std::string held = wrappedValueText(
                m.whole, *value, leftIsNullish ? m.rightText : m.leftText);
            const std::string empty = emptySlotText(m.whole, nullish->shape);
            if (leftIsNullish) {
                return "(" + m.conditionText + " ? " + empty + " : " + held + ")";
            }
            return "(" + m.conditionText + " ? " + held + " : " + empty + ")";
        }
    }
    return std::string();
}

// A logical operator with mixed operand types.
std::string mixedLogical(const MixedBranches& m, bool isAnd) {
    // A primitive union (`0 || "x"`): each side is wrapped into the canonical
    // variant so the lambda has a single return type.
    if (m.whole.representable && m.whole.shape == Shape::Union &&
        isPrimitiveShape(m.left.shape) && isPrimitiveShape(m.right.shape)) {
        const std::string cast = "static_cast<" + m.whole.cpp + ">";
        if (isAnd) {
            return "([&]{ auto cp3_l = (" + cast + "(" + m.leftText +
                   ")); return cp3_truthy(cp3_l) ? (" + cast + "(" + m.rightText +
                   ")) : cp3_l; }())";
        }
        return "([&]{ auto cp3_l = (" + cast + "(" + m.leftText +
               ")); return cp3_truthy(cp3_l) ? cp3_l : (" + cast + "(" +
               m.rightText + ")); }())";
    }
    // A nullish side (`x || null`): the result is the optional the union of
    // the two operand types lowers to; the nullish side is the empty slot.
    if (m.whole.representable &&
        (m.whole.shape == Shape::Optional || m.whole.shape == Shape::Maybe)) {
        const CppInfo* value = nullptr;
        const CppInfo* nullish = nullptr;
        bool leftIsNullish = false;
        if (nullishPair(m, value, nullish, leftIsNullish)) {
            const std::string held = wrappedValueText(
                m.whole, *value, leftIsNullish ? m.rightText : m.leftText);
            const std::string empty = emptySlotText(m.whole, nullish->shape);
            // `x || null` keeps x when x is truthy; `x && null` keeps the
            // nullish side when x is truthy. A nullish left side is always
            // falsy, so the same two forms cover it.
            const bool keepValueWhenTruthy = (isAnd == leftIsNullish);
            const std::string whenTrue = keepValueWhenTruthy ? held : empty;
            const std::string whenFalse = keepValueWhenTruthy ? empty : held;
            return "([&]{ auto cp3_l = (" + m.leftText + "); return cp3_truthy(cp3_l) ? " +
                   whenTrue + " : " + whenFalse + "; }())";
        }
    }
    return std::string();
}

}  // namespace detail
}  // namespace cp3
