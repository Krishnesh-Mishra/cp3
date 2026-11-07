// =============================================================================
// src/std/hash/sema.cpp - `std/hash` member typing and documentation.
//
// OWNERSHIP: the std/hash family (src/std/hash/**). One per-module check unit;
// the rest of sema never needs to change for this module (std_check.cpp carries
// the namespace dispatch row and forwards here).
//
// The typed table mirrors docs/content/03-standard-library/23-hash.md, the
// pinned signature surface the cases exercise:
//   crc32(input: string | Buffer): u32    (IEEE 802.3 CRC-32)
//   crc32c(input: string | Buffer): u32   (Castagnoli CRC-32C)
//   fnv1a32(input: string | Buffer): u32  (FNV-1a 32-bit)
//   fnv1a64(input: string | Buffer): u64  (FNV-1a 64-bit)
//   xxhash64(input: string | Buffer): u64 (XXH64, seed 0)
//
// Width pattern (fs/process, cp3/host/std_integer.hpp): a TypeScript project
// keeps `number` for every return; a Rust/C++ project gets the exact native
// width via StdIntegerKind::Checksum32 (canonical "u32") / Checksum64
// (canonical "u64") through stdIntegerType, so the module spells no native
// id itself and typing/docs cannot drift when the table moves.
//
// xxhash64 IS in the table: the vendored XXHash static archive
// (vendor/third_party/xxhash, v0.8.3) is wired through the std/hash native row
// (src/std/hash/native.hpp) and implemented in src/std/hash/runtime.cpp as
// XXH64(data, len, seed 0). Only this wrapper may implement the name - a
// hand-rolled replica is forbidden exactly as before. The internal bucket
// hash (XXH3, seeded per-process) is a different function and never appears
// in this table.
//
// The same table drives member typing, the CP3305 candidate list and the
// editor documentation, so the three can never drift apart. hash is a CLOSED
// family (std_check.cpp isClosedStdNamespace): a member outside the table is
// CP3305 with these names as candidates, never an untyped fallthrough.
//
// Every description states the non-security boundary: checksums, not crypto.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "cp3/host/std_integer.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The exact native width behind a checksum return is the shared table's
// semantic kinds (cp3/host/std_integer.hpp). Replaced the former
// Type::native("u32")/Type::native("u64") locals (ROADMAP C) with
// stdIntegerType(StdIntegerKind::Checksum32/Checksum64, ts) below.
Type stringOrBufferToNumber() {
    return Type::function({Type::unionOf({Type::string(), byteBufferType()})},
                          Type::number());
}

const MemberRow kHashRows[] = {
    {"crc32", stringOrBufferToNumber,
     "crc32(input: string | Buffer): u32",
     "IEEE 802.3 CRC-32 checksum as an unsigned 32-bit value (number in a "
     "TypeScript project). Fast non-crypto checksum, not security: safe for "
     "cache keys and corruption checks, never for authentication or "
     "integrity against an adversary (use std/crypto for that)."},
    {"crc32c", stringOrBufferToNumber,
     "crc32c(input: string | Buffer): u32",
     "Castagnoli CRC-32C checksum as an unsigned 32-bit value (number in a "
     "TypeScript project). Fast non-crypto checksum, not security: safe for "
     "cache keys and corruption checks, never for authentication or "
     "integrity against an adversary (use std/crypto for that)."},
    {"fnv1a32", stringOrBufferToNumber,
     "fnv1a32(input: string | Buffer): u32",
     "FNV-1a 32-bit checksum as an unsigned 32-bit value (number in a "
     "TypeScript project). Fast non-crypto checksum, not security: safe for "
     "cache keys and corruption checks, never for authentication or "
     "integrity against an adversary (use std/crypto for that)."},
    {"fnv1a64", stringOrBufferToNumber,
     "fnv1a64(input: string | Buffer): u64",
     "FNV-1a 64-bit checksum as an unsigned 64-bit value (number in a "
     "TypeScript project, where values above 2^53 lose precision). Fast "
     "non-crypto checksum, not security: safe for cache keys and corruption "
     "checks, never for authentication or integrity against an adversary "
     "(use std/crypto for that)."},
    {"xxhash64", stringOrBufferToNumber,
     "xxhash64(input: string | Buffer): u64",
     "XXH64 checksum (seed 0, deterministic across runs) as an unsigned "
     "64-bit value (number in a TypeScript project, where values above "
     "2^53 lose precision). Fast non-crypto checksum, not security: safe "
     "for cache keys and corruption checks, never for authentication or "
     "integrity against an adversary (use std/crypto for that). The seed "
     "is fixed to 0 and is not a parameter."},
};

}  // namespace

std::optional<Type> Analyzer::hashMember(const std::string& prop) {
    std::optional<Type> member = lookupMember(kHashRows, prop);
    if (!member) return member;
    // Width pattern (fs/process): a TypeScript project keeps `number`; a
    // Rust/C++ project gets the exact native width. The table row above
    // carries `number` as the placeholder return (last function part).
    if (!member->parts.empty()) {
        if (prop == "fnv1a64" || prop == "xxhash64") {
            member->parts.back() =
                stdIntegerType(StdIntegerKind::Checksum64, options_.typeSystem);
        } else {
            member->parts.back() =
                stdIntegerType(StdIntegerKind::Checksum32, options_.typeSystem);
        }
    }
    return member;
}

std::vector<std::string> hashMemberNames() { return memberNames(kHashRows); }

StdMemberDoc hashMemberDoc(const std::string& prop) {
    return memberDoc(kHashRows, prop);
}

}  // namespace cp3
