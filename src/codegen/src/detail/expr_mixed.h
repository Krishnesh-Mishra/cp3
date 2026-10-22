// =============================================================================
// src/codegen/src/detail/expr_mixed.h - PRIVATE mixed-type expression
// lowerings (STYLE.md section 8).
//
// A logical operator or a ternary whose two sides have different C++ types
// must pick ONE type for the result. This helper owns that choice:
//
//   * a union over primitives -> the canonical `std::variant`
//     (cp3/core/union.hpp); each side is wrapped into it, so the expression
//     has the variant's type;
//   * a union with a nullish side -> the optional / `Maybe<T>` the nullish
//     union lowers to; the nullish side is the empty slot.
//
// A union that mixes a primitive with a non-primitive (an object, an array, a
// class instance) has no C++ spelling in the value model, so the helper
// returns "" and the caller keeps its marker.
// =============================================================================
#pragma once

#include <string>

#include "detail/type_map.h"

namespace cp3 {
namespace detail {

// The two sides of a mixed expression, with the text the emitter already
// produced for them and the result type the value model picked.
struct MixedBranches {
    CppInfo left;               // the true / left side
    CppInfo right;              // the false / right side
    CppInfo whole;              // the result type
    std::string leftText;       // the C++ text of the left side
    std::string rightText;      // the C++ text of the right side
    std::string conditionText;  // the C++ text of the condition
};

// The lowering of a ternary with non-uniform branch types; "" when the
// result type has no C++ spelling.
std::string mixedConditional(const MixedBranches& m);

// The lowering of a logical operator with mixed operand types; "" when the
// result type has no C++ spelling. `isAnd` selects `&&` over `||`.
std::string mixedLogical(const MixedBranches& m, bool isAnd);

}  // namespace detail
}  // namespace cp3
