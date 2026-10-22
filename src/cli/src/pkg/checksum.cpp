// =============================================================================
// src/cli/src/pkg/checksum.cpp - CRC-32 DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/checksum.hpp.
//
// CRC-32 is implemented from its published specification (IEEE 802.3) in plain
// C++20: no third-party code, no platform crypto APIs, so the same bytes are
// produced on every platform. SHA-256 lives in the common module
// (cp3/checksum.hpp) and is re-exported by the private header.
// =============================================================================
#include "detail/checksum.hpp"

#include <array>
#include <cstddef>

namespace cp3::cli {

namespace {

// -----------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320)
// -----------------------------------------------------------------------------

std::uint32_t crc32TableEntry(std::uint32_t index) noexcept {
    std::uint32_t c = index;
    for (int i = 0; i < 8; ++i) {
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    return c;
}

const std::array<std::uint32_t, 256>& crc32Table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) t[i] = crc32TableEntry(i);
        return t;
    }();
    return table;
}

}  // namespace

std::uint32_t crc32(std::string_view data, std::uint32_t seed) noexcept {
    const auto& table = crc32Table();
    std::uint32_t c = seed ^ 0xFFFFFFFFu;
    for (char raw : data) {
        c = table[(c ^ static_cast<unsigned char>(raw)) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

}  // namespace cp3::cli
