// =============================================================================
// src/std/hash/runtime.cpp - `std/hash` compiled backend TU (xxhash64).
//
// OWNERSHIP: the std/hash family (src/std/hash/**). The xxhash64 slice of the
// family is backed by the vendored XXHash static archive
// (vendor/third_party/xxhash, v0.8.3, BSD-2-Clause; vendor/third_party/MANIFEST.md Ledger F)
// instead of inline code: the crc32/crc32c/fnv1a32/fnv1a64 surface above stays
// header-only in cp3/std/hash.hpp. This TU is compiled + linked only when a
// program imports "std/hash" (src/std/hash/native.hpp, D68) and the archive is
// referenced only from here, so a program that never calls xxhash64 pulls no
// xxhash objects out of the archive.
//
// Only this wrapper may implement the name: hash.xxhash64 is XXH64(data, len,
// seed=0). No seed parameter, no XXH3/XXH32/XXH128 name, and no hand-rolled
// lookalike. The internal bucket hash (src/runtime/.../bucket_hash.hpp) uses
// a DIFFERENT function (XXH3_64bits_withSeed, per-process seed) and never
// appears under std/hash.
// =============================================================================
#include "cp3/std/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <xxhash.h>

namespace cp3::runtime::hash {

std::uint64_t xxhash64(std::string_view data) {
    // XXH64 tolerates a null pointer only for length 0; spell the empty
    // case out so every TU behaves identically whatever data.data() is.
    if (data.empty()) {
        return static_cast<std::uint64_t>(XXH64("", 0, 0ULL));
    }
    return static_cast<std::uint64_t>(XXH64(data.data(), data.size(), 0ULL));
}

}  // namespace cp3::runtime::hash
