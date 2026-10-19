// =============================================================================
// src/codegen/src/detail/helper_blocks.h - PRIVATE generated-code helper
// blocks (the anonymous-namespace text a generated .cpp carries).
//
// The blocks whose families arrived with the operator wave: the primitive
// union formatter/truthiness pair (cp3/core/union.hpp) and the object value
// shape dispatchers (a generated struct's own `cp3ToText`/`cp3Truthy`).
// code_generator.cpp appends each block only when the unit's scanned text uses
// the family, so no generated unit pays for a family it does not name.
// =============================================================================
#pragma once

namespace cp3 {
namespace detail {

// The primitive-union helpers (`cp3_ts`/`cp3_truthy` over a `std::variant`).
const char* unionHelperBlock();

// The object value shape dispatchers (SFINAE over `cp3ToText`/`cp3Truthy`).
const char* objectHelperBlock();

}  // namespace detail
}  // namespace cp3
