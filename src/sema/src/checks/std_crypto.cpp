// =============================================================================
// src/sema/src/checks/std_crypto.cpp - `std/crypto` surface typing.
//
// OWNERSHIP: the crypto wave (std/crypto). One per-module check unit; the rest
// of sema never needs to change for this module (the dispatch row already
// exists in std_check.cpp).
//
// The typed table mirrors docs/standard-library/04 section crypto, which is
// the pinned signature surface the run-mode cases exercise:
//   randomUUID(): string
//   randomBytes(n: number): Buffer      (beta one: the raw-byte string
//                                        stand-in, see cp3/std/crypto.hpp)
//   sha256(s: string | Buffer): string  (hex; Buffer == string for now)
//   sha512(s: string | Buffer): string  (hex)
//   AES256(key: string, data: string): string
//   AES256Decrypt(key: string, encrypted: string): string
//
// Members outside the table stay CP3002 at codegen (the emitter reports the
// call as not implemented), so an undocumented member never lowers silently.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>

namespace cp3 {

std::optional<Type> Analyzer::cryptoMember(const std::string& prop) {
    if (prop == "randomUUID") return Type::function({}, Type::string());
    if (prop == "randomBytes") {
        return Type::function({Type::number()}, Type::string());
    }
    if (prop == "sha256" || prop == "sha512") {
        return Type::function({Type::string()}, Type::string());
    }
    if (prop == "AES256" || prop == "AES256Decrypt") {
        return Type::function({Type::string(), Type::string()}, Type::string());
    }
    return std::nullopt;
}

}  // namespace cp3
