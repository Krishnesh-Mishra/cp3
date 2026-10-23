// =============================================================================
// src/std/date/native.hpp - `std/date` native backend row (D72).
//
// OWNERSHIP: the std/date family (src/std/date/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the row
// supplies the generated-code include tree, selected when a program imports
// "std/date".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kDateNativePackage{
    "std/date",
    {"src/std/date/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
