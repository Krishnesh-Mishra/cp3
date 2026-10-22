// =============================================================================
// src/codegen/src/detail/spread_call.h - PRIVATE spread-call lowering
// (STYLE.md section 8).
//
// A call whose argument list contains a spread (`f(...xs)`) splices the
// elements of an array into the argument list. C++ has no way to expand a
// runtime-sized vector into a call, so the lowering picks the shape the
// callee allows:
//
//   * a callee with fixed parameters -> the call is a switch over the
//     spread's length: the elements fill the parameters the spread covers,
//     the parameters it does not reach take their default, and the elements
//     beyond the last parameter are dropped (JS semantics);
//   * a callee whose last parameter is a rest -> the elements join the
//     packed rest array.
//
// The caller checks the shapes and reports a type error when the spread has
// no C++ spelling; this unit only spells the call.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {
namespace detail {

// One parameter of a spread call: its C++ type and the text used when the
// spread does not supply a value for it ("" when the parameter is required,
// so a missing value becomes a runtime error).
struct SpreadParam {
    std::string type;
    std::string fallback;
    bool required = true;
};

// A call whose argument list contains a spread.
struct SpreadCall {
    std::string callee;       // the C++ text of the callee
    std::string arrayText;    // the C++ text of the spread source
    std::string elementType;  // the C++ type of the spread's elements
    std::vector<std::string> before;  // the fixed arguments before the spread
    std::vector<std::string> after;   // the fixed arguments after the spread
    std::vector<SpreadParam> params;  // the callee's fixed parameters
    bool hasRest = false;      // the callee ends in a rest parameter
    std::string restType;      // the C++ type of the rest element
};

// The call text of a spread into a callee with fixed parameters; "" when the
// shape has no C++ spelling.
std::string spreadCallText(const SpreadCall& call);

// The call text of a spread into a callee whose last parameter is a rest;
// "" when the shape has no C++ spelling.
std::string spreadRestCallText(const SpreadCall& call);

}  // namespace detail
}  // namespace cp3
