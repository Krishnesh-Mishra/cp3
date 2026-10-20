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

// The always-on per-TU block: value formatting, JS truthiness and the
// `finally` guard. Shared with the whole-program assembler (emit/program.cpp)
// so both paths splice identical bytes.
extern const char* const kHelperBlock;

// Per-family additions, emitted only when the family's flag is set.
extern const char* const kTypedArrayHelperBlock;
extern const char* const kBufferHelperBlock;
extern const char* const kEventEmitterHelperBlock;
// spec 15 handles: `Ref<T>` / optional truthiness (cp3/core/value.hpp).
extern const char* const kRefHelperBlock;

// The primitive-union helpers (`cp3_ts`/`cp3_truthy` over a `std::variant`).
const char* unionHelperBlock();

// The object value shape dispatchers (SFINAE over `cp3ToText`/`cp3Truthy`).
const char* objectHelperBlock();

}  // namespace detail
}  // namespace cp3
