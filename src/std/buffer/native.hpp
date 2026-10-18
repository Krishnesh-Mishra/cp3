// =============================================================================
// src/std/buffer/native.hpp - `std/buffer` native backend row (D72).
//
// OWNERSHIP: the std/buffer family (src/std/buffer/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the row
// supplies the generated-code include tree (cp3/std/buffer.hpp), selected when
// a program imports "std/buffer".
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kBufferNativePackage{
    "std/buffer",
    {"src/std/buffer/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
