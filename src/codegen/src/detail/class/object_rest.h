// =============================================================================
// src/codegen/src/detail/object_rest.h - PRIVATE the object-rest lowering
// (STYLE.md section 8).
//
// `const { a, ...rest } = source` binds `rest` to a fresh object that holds
// the properties the pattern does not name. cp3's object model is static: an
// object is a generated struct with one member per property, so the rest
// object is a fresh value of the source's type whose named properties keep
// the type's default value (there is no absent property to spell).
// =============================================================================
#pragma once

#include <string>

#include "cp3/core/ast.hpp"
#include "cp3/core/types.hpp"

namespace cp3 {
namespace detail {

// The rest object of an object pattern: a fresh value of the source's type
// with the properties the pattern does not name copied over. "" when the
// source has no layout cp3 can copy (a class instance, an interface
// wrapper, a std/json record).
std::string objectRestText(const ObjectPattern& pattern, const Type& sourceType,
                            const std::string& sourceText);

}  // namespace detail
}  // namespace cp3
