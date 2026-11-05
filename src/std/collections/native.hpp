// =============================================================================
// src/std/collections/native.hpp - `std/collections` native backend row (D72).
//
// OWNERSHIP: the std/collections family (src/std/collections/**). One row of
// the D68 native-package table; the rest of the table never needs to change
// for this module. Header-only family plus the seeded bucket-hash TU
// (src/runtime/src/core/bucket_hash.cpp, libsodium-seeded XXH3 -
// docs/design/hash-placement.md); the row supplies the generated-code
// include tree, selected when a program imports "std/collections".
//
// Link backends: libsodium (seed) AND XXHash (XXH3 mix). Both archives are
// pinned static builds: third_party/libsodium and third_party/xxhash (v0.8.3,
// BSD-2-Clause; third_party/MANIFEST.md Ledger F). The xxhash members are
// added to the windows-x64 platform row with the libsodium members - the only
// windows-x64 archive available is third_party/xxhash/lib/windows-x64/
// libxxhash.a. The linux-x64 / linux-arm64 rows stay libsodium-only until
// their xxhash archives are vendored; a linux Map/Set program will then fail
// the final link with an undefined reference to XXH3_64bits_withSeed (a
// visible error, never a silent fallback). Do not add linux platform rows
// pointing at the windows archive. The third_party/xxhash/include root is
// listed for completeness (the verified consumer flag) although
// bucket_hash.cpp currently declares the XXH3 symbol directly so the
// cp3_runtime unit build needs no new include root.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kCollectionsNativePackage{
    "std/collections",
    {"src/std/collections/include", "third_party/libsodium/include",
     "third_party/xxhash/include"},
    {},
    {"src/runtime/src/core/bucket_hash.cpp"},
    {},
    {"-lsodium", "-lxxhash"},
    {},
    // Linux audit: docs/design/linux-crosscompat.md — libsodium covers
    // linux-x64/linux-arm64; the xxhash component is windows-x64 only
    // (no linux libxxhash.a vendored yet), so the linux rows below stay
    // libsodium-only until that archive lands.
    {{"windows-x64",
      {"third_party/libsodium/lib/windows-x64",
       "third_party/xxhash/lib/windows-x64"},
      {"-lsodium", "-lxxhash"}},
     {"linux-x64", {"third_party/libsodium/lib/linux-x64"}, {"-lsodium"}},
     {"linux-arm64", {"third_party/libsodium/lib/linux-arm64"}, {"-lsodium"}}},
};

}  // namespace cp3::cli::detail
