// =============================================================================
// src/std/text/native.hpp - `std/text` native backend row (D72).
//
// OWNERSHIP: the std/text family (src/std/text/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the row
// supplies the generated-code include trees, selected when a program imports
// "std/text".
//
// The header (cp3/std/text.hpp) spells the `Uint8Array` value type from
// cp3/typed_array.hpp, so the typed-arrays include tree rides along - the
// same bundling std/fs uses for its std/buffer `Buffer` chunk
// (src/std/fs/native.hpp).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kTextNativePackage{
    "std/text",
    {"src/std/text/include", "src/std/typed-arrays/include"},
    {},
    {},
    {},
    {},
};

}  // namespace cp3::cli::detail
