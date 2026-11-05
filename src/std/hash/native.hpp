// =============================================================================
// src/std/hash/native.hpp - `std/hash` native backend row (D72).
//
// OWNERSHIP: the std/hash family (src/std/hash/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. The crc32/crc32c/fnv1a32/fnv1a64 surface stays header-only; the
// xxhash64 slice is backed by the vendored XXHash static archive
// (third_party/xxhash, v0.8.3, BSD-2-Clause; third_party/MANIFEST.md Ledger F)
// and implemented in src/std/hash/runtime.cpp. The row supplies the
// generated-code include tree, selected when a program imports "std/hash".
// The buffer tree is added because the surface accepts `string | Buffer`
// (the same pattern std/crypto uses).
//
// Verified consumer flags (third_party/MANIFEST.md, "Verified consumer
// flags"): compile -I third_party/xxhash/include; link
// -L third_party/xxhash/lib/<triple> -lxxhash. Only the windows-x64 archive
// is vendored so far (third_party/xxhash/lib/windows-x64/libxxhash.a); the
// row carries a single windows-x64 platform entry, so a linux-x64 or
// linux-arm64 target fails the build loudly via NativePackage::platforms
// ("has no vendor archives") instead of silently linking the wrong COFF
// objects. Do not add linux platform rows pointing at the windows archive.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kHashNativePackage{
    "std/hash",
    {"src/std/hash/include", "src/std/buffer/include",
     "third_party/xxhash/include"},
    {},
    {"src/std/hash/runtime.cpp"},
    {},
    {"-lxxhash"},
    {},
    // Linux audit: docs/design/linux-crosscompat.md — no linux-x64 /
    // linux-arm64 libxxhash.a vendored yet, so no linux platform row is
    // added here (a linux target fails loudly, never mislinks COFF).
    {{"windows-x64", {"third_party/xxhash/lib/windows-x64"}, {"-lxxhash"}}},
};

}  // namespace cp3::cli::detail
