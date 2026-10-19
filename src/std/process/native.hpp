// =============================================================================
// src/std/process/native.hpp - `std/process` native backend row (D72).
//
// OWNERSHIP: the std/process family (src/std/process/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. No vendored library, but exec/spawn do need real runtime TUs:
// child.cpp (the platform child primitives) and runtime.cpp (the argv store
// and the exec/spawn orchestration). The include tree also carries
// std/collections (D44): cp3/std/process.hpp includes cp3/std/collections.hpp
// for the `process.env` `Map<string, string>`.
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kProcessNativePackage{
    "std/process",
    {"src/std/process/include", "src/std/collections/include"},
    {},
    {"src/std/process/child.cpp", "src/std/process/runtime.cpp"},
    {},
    {},
};

}  // namespace cp3::cli::detail
