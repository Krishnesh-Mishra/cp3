// =============================================================================
// src/std/collections/native.hpp - `std/collections` native backend row (D72).
//
// OWNERSHIP: the std/collections family (src/std/collections/**). One row of
// the D68 native-package table; the rest of the table never needs to change
// for this module. Header-only family plus the seeded bucket-hash TU
// (src/runtime/src/core/bucket_hash.cpp, libsodium-seeded wyhash -
// docs/design/hash-placement.md); the row supplies the generated-code
// include tree, selected when a program imports "std/collections".
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kCollectionsNativePackage{
    "std/collections",
    {"src/std/collections/include", "third_party/libsodium/include"},
    {},
    {"src/runtime/src/core/bucket_hash.cpp"},
    {},
    {"-lsodium"},
    {},
    {{"windows-x64", {"third_party/libsodium/lib/windows-x64"}, {"-lsodium"}},
     {"linux-x64", {"third_party/libsodium/lib/linux-x64"}, {"-lsodium"}},
     {"linux-arm64", {"third_party/libsodium/lib/linux-arm64"}, {"-lsodium"}}},
};

}  // namespace cp3::cli::detail
