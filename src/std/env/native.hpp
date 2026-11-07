// =============================================================================
// src/std/env/native.hpp - `std/env` native backend row (D72).
//
// OWNERSHIP: the std/env family (src/std/env/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. Header-only family: no vendored library and no runtime TU; the row
// supplies the generated-code include tree, selected when a program imports
// "std/env". cp3/std/env.hpp composes the process family, and
// cp3/std/process.hpp includes the collections Map for `process.env`, so both
// include roots ride along with this row (the process row lists them too, for
// programs that import std/process alone).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kEnvNativePackage{
    "std/env",
    {"src/std/env/include", "src/std/process/include",
     "src/std/collections/include", "vendor/third_party/unordered_dense/include",
     "vendor/third_party/libsodium/include", "vendor/third_party/xxhash/include"},
    {},
    // cp3/std/env.hpp composes the process family, whose header includes the
    // collections Map; instantiating it references bucketHashBytes, so the
    // seeded bucket-hash TU and its sodium/xxhash link ride along.
    {"src/runtime/src/core/bucket_hash.cpp"},
    {},
    {"-lsodium", "-lxxhash"},
    {},
    {{"windows-x64",
      {"vendor/third_party/libsodium/lib/windows-x64",
       "vendor/third_party/xxhash/lib/windows-x64"},
      {"-lsodium", "-lxxhash"}},
     {"linux-x64",
      {"vendor/third_party/libsodium/lib/linux-x64",
       "vendor/third_party/xxhash/lib/linux-x64"},
      {"-lsodium", "-lxxhash"}},
     {"linux-arm64", {"vendor/third_party/libsodium/lib/linux-arm64"}, {"-lsodium"}}},
};

}  // namespace cp3::cli::detail
