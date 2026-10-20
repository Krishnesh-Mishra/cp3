// =============================================================================
// src/codegen/src/emit/closures.cpp - shared-cell queries (STYLE.md section 8).
//
// Spec section 10 "Closures": a binding shared between a frame and a nested
// closure is a shared cell exactly when it is also reassignable
// (cp3/core/value.hpp's codegen contract). The lexical walk that finds those
// bindings lives in support/captures.cpp (`analyzeCaptures`,
// detail/captures.h); this unit consumes its result.
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <unordered_map>

#include "detail/captures.h"

namespace cp3 {

using namespace detail;

// Is `name` a shared cell of the current frame? A cell is emitted as
// `Cell<T> name`, read as `name.get()` and written through `name.set(...)`, so
// the enclosing frame and every capturing closure share one binding.
bool Generator::isCell(const std::string& name) const {
    const auto it = captures_.frameCells.find(currentFrameId_);
    return it != captures_.frameCells.end() && it->second.count(name) != 0;
}

}  // namespace cp3
