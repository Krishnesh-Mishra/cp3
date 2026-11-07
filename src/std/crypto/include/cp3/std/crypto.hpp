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
// Backend: libsodium (vendor/third_party/libsodium, D70). The implementation TU is
// src/std/crypto/runtime.cpp and is compiled + linked only when a program
// imports "std/crypto" (src/std/crypto/native.hpp, D68).
// =============================================================================
#pragma once

#include <cstdint>
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
///
/// The `double` form is the TypeScript project's entry point: codegen passes
/// the `number` argument through unchanged so the fractional/negative
/// validation below still fires. The exact-width form takes the std length
/// width (`usize`/`size_t`, canonical `u64`); a Rust/C++ project's argument
/// is already that width, so only the 2^31 - 1 cap remains to check.
std::string randomBytes(double length);
std::string randomBytes(std::uint64_t length);

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

// --- Ledger D extensions (append-only) ---------------------------------------
// CODEGEN CONTRACT additions:
//   crypto.hmacSha256(key, data)   -> ::cp3::runtime::crypto::hmacSha256
//   crypto.hmacSha512(key, data)   -> ::cp3::runtime::crypto::hmacSha512
//   crypto.blake2b(data)           -> ::cp3::runtime::crypto::blake2b
//   crypto.getRandomValues(n)      -> ::cp3::runtime::crypto::getRandomValues
//     (wrapped with ::cp3::runtime::buffer::from, like randomBytes)
//   crypto.uuidParse(s)            -> ::cp3::runtime::crypto::uuidParse
//     (wrapped with ::cp3::runtime::buffer::from)
//   crypto.uuidValidate(s)         -> ::cp3::runtime::crypto::uuidValidate
//   crypto.uuidVersion(s)          -> ::cp3::runtime::crypto::uuidVersion
//   crypto.sha256Raw(data)         -> ::cp3::runtime::crypto::sha256Raw
//     (wrapped with ::cp3::runtime::buffer::from)
// A `string | Buffer` argument lowers to its byte view
// (`::cp3::runtime::buffer::view`), as for sha256/sha512; the C++ surface
// takes non-owning `std::string_view`s. Raw-byte results are `std::string`s
// that codegen wraps into the nominal `Buffer`.

/// `crypto.hmacSha256(key, data)`: lowercase hex HMAC-SHA-256 (64 chars).
/// libsodium `crypto_auth_hmacsha256` (state API, so any key length works;
/// the one-shot form would demand exactly 32 key bytes).
std::string hmacSha256(std::string_view key, std::string_view data);

/// `crypto.hmacSha512(key, data)`: lowercase hex HMAC-SHA-512 (128 chars).
/// libsodium `crypto_auth_hmacsha512` (state API, any key length).
std::string hmacSha512(std::string_view key, std::string_view data);

/// `crypto.blake2b(data)`: lowercase hex BLAKE2b digest (64 chars).
/// libsodium `crypto_generichash`, unkeyed, default 32-byte output.
std::string blake2b(std::string_view data);

/// `crypto.getRandomValues(length)`: `length` random bytes as a raw-byte
/// string (codegen wraps it into the nominal `Buffer`). WebCrypto-inspired
/// alias of `randomBytes`, backed by libsodium `randombytes_buf`; same
/// size validation (non-negative integer below 2^31).
std::string getRandomValues(double length);
/// Exact-width form of `getRandomValues` (see `randomBytes` above).
std::string getRandomValues(std::uint64_t length);

/// `crypto.uuidParse(text)`: the 16 raw bytes of a canonical UUID string
/// (8-4-4-4-12 hex, case-insensitive) as a raw-byte string; codegen wraps
/// it into the nominal `Buffer`. Throws a catchable value on malformed input.
std::string uuidParse(const std::string& text);

/// `crypto.uuidValidate(text)`: true iff `text` is a canonical UUID string.
/// Format only: the version/variant nibbles are not constrained, so any
/// version (or a version nibble of 0) validates.
bool uuidValidate(const std::string& text);

/// `crypto.uuidVersion(text)`: the version nibble (0-15) of a canonical UUID
/// string, or 0 when `text` is not a canonical UUID string.
double uuidVersion(const std::string& text);

/// `crypto.sha256Raw(data)`: the 32 raw SHA-256 digest bytes as a raw-byte
/// string; codegen wraps it into the nominal `Buffer`. Same input and
/// backend as `sha256`, without the hex encoding.
std::string sha256Raw(std::string_view data);

}  // namespace cp3::runtime::crypto
