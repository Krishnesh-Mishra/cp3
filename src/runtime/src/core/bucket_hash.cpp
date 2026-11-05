// =============================================================================
// cp3/src/core/bucket_hash.cpp - per-process seed + XXH3 bucket mix.
//
// See cp3/core/bucket_hash.hpp and docs/design/hash-placement.md. This TU is
// the only place libsodium is touched for bucket hashing: sodium_init() +
// randombytes_buf(). std/crypto and std/hash never reach this file, and this
// seed is never returned to user code.
//
// Backend: libsodium 1.0.22 static (third_party/libsodium, D70), the same
// backend std/crypto uses, plus the vendored XXHash static archive
// (third_party/xxhash, v0.8.3). The mix function is XXH3_64bits_withSeed
// (XXH3, NOT the XXH64 that std/hash exposes as hash.xxhash64); XXH3 is the
// faster of the two and its algorithmic instability across upstream minor
// versions is safe here because bucket values are per-process only. The
// generated-program link for any program that uses Map/Set/LRUCache must
// therefore carry -lsodium AND -lxxhash (src/std/collections/native.hpp).
// Until the -lxxhash wiring lands, a program that does not link libxxhash.a
// and uses a collection will fail the final link with an undefined reference
// to XXH3_64bits_withSeed - that failure is intentional and visible, not a
// silent fallback to std::hash or wyhash (the wyhash implementation is
// retired; do not reintroduce it as a fallback).
// Other runtime TUs that do not reference bucketSeed()/bucketHashBytes() do
// not pay for either archive: the static archive only pulls this object when
// the symbol is referenced.
//
// XXH3_64bits_withSeed is declared below with a signature identical to
// third_party/xxhash/include/xxhash.h (uint64_t(const void*, size_t,
// uint64_t); XXH64_hash_t IS uint64_t and xxhash.h wraps its declarations in
// extern "C", so this declaration is compatible if <xxhash.h> is ever
// included alongside it). Declaring it directly keeps the cp3_runtime unit
// build compiling without a new include root; if the runtime CMake gains
// third_party/xxhash/include (the lead follow-up noted in bucket_hash.hpp),
// replace this declaration with `#include <xxhash.h>`.
// =============================================================================
#include "cp3/core/bucket_hash.hpp"

#include <cstddef>
#include <cstdint>
#include <random>

#include <sodium.h>

// XXH3_64bits_withSeed, C linkage, exactly as declared in xxhash.h
// (bool return type does not exist; the function always returns a hash).
extern "C" std::uint64_t XXH3_64bits_withSeed(const void* input,
                                              std::size_t length,
                                              std::uint64_t seed);

namespace cp3::runtime::detail {
namespace {

// Generated once, on first map/set use. sodium_init() is idempotent, so no
// separate init hook is required from generated code or main().
std::uint64_t generateSeed() noexcept {
    if (sodium_init() >= 0) {
        unsigned char raw[32];
        randombytes_buf(raw, sizeof(raw));
        // Fold 32 random bytes to one 64-bit seed without the bucket hash
        // itself (no recursion: wyhashImpl is seeded, not a combiner).
        std::uint64_t seed = 0;
        for (std::size_t i = 0; i < sizeof(seed); ++i) {
            seed = (seed << 8) | static_cast<std::uint64_t>(raw[i]);
            seed ^= (static_cast<std::uint64_t>(raw[i + 8]) << 32) |
                    static_cast<std::uint64_t>(raw[i + 16]);
        }
        // Fold the tail so every random byte influences the seed.
        for (std::size_t i = 8; i < sizeof(raw); ++i) {
            seed ^= (static_cast<std::uint64_t>(raw[i]) << ((i % 8) * 8));
            seed *= 0x100000001b3ULL;  // FNV prime, as an avalanche mixer only
        }
        sodium_memzero(raw, sizeof(raw));
        if (seed != 0) return seed;
    }
    // Degraded fallback when libsodium cannot initialize (should not happen
    // in the pinned build; it would already break std/crypto the same way).
    // Keep HashDoS defense by never falling back to a fixed seed or to
    // std::hash: std::random_device is still per-process random on the
    // supported platforms, and the seed is still never exposed.
    std::random_device rd;
    std::uint64_t a = static_cast<std::uint64_t>(rd()) << 32 |
                      static_cast<std::uint64_t>(rd());
    std::uint64_t b = static_cast<std::uint64_t>(rd()) << 32 |
                      static_cast<std::uint64_t>(rd());
    std::uint64_t fallback = a ^ (b * 0x9e3779b97f4a7c15ULL);
    return fallback != 0 ? fallback : 0x9e3779b97f4a7c15ULL;
}

std::uint64_t cachedSeed() noexcept {
    static const std::uint64_t seed = generateSeed();
    return seed;
}

}  // namespace

void ensureBucketSeed() noexcept { (void)cachedSeed(); }

std::uint64_t bucketSeed() noexcept { return cachedSeed(); }

std::uint64_t bucketHashBytes(const void* data, std::size_t len,
                              std::uint64_t domainTag) noexcept {
    // XXH3 accepts a null pointer only for length 0; spell the empty case
    // out so behavior is identical whatever the caller passes for data.
    const void* bytes = data != nullptr ? data : "";
    const std::size_t count = data != nullptr ? len : 0;
    return XXH3_64bits_withSeed(bytes, count, bucketSeed() ^ domainTag);
}

}  // namespace cp3::runtime::detail
