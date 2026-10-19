// =============================================================================
// src/std/crypto/native.hpp - `std/crypto` native backend row.
//
// OWNERSHIP: the std/crypto family (src/std/crypto/**). One row of the D68
// native-package table; the rest of the table never needs to change for this module.
//
// Backend: libsodium 1.0.22 (third_party/libsodium), pinned static build -
// DECISIONS D70 + third_party/MANIFEST.md. Authored (non-public-domain) AES-GCM
// paths are provided by the library itself; cp3c never bundles crypto code of
// its own outside this module's runtime TU.
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

// Verified against third_party/MANIFEST.md "Verified consumer flags":
//   compile: -I third_party/libsodium/include
//   link:    -L third_party/libsodium/lib -lsodium
// The row also adds the family ABI tree (src/std/crypto/include, D72) first,
// plus the std/buffer tree: the crypto surface types `randomBytes` as the
// nominal Buffer and accepts Buffers in sha256/sha512, so generated code can
// name cp3/std/buffer.hpp without the program importing std/buffer (the same
// pattern std/fs uses for its Buffer chunk).
inline const NativePackage kCryptoNativePackage{
    "std/crypto",
    {"src/std/crypto/include", "src/std/buffer/include",
     "third_party/libsodium/include"},
    {},
    {"src/std/crypto/runtime.cpp"},
    {"third_party/libsodium/lib"},
    {"-lsodium"},
};

}  // namespace cp3::cli::detail
