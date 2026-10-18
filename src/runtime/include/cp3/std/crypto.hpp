// =============================================================================
// cp3/std/crypto.hpp - `std/crypto` backing surface.
//
// OWNERSHIP: runtime module (the crypto wave fills this file; the header is the
// generated-code contract, so keep it additive like every cp3/std header, D14).
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 §crypto - the wave pins the exact
// signatures while landing them; codegen lowers `crypto.*` onto this namespace)
// -----------------------------------------------------------------------------
//   crypto.randomUUID()            -> ::cp3::runtime::crypto::randomUuid()
//   crypto.randomBytes(n)          -> ::cp3::runtime::crypto::randomBytes(n)
//   crypto.sha256(s) / sha512(s)   -> hex digest strings
//   crypto AES-256 round-trips     -> base64 "nonce.tag.ciphertext" envelope,
//                                     decrypt THROWS on tamper/wrong key
//
// Backend: libsodium (third_party/libsodium, D70). The implementation TU is
// src/runtime/src/std/crypto.cpp and is compiled + linked only when a program
// imports "std/crypto" (src/cli/src/detail/native_std_crypto.hpp, D68).
// =============================================================================
#pragma once

namespace cp3::runtime::crypto {

// STUB: the crypto wave lands the surface here.

}  // namespace cp3::runtime::crypto
