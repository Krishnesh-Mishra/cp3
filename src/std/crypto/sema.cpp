// =============================================================================
// src/std/crypto/sema.cpp - `std/crypto` surface typing and documentation.
//
// OWNERSHIP: the std/crypto family (src/std/crypto/**). One per-module check unit; the rest
// of sema never needs to change for this module (the dispatch row already
// exists in std_check.cpp).
//
// The typed table mirrors docs/standard-library/04 section crypto, which is
// the pinned signature surface the run-mode cases exercise:
//   randomUUID(): string
//   randomBytes(n: number): Buffer      (the nominal std/buffer `Buffer`;
//                                        codegen wraps the runtime's raw bytes
//                                        with ::cp3::runtime::buffer::from)
//   sha256(s: string | Buffer): string  (hex; a Buffer argument lowers to its
//                                        bytes in codegen, cp3/std/buffer.hpp)
//   sha512(s: string | Buffer): string  (hex)
//   AES256(key: string, data: string): string
//   AES256Decrypt(key: string, encrypted: string): string
//
// The same table drives member typing, the CP3305 candidate list and the
// editor documentation, so the three can never drift apart. The family is
// CLOSED (std_check.cpp isClosedStdNamespace): a member outside the table is
// CP3305 with these names as candidates, never an untyped fallthrough.
//
// The descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, crypto section):
// libsodium-backed, with authenticated encryption by default.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/host/std_integer.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type zeroToString() { return Type::function({}, Type::string()); }
Type numberToBuffer() {
    return Type::function({Type::number()}, byteBufferType());
}
Type stringOrBufferToString() {
    return Type::function({Type::unionOf({Type::string(), byteBufferType()})},
                          Type::string());
}
Type twoStringToString() {
    return Type::function({Type::string(), Type::string()}, Type::string());
}

// --- Ledger D extension factories (append-only) -------------------------------
Type stringOrBuffer2ToString() {
    const Type in = Type::unionOf({Type::string(), byteBufferType()});
    return Type::function({in, in}, Type::string());
}
Type stringOrBufferToBuffer() {
    return Type::function({Type::unionOf({Type::string(), byteBufferType()})},
                          byteBufferType());
}
Type stringToBuffer() {
    return Type::function({Type::string()}, byteBufferType());
}
Type stringToBoolean() { return Type::function({Type::string()}, Type::boolean()); }
Type stringToNumber() { return Type::function({Type::string()}, Type::number()); }

// One implemented member: its name, its type factory and its documentation.
// The same table drives member typing and the CP3305 candidate list.
const MemberRow kRows[] = {
    {"randomUUID", zeroToString, "randomUUID(): string",
     "Returns a random UUID (version 4). This one is dependency-free - no "
     "libsodium needed."},
    {"randomBytes", numberToBuffer, "randomBytes(length: number): Buffer",
     "Returns `length` cryptographically strong random bytes as a `Buffer`."},
    {"sha256", stringOrBufferToString, "sha256(data: string | Buffer): string",
     "Returns the hex-encoded SHA-256 digest of `data`."},
    {"sha512", stringOrBufferToString, "sha512(data: string | Buffer): string",
     "Returns the hex-encoded SHA-512 digest of `data`."},
    {"AES256", twoStringToString, "AES256(key: string, data: string): string",
     "Encrypts `data` with `key` and returns the base64 result: the nonce, "
     "the ciphertext and the authentication tag are bundled together.\n\n"
     "The encryption is authenticated (AES-256-GCM) by default, so the "
     "nonce never has to be managed by hand."},
    {"AES256Decrypt", twoStringToString,
     "AES256Decrypt(key: string, encrypted: string): string",
     "Decrypts the base64 `encrypted` value with `key` and returns the "
     "plaintext. A tampered ciphertext or a wrong key fails the "
     "authentication check and throws a catchable error - it never returns "
     "silently-wrong plaintext."},
    // --- Ledger D extensions (append-only) ---------------------------------
    {"hmacSha256", stringOrBuffer2ToString,
     "hmacSha256(key: string | Buffer, data: string | Buffer): string",
     "Returns the hex-encoded HMAC-SHA-256 of `data` under `key` "
     "(libsodium crypto_auth_hmacsha256; any key length)."},
    {"hmacSha512", stringOrBuffer2ToString,
     "hmacSha512(key: string | Buffer, data: string | Buffer): string",
     "Returns the hex-encoded HMAC-SHA-512 of `data` under `key` "
     "(libsodium crypto_auth_hmacsha512; any key length)."},
    {"blake2b", stringOrBufferToString, "blake2b(data: string | Buffer): string",
     "Returns the hex-encoded BLAKE2b digest (32 bytes) of `data` "
     "(libsodium crypto_generichash, unkeyed)."},
    {"getRandomValues", numberToBuffer,
     "getRandomValues(length: number): Buffer",
     "Returns `length` cryptographically strong random bytes as a `Buffer` "
     "(libsodium randombytes; WebCrypto-inspired alias of randomBytes)."},
    {"uuidParse", stringToBuffer, "uuidParse(uuid: string): Buffer",
     "Parses a canonical UUID string into its 16 raw bytes as a `Buffer`. "
     "Throws a catchable error on malformed input."},
    {"uuidValidate", stringToBoolean, "uuidValidate(uuid: string): boolean",
     "Returns true iff `uuid` is a canonical UUID string (format only; "
     "the version/variant nibbles are not constrained)."},
    {"uuidVersion", stringToNumber, "uuidVersion(uuid: string): number",
     "Returns the version nibble (0-15) of a canonical UUID string, "
     "or 0 when the input is not a canonical UUID string."},
    {"sha256Raw", stringOrBufferToBuffer,
     "sha256Raw(data: string | Buffer): Buffer",
     "Returns the raw 32 SHA-256 digest bytes of `data` as a `Buffer` "
     "(sha256 without the hex encoding)."},
};

}  // namespace

std::optional<Type> Analyzer::cryptoMember(const std::string& prop) {
    std::optional<Type> member = lookupMember(kRows, prop);
    if (!member) return member;
    // Width pattern: the byte-count argument of `randomBytes` /
    // `getRandomValues` is the std length width (`usize` in a Rust project,
    // `size_t` in a C++ one, `number` in a TypeScript one). The table row
    // carries `number` as the placeholder parameter (first function part).
    if ((prop == "randomBytes" || prop == "getRandomValues") &&
        !member->parts.empty()) {
        member->parts[0] =
            stdIntegerType(StdIntegerKind::Length, options_.typeSystem);
    }
    return member;
}

std::vector<std::string> cryptoMemberNames() {
    return memberNames(kRows);
}

StdMemberDoc cryptoMemberDoc(const std::string& prop) {
    return memberDoc(kRows, prop);
}

}  // namespace cp3
