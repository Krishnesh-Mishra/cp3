// =============================================================================
// src/std/fs/native.hpp - `std/fs` native backend row (D68).
//
// OWNERSHIP: the std/fs family (src/std/fs/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module.
//
// Backend: the C++ standard library only (`std::filesystem` + `std::fstream`,
// see src/std/fs/runtime.cpp). No vendored library, no extra compile flags, no
// extra link flags: the row supplies the generated-code include tree and the
// one runtime TU, selected when a program imports "std/fs".
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kFsNativePackage{
    "std/fs",
    {"src/std/fs/include"},
    {},
    {"src/std/fs/runtime.cpp"},
    {},
    {},
};

}  // namespace cp3::cli::detail
