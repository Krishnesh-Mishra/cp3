// =============================================================================
// src/std/path/native.hpp - `std/path` native backend row (D72).
//
// OWNERSHIP: the std/path family (src/std/path/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the
// row supplies the generated-code include tree, selected when a program
// imports "std/path".
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kPathNativePackage{
    "std/path",
    {"src/std/path/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
