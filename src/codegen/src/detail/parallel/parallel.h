// =============================================================================
// src/codegen/src/detail/parallel.h - the `parallel.run(...)` result marker.
//
// `parallel.run(f1, ..., fN)` yields an N-element array of the callables'
// common result type. cp3's array spelling (`number[]`) carries an element type
// but no extent, and sema types the ambient `parallel.run` call `unknown`, so
// codegen carries the element type itself: a `TypeKind::Array` whose `name` is
// `kParallelRunLabel`. `classify` must special-case the marker BEFORE its Array
// arm (classifyFamily would otherwise claim it as `Array<double>`), which keeps
// the marker non-representable: a binding of a `parallel.run` result lowers to
// `auto`, and index reads/writes go through the marker branches in
// src/language/typed-arrays/codegen_methods.cpp.
// =============================================================================
#pragma once

#include "cp3/core/types.hpp"

namespace cp3 {
namespace detail {

// The `Type::name` every parallel.run marker carries.
inline constexpr const char* kParallelRunLabel = "$parallelRun";

// The marker type for a `parallel.run(...)` result over `element`.
inline Type parallelRunType(const Type& element) {
    Type t(TypeKind::Array, kParallelRunLabel);
    t.parts.push_back(element);
    return t;
}

// True when `t` is a parallel.run result marker.
inline bool isParallelRunType(const Type& t) {
    return t.kind == TypeKind::Array && t.name == kParallelRunLabel;
}

// The element type of a parallel.run result marker (`unknown` when absent).
inline Type parallelRunElement(const Type& t) {
    if (!isParallelRunType(t) || t.parts.empty()) return Type::unknown();
    return t.parts.front();
}

}  // namespace detail
}  // namespace cp3
