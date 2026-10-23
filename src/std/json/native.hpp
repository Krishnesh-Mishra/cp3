// =============================================================================
// src/std/json/native.hpp - `std/json` native backend row (D72).
//
// OWNERSHIP: the std/json family (src/std/json/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the
// row supplies the generated-code include tree, selected when a program
// imports "std/json".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kJsonNativePackage{
    "std/json",
    {"src/std/json/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
