// =============================================================================
// src/std/url/native.hpp - `std/url` native backend row (D72).
//
// OWNERSHIP: the std/url family (src/std/url/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the
// row supplies the generated-code include tree, selected when a program
// imports "std/url".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kUrlNativePackage{
    "std/url",
    {"src/std/url/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
