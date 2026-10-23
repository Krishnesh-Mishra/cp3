// =============================================================================
// src/std/string/native.hpp - `std/string` native backend row (D72).
//
// OWNERSHIP: the std/string family (src/std/string/**). One row of the D68
// native-package table. String methods need no import (D72 rule 1):
// src/std/string/include is always on the compiler include path
// (compilerIncludeRoots), so this row is the uniform table entry only and
// is never selected by an import.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kStringNativePackage{
    "std/string",
    {"src/std/string/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
