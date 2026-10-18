// =============================================================================
// cli/private/checksum.hpp - SHA-256 and CRC-32 for the package manager.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/checksum.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines).
//
// SHA-256 backs `.cp3/lock.json` integrity entries; CRC-32 is what the gzip
// container requires (RFC 1952). Standard C++20 only, no dependencies.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cp3::cli {

// Lowercase hex SHA-256 of `data`.
std::string sha256Hex(std::string_view data);

// Incremental CRC-32 (IEEE 802.3 / gzip). Pass the previous result as `seed`.
std::uint32_t crc32(std::string_view data, std::uint32_t seed = 0) noexcept;

}  // namespace cp3::cli
