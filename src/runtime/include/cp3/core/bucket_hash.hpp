// =============================================================================
// cp3/core/bucket_hash.hpp - internal seeded bucket hash (XXH3).
//
// OWNERSHIP: runtime module. INTERNAL - never exposed to cp3 user code.
//
// Placement (docs/design/hash-placement.md, Roadmap E):
//   * Map/Set/LRU bucket hash = XXH3_64bits_withSeed with a per-process random
//     seed from libsodium (bucketSeed()). This is the ONLY place an XXH3 hash
//     exists. It is a non-crypto hash for bucket distribution and HashDoS
//     defense only; the HashDoS defense comes from the per-process seed,
//     not from the algorithm.
//   * std/crypto exposes libsodium-safe primitives only (SHA-256/512, AES-GCM,
//     randomBytes/UUID). No xxhash name exists under crypto.
//   * std/hash exposes crc32/crc32c/fnv1a32/fnv1a64 checksums plus xxhash64,
//     which is a DIFFERENT function: XXH64 (frozen checksum, seed 0,
//     deterministic across runs, safe for persisted cache keys) exposed as
//     the user-facing hash.xxhash64. No xxhash name is re-exported as crypto,
//     and XXH3 is never exposed under any std/hash name: bucket values are
//     per-process and must never be persisted or compared across runs.
//
// NEVER expose: the seed (bucketSeed()) and the raw bucket hash value must
// not be surfaced to cp3 user code, serialized, logged, or compared across
// processes/hosts. Bucket values are per-process, per-run only.
//
// Backend: the vendored XXHash static archive (vendor/third_party/xxhash, v0.8.3,
// BSD-2-Clause; vendor/third_party/MANIFEST.md Ledger F). The archive is linked at
// generated-program link time by the std/collections native row
// (src/std/collections/native.hpp) alongside the libsodium archive; the
// xxhash64 path links the same archive via the std/hash native row - one
// archive, two consumers, no second vendored instance. The previous wyhash
// final4 implementation is gone: it was retired in favor of XXH3 (a single
// audited archive serves both the bucket hash and xxhash64; docs/design/
// xxhash64.md). The wyhash constants and mix code must not come back.
//
// The XXH3 symbol is declared in bucket_hash.cpp (not via <xxhash.h>) so the
// cp3_runtime unit build keeps compiling without a new include root; the
// declaration there is signature-identical to xxhash.h's. bucketHashBytes is
// declared here and defined in bucket_hash.cpp (headers declare, .cpp
// defines), so generated programs only link libxxhash.a and never need
// <xxhash.h> in their TUs.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
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

// Seeded bucket hash of a byte extent (string key bytes, canonical number
// bit pattern, boolean byte, pointer word, ...), implemented as
// XXH3_64bits_withSeed(data, len, bucketSeed() ^ domainTag) in
// bucket_hash.cpp. The caller owns domain separation via domainTag when
// bytes from different key families could otherwise alias (e.g. string "1"
// vs number 1): pass a distinct tag per family. Default domainTag 0 hashes
// the bytes with the bare per-process seed; KeyHash always passes a non-zero
// family tag.
std::uint64_t bucketHashBytes(const void* data, std::size_t len,
                              std::uint64_t domainTag) noexcept;

inline std::uint64_t bucketHash(const void* data, std::size_t len) noexcept {
    return bucketHashBytes(data, len, 0);
}

inline std::uint64_t bucketHashString(std::string_view s) noexcept {
    return bucketHashBytes(s.data(), s.size(), 0x100000001ULL);
}

}  // namespace cp3::runtime::detail
