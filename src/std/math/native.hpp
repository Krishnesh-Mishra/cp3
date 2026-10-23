// =============================================================================
// src/std/math/native.hpp - `std/math` native backend row (D72).
//
// OWNERSHIP: the std/math family (src/std/math/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the
// row supplies the generated-code include tree, selected when a program
// imports "std/math".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kMathNativePackage{
    "std/math",
    {"src/std/math/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
