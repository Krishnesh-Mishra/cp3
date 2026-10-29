// =============================================================================
// cp3/std/crypto.hpp - `std/crypto` backing surface.
//
// OWNERSHIP: the std/crypto family (src/std/crypto/**). The header is the
// generated-code contract, so keep it additive like every cp3/std header, D14.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 section crypto; the pinned cases
// under tests/cases/stdlib/crypto_* pin the behavior)
// -----------------------------------------------------------------------------
//   crypto.randomUUID()            -> ::cp3::runtime::crypto::randomUuid()
//   crypto.randomBytes(n)          -> ::cp3::runtime::crypto::randomBytes(n)
//   crypto.sha256(s) / sha512(s)   -> ::cp3::runtime::crypto::sha256/sha512
//   crypto.AES256(k, d)            -> ::cp3::runtime::crypto::aes256Encrypt
//   crypto.AES256Decrypt(k, e)     -> ::cp3::runtime::crypto::aes256Decrypt;
//                                     throws a catchable value on tamper or
//                                     wrong key (docs 04 section crypto)
//
// -----------------------------------------------------------------------------
// SEMANTICS / LIMITATIONS
// -----------------------------------------------------------------------------
//   * `randomBytes` returns the nominal `Buffer` (docs 04). The runtime still
//     produces the raw bytes as a `std::string` and this C++ surface keeps
//     that spelling; codegen wraps the result with
//     `::cp3::runtime::buffer::from`.
//   * `randomUUID` is the RFC 4122 version 4 form: 36 lowercase chars.
//   * `sha256`/`sha512` take `string | Buffer` in the cp3 surface; a Buffer
//     argument lowers to its byte view (`::cp3::runtime::buffer::view`) and
//     this C++ surface takes a non-owning `std::string_view`. The result is
//     lowercase hex over the byte input.
//   * AES-256-GCM envelope: one base64 string of nonce (12 bytes) ||
//     ciphertext || tag (16 bytes), so the value is opaque to callers. The
//     `key` string is stretched to 32 bytes with BLAKE2b-256 before use (the
//     docs leave key derivation open; this is the decision recorded in the
//     wave report).
//   * `aes256Decrypt` throws a catchable cp3 value (`cp3::runtime::Thrown`)
//     on malformed input, wrong key or tag mismatch - never silently-wrong
//     plaintext (docs 04).
//
// Backend: libsodium (third_party/libsodium, D70). The implementation TU is
// src/std/crypto/runtime.cpp and is compiled + linked only when a program
// imports "std/crypto" (src/std/crypto/native.hpp, D68).
// =============================================================================
#pragma once

#include <string>
#include <string_view>

namespace cp3::runtime::crypto {

/// `crypto.randomUUID()`: a random RFC 4122 version 4 UUID in the canonical
/// lowercase 8-4-4-4-12 form (36 characters).
std::string randomUuid();

/// `crypto.randomBytes(length)`: `length` cryptographically random bytes as a
/// raw-byte string; codegen wraps the result into the nominal `Buffer`
/// (`::cp3::runtime::buffer::from`). Throws a catchable value when `length`
/// is negative, fractional or above 2^31 - 1.
std::string randomBytes(double length);

/// `crypto.sha256(data)`: the lowercase hex SHA-256 digest (64 characters).
std::string sha256(std::string_view data);

/// `crypto.sha512(data)`: the lowercase hex SHA-512 digest (128 characters).
std::string sha512(std::string_view data);

/// `crypto.AES256(key, data)`: AES-256-GCM encryption. Returns the base64
/// envelope (`nonce || ciphertext || tag`) that `aes256Decrypt` accepts.
std::string aes256Encrypt(const std::string& key, const std::string& data);

/// `crypto.AES256Decrypt(key, encrypted)`: the inverse of `aes256Encrypt`.
/// Throws a catchable value when the payload is malformed or the auth tag
/// does not verify (wrong key or tampered ciphertext).
std::string aes256Decrypt(const std::string& key, const std::string& encrypted);

}  // namespace cp3::runtime::crypto
