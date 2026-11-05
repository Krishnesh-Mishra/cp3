// =============================================================================
// src/std/log/native.hpp - `std/log` native backend row (D72).
//
// OWNERSHIP: the std/log family (src/std/log/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library (`fmt` is not bundled) and
// no runtime TU; the row supplies the generated-code include tree, selected
// when a program imports "std/log".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kLogNativePackage{
    "std/log",
    {"src/std/log/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
