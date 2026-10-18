// =============================================================================
// src/std/typed-arrays/native.hpp - `std/typed-arrays` native backend row (D72).
//
// OWNERSHIP: the std/typed-arrays family (src/std/typed-arrays/**). One row of the D68
// native-package table. Typed arrays need no import (D72 rule 1):
// src/std/typed-arrays/include is always on the compiler include path
// (compilerIncludeRoots), so this row is the uniform table entry only and
// is never selected by an import.
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kTypedArraysNativePackage{
    "std/typed-arrays",
    {"src/std/typed-arrays/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
