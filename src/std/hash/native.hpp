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
// -L third_party/xxhash/lib/<triple> -lxxhash. The windows-x64 and linux-x64
// archives are vendored (third_party/xxhash/lib/{windows,linux}-x64/
// libxxhash.a); linux-arm64 has no archive yet, so only those two platform
// rows exist. A target without a row fails the build loudly via
// NativePackage::platforms ("has no vendor archives") instead of silently
// linking the wrong object format. Do not add platform rows pointing at
// another target's archive.
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
    // Linux audit: docs/design/linux-crosscompat.md — the linux-x64 archive is
    // vendored now; linux-arm64 has none yet, so that target still fails
    // loudly, never mislinks a foreign archive.
    {{"windows-x64", {"third_party/xxhash/lib/windows-x64"}, {"-lxxhash"}},
     {"linux-x64", {"third_party/xxhash/lib/linux-x64"}, {"-lxxhash"}}},
};

}  // namespace cp3::cli::detail
