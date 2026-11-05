// =============================================================================
// cp3/core/bucket_hash.hpp - internal seeded bucket hash (wyhash).
//
// OWNERSHIP: runtime module. INTERNAL - never exposed to cp3 user code.
//
// Placement (docs/design/hash-placement.md, Roadmap E):
//   * Map/Set/LRU bucket hash = wyhash (final4) with a per-process random
//     seed from libsodium. This is the ONLY place a wyhash/xxhash-class hash
//     exists. It is a non-crypto hash for bucket distribution and HashDoS
//     defense only.
//   * std/crypto exposes libsodium-safe primitives only (SHA-256/512, AES-GCM,
//     randomBytes/UUID). No wyhash/xxhash name exists under crypto.
//   * std/hash exposes crc32/crc32c/fnv1a32/fnv1a64 checksums only. No
//     wyhash/xxhash/fnv-style user name is ever re-exported as crypto.
//
// NEVER expose: the seed (bucketSeed()) and the raw bucket hash value must
// not be surfaced to cp3 user code, serialized, logged, or compared across
// processes/hosts. Bucket values are per-process, per-run only.
//
// Algorithm: wyhash final4 (public-domain style reference, wangyi-fudan/
// wyhash). Implemented inline here so there is no vendored archive dependency
// and no user-facing symbol under cp3/std/*.hpp. The secret scalars below are
// approach-specific constants; the per-process randomness comes solely from
// seed (bucketSeed()), not from the constants. Any future swap to xxh3 requires
// a vendored xxhash row in third_party/MANIFEST.md and must stay internal.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include <cstring>
#include <string_view>

namespace cp3::runtime::detail {

// Per-process random seed, generated once from libsodium (see
// src/runtime/src/core/bucket_hash.cpp). Thread-safe. Never returned by any
// user-facing std/hash or std/crypto API.
std::uint64_t bucketSeed() noexcept;

// Internal forward declaration for seed initialization hook (for tests).
// Generated code must never call this directly; lazy init inside bucketSeed()
// is automatic.
void ensureBucketSeed() noexcept;

namespace bucket_hash_detail {

// wyhash final4 constants (0xa0761d6478bd642f, 0xe7037ed1a0b428db,
// 0x8ebc6af09c88c6e3, 0x589965cc75374cc3).
inline constexpr std::uint64_t kSecret0 = 0xa0761d6478bd642fULL;
inline constexpr std::uint64_t kSecret1 = 0xe7037ed1a0b428dbULL;
inline constexpr std::uint64_t kSecret2 = 0x8ebc6af09c88c6e3ULL;
inline constexpr std::uint64_t kSecret3 = 0x589965cc75374cc3ULL;

inline std::uint64_t read64(const unsigned char* p) noexcept {
    std::uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline std::uint64_t read32(const unsigned char* p) noexcept {
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline std::uint64_t read3(const unsigned char* p, std::size_t len) noexcept {
    return (static_cast<std::uint64_t>(p[0]) << 16) |
           (static_cast<std::uint64_t>(p[len >> 1]) << 8) |
           (static_cast<std::uint64_t>(p[len - 1]));
}
inline std::uint64_t mum(std::uint64_t a, std::uint64_t b) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t hi = 0;
    const std::uint64_t lo = _umul128(a, b, &hi);
    return lo ^ hi;
#elif defined(__GNUC__) || defined(__clang__)
    const unsigned __int128 r = static_cast<unsigned __int128>(a) * b;
    return static_cast<std::uint64_t>(r) ^ static_cast<std::uint64_t>(r >> 64);
#else
    // Portable 32-bit limb fallback (no 128-bit integer available).
    const std::uint64_t a_lo = a & 0xffffffffULL;
    const std::uint64_t a_hi = a >> 32;
    const std::uint64_t b_lo = b & 0xffffffffULL;
    const std::uint64_t b_hi = b >> 32;
    const std::uint64_t lo_lo = a_lo * b_lo;
    const std::uint64_t lo_hi = a_lo * b_hi;
    const std::uint64_t hi_lo = a_hi * b_lo;
    const std::uint64_t hi_hi = a_hi * b_hi;
    const std::uint64_t mid = lo_hi + hi_lo;
    const std::uint64_t mid_carry = mid < lo_hi ? (1ULL << 32) : 0ULL;
    const std::uint64_t low = lo_lo + (mid << 32);
    const std::uint64_t low_carry = low < lo_lo ? 1ULL : 0ULL;
    const std::uint64_t high = hi_hi + (mid >> 32) + mid_carry + low_carry;
    return low ^ high;
#endif
}

// wyhash final4 core. Caller supplies the per-process seed; domainTag is
// mixed into the initial seed for type-domain separation between strings,
// numbers, booleans and other key families so a single bucket layout cannot
// be cross-predicted across families.
inline std::uint64_t wyhashImpl(const unsigned char* p, std::size_t len,
                                std::uint64_t seed) noexcept {
    seed ^= kSecret0;
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    if (len <= 16) {
        if (len >= 8) {
            a = read64(p);
            b = read64(p + len - 8);
        } else if (len >= 4) {
            a = (read32(p) << 32) | read32(p + ((len >> 3) << 2));
            b = (read32(p + len - 4) << 32) | read32(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = read3(p, len);
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        std::size_t i = len;
        const unsigned char* q = p;
        if (i > 48) {
            std::uint64_t see1 = seed;
            std::uint64_t see2 = seed;
            do {
                seed = mum(read64(q) ^ kSecret1, read64(q + 8) ^ seed);
                see1 = mum(read64(q + 16) ^ kSecret2, read64(q + 24) ^ see1);
                see2 = mum(read64(q + 32) ^ kSecret3, read64(q + 40) ^ see2);
                q += 48;
                i -= 48;
            } while (i > 48);
            seed ^= see1 ^ see2;
        }
        while (i > 16) {
            seed = mum(read64(q) ^ kSecret1, read64(q + 8) ^ seed);
            i -= 16;
            q += 16;
        }
        a = read64(q + i - 16);
        b = read64(q + i - 8);
    }
    return mum(kSecret1 ^ static_cast<std::uint64_t>(len),
               mum(a ^ kSecret1, b ^ seed));
}

}  // namespace bucket_hash_detail

// Seeded bucket hash of a byte extent (string key bytes, canonical number
// bit pattern, boolean byte, pointer word, ...). The caller owns domain
// separation via domainTag when bytes from different key families could
// otherwise alias (e.g. string "1" vs number 1): pass a distinct tag per
// family. Default domainTag 0 hashes the bytes bare; KeyHash always passes
// a non-zero family tag.
inline std::uint64_t bucketHashBytes(const void* data, std::size_t len,
                                     std::uint64_t domainTag) noexcept {
    const std::uint64_t seed = bucketSeed() ^ domainTag;
    if (len == 0) {
        return bucket_hash_detail::wyhashImpl(nullptr, 0, seed);
    }
    const auto* p = static_cast<const unsigned char*>(data);
    return bucket_hash_detail::wyhashImpl(p, len, seed);
}

inline std::uint64_t bucketHash(const void* data, std::size_t len) noexcept {
    return bucketHashBytes(data, len, 0);
}

inline std::uint64_t bucketHashString(std::string_view s) noexcept {
    return bucketHashBytes(s.data(), s.size(), 0x100000001ULL);
}

}  // namespace cp3::runtime::detail
