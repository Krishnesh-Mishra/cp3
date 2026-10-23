// =============================================================================
// src/std/collections/native.hpp - `std/collections` native backend row (D72).
//
// OWNERSHIP: the std/collections family (src/std/collections/**). One row of
// the D68 native-package table; the rest of the table never needs to change
// for this module. Header-only family: no vendored library and no runtime TU;
// the row supplies the generated-code include tree, selected when a program
// imports "std/collections".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kCollectionsNativePackage{
    "std/collections",
    {"src/std/collections/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
