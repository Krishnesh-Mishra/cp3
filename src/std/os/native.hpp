// =============================================================================
// src/std/os/native.hpp - `std/os` native backend row (D72).
//
// OWNERSHIP: the std/os family (src/std/os/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the
// row supplies the generated-code include tree, selected when a program
// imports "std/os".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kOsNativePackage{
    "std/os",
    {"src/std/os/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
