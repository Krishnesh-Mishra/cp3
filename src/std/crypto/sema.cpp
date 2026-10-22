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
// CP3305 with these names as candidates instead of a CP3002 skip.
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
};

}  // namespace

std::optional<Type> Analyzer::cryptoMember(const std::string& prop) {
    return lookupMember(kRows, prop);
}

std::vector<std::string> cryptoMemberNames() {
    return memberNames(kRows);
}

StdMemberDoc cryptoMemberDoc(const std::string& prop) {
    return memberDoc(kRows, prop);
}

}  // namespace cp3
