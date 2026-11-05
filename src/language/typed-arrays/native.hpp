// =============================================================================
// src/language/typed-arrays/native.hpp - `language/typed-arrays` native backend row (D72).
//
// OWNERSHIP: the language/typed-arrays family (src/language/typed-arrays/**). One row of the D68
// native-package table. Typed arrays need no import (D72 rule 1):
// src/language/typed-arrays/include is always on the compiler include path
// (compilerIncludeRoots), so this row is the uniform table entry only and
// is never selected by an import.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kTypedArraysNativePackage{
    "language/typed-arrays",
    {"src/language/typed-arrays/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
