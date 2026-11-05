// =============================================================================
// src/std/hash/native.hpp - `std/hash` native backend row (D72).
//
// OWNERSHIP: the std/hash family (src/std/hash/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the row
// supplies the generated-code include tree, selected when a program imports
// "std/hash". The buffer tree is added because the surface accepts
// `string | Buffer` (the same pattern std/crypto uses).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kHashNativePackage{
    "std/hash",
    {"src/std/hash/include", "src/std/buffer/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
