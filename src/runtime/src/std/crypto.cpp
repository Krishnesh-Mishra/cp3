// =============================================================================
// src/runtime/src/std/crypto.cpp - `std/crypto` implementation TU.
//
// OWNERSHIP: the crypto wave. Selected (compiled + linked, with -lsodium) only
// when a program imports "std/crypto" - see src/cli/src/detail/native_std_crypto.hpp.
//
// Backend notes: libsodium 1.0.22 static (third_party/libsodium, D70). Do not
// gate AES-256-GCM on crypto_aead_aes256gcm_is_available(): in this pinned
// build it returns 0 while the round-trip works (known-answer covered by
// tests/cases/stdlib/crypto_*), so the wave must not consult it.
// =============================================================================
#include "cp3/std/crypto.hpp"

// STUB: the crypto wave lands the definitions here.
