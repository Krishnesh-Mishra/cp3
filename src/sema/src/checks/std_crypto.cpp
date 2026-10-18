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
// The family is CLOSED (std_check.cpp isClosedStdNamespace): a member outside
// the table is CP3305 with these names as candidates instead of a CP3002 skip.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// One implemented member: the name and its type factory. Same table drives
// member typing and the CP3305 candidate list.
struct CryptoRow {
    const char* name;
    Type (*make)();
};

Type zeroToString() { return Type::function({}, Type::string()); }
Type numberToString() { return Type::function({Type::number()}, Type::string()); }
Type stringToString() { return Type::function({Type::string()}, Type::string()); }
Type twoStringToString() {
    return Type::function({Type::string(), Type::string()}, Type::string());
}

const CryptoRow kRows[] = {
    {"randomUUID", zeroToString},      {"randomBytes", numberToString},
    {"sha256", stringToString},        {"sha512", stringToString},
    {"AES256", twoStringToString},     {"AES256Decrypt", twoStringToString},
};

}  // namespace

std::optional<Type> Analyzer::cryptoMember(const std::string& prop) {
    for (const CryptoRow& row : kRows) {
        if (prop == row.name) return row.make();
    }
    return std::nullopt;
}

std::vector<std::string> cryptoMemberNames() {
    std::vector<std::string> names;
    names.reserve(std::size(kRows));
    for (const CryptoRow& row : kRows) names.push_back(row.name);
    return names;
}

}  // namespace cp3
