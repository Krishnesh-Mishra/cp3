// =============================================================================
// src/std/time/native.hpp - `std/time` native backend row (D72).
//
// OWNERSHIP: the std/time family (src/std/time/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the row
// supplies the generated-code include tree, selected when a program imports
// "std/time".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kTimeNativePackage{
    "std/time",
    {"src/std/time/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
