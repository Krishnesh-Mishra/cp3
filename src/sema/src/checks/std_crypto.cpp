// =============================================================================
// src/sema/src/checks/std_crypto.cpp - `std/crypto` surface typing.
//
// OWNERSHIP: the crypto wave (std/crypto). One per-module check unit; the rest
// of sema never needs to change for this module (the dispatch row already
// exists in std_check.cpp).
//
// STUB: returns nullopt until the crypto wave lands, so `crypto.*` member
// access keeps the permissive ambient shape it had before this file existed
// (the pinned check-mode signature cases pass unchanged either way).
// The typed table the wave adds here mirrors docs/standard-library/04:
//   randomUUID(): string
//   randomBytes(n: number): string          (base64)
//   sha256(s: string): string               (hex)
//   sha512(s: string): string               (hex)
//   aes256Encrypt / aes256Decrypt per the STDLIB spec surface.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>

namespace cp3 {

std::optional<Type> Analyzer::cryptoMember(const std::string& prop) {
    (void)prop;
    return std::nullopt;
}

}  // namespace cp3
