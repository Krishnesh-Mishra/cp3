// =============================================================================
// src/std/json/native.hpp - `std/json` native backend row (D72).
//
// OWNERSHIP: the std/json family (src/std/json/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no runtime TU and no vendored archive; the
// row supplies the generated-code include trees, selected when a program
// imports "std/json" - its own include tree plus the vendored fast_float
// headers, which cp3/std/json_parse.hpp needs for fast_float::from_chars
// (JSON number parsing; header-only, so no libDirs / linkFlags).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

// Verified consumer flags (header-only backend):
//   compile: -I src/std/json/include -I vendor/third_party/fast_float/include
inline const NativePackage kJsonNativePackage{
    "std/json",
    {"src/std/json/include", "vendor/third_party/fast_float/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
