// =============================================================================
// cp3/std/hash.hpp - `std/hash` backing: fast non-crypto checksums.
//
// OWNERSHIP: the std/hash family (src/std/hash/**). The header is the
// generated-code contract, so keep it additive like every cp3/std header, D14.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   hash.crc32(s)     -> ::cp3::runtime::hash::crc32(s)      (std::uint32_t)
//   hash.crc32c(s)    -> ::cp3::runtime::hash::crc32c(s)     (std::uint32_t)
//   hash.fnv1a32(s)   -> ::cp3::runtime::hash::fnv1a32(s)    (std::uint32_t)
//   hash.fnv1a64(s)   -> ::cp3::runtime::hash::fnv1a64(s)    (std::uint64_t)
//
// A Buffer argument lowers to its byte view
// (`::cp3::runtime::buffer::view`, cp3/std/buffer.hpp); a string argument
// lowers unchanged. This C++ surface takes a non-owning `std::string_view`
// over the exact input bytes (a CP3 string is already UTF-8).
//
// A TypeScript project sees every return as `number`; a Rust/C++ project
// sees the exact native width (`u32` / `u64`). Codegen widens with
// `static_cast<double>` only in a TypeScript project (the fs/process
// pattern), so only the 64-bit result can lose precision there.
//
// -----------------------------------------------------------------------------
// SEMANTICS / LIMITATIONS
// -----------------------------------------------------------------------------
//   * These are checksums, NOT security. Fast non-crypto checksums are easy
//     to forge and must never be used for passwords, signatures, tokens, or
//     integrity against an adversary. Use std/crypto for that.
//   * crc32 is IEEE 802.3 CRC-32 (reflected poly 0xEDB88320), crc32c is
//     Castagnoli CRC-32C (reflected poly 0x82F63B78). Both use init/xorout
//     0xFFFFFFFF and are table-driven (256-entry tables built constexpr).
//   * fnv1a32 / fnv1a64 are FNV-1a (offset basis, xor-then-multiply) at
//     32 and 64 bits.
//   * xxhash64 is deliberately NOT provided: no XXHash archive is vendored
//     (third_party/MANIFEST.md has no xxhash row), so it is BLOCKED and must
//     not be hand-rolled here under that name. Roadmap section E also
//     reserves wyhash/xxhash-class hashes to the internal Map/Set bucket
//     hash, never to a user-facing checksum name.
//   * Header-only, table-driven CRC and scalar FNV: no vendored dependency,
//     no runtime TU, no link flags.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cp3::runtime::hash {

namespace detail {

// --- CRC32 tables (hand-written, constexpr) ---
// Reflected algorithm: for each byte, crc = table[(crc ^ byte) & 0xFF] ^
// (crc >> 8). The tables are generated from the polynomial directly, so no
// vendored CRC archive is needed and no bit-by-byte loop is kept.
constexpr std::array<std::uint32_t, 256> makeCrcTable(std::uint32_t poly) {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? ((crc >> 1) ^ poly) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

// IEEE 802.3 reflected polynomial 0xEDB88320.
inline constexpr std::array<std::uint32_t, 256> kCrc32Table =
    makeCrcTable(0xEDB88320u);
// Castagnoli reflected polynomial 0x82F63B78.
inline constexpr std::array<std::uint32_t, 256> kCrc32cTable =
    makeCrcTable(0x82F63B78u);

inline std::uint32_t crcImpl(std::string_view data,
                             const std::array<std::uint32_t, 256>& table) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (char ch : data) {
        const std::uint32_t byte = static_cast<unsigned char>(ch);
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace detail

/// `hash.crc32(data)`: IEEE CRC-32 as an unsigned 32-bit value.
inline std::uint32_t crc32(std::string_view data) {
    return detail::crcImpl(data, detail::kCrc32Table);
}

/// `hash.crc32c(data)`: Castagnoli CRC-32C as an unsigned 32-bit value.
inline std::uint32_t crc32c(std::string_view data) {
    return detail::crcImpl(data, detail::kCrc32cTable);
}

/// `hash.fnv1a32(data)`: FNV-1a 32-bit as an unsigned 32-bit value.
inline std::uint32_t fnv1a32(std::string_view data) {
    constexpr std::uint32_t kOffset = 2166136261u;   // 0x811C9DC5
    constexpr std::uint32_t kPrime = 16777619u;      // 0x01000193
    std::uint32_t h = kOffset;
    for (char ch : data) {
        h ^= static_cast<unsigned char>(ch);
        h *= kPrime;
    }
    return h;
}

/// `hash.fnv1a64(data)`: FNV-1a 64-bit as an unsigned 64-bit value.
inline std::uint64_t fnv1a64(std::string_view data) {
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t h = kOffset;
    for (char ch : data) {
        h ^= static_cast<unsigned char>(ch);
        h *= kPrime;
    }
    return h;
}

}  // namespace cp3::runtime::hash
