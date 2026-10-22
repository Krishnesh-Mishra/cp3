// =============================================================================
// src/cli/src/detail/checksum.hpp - SHA-256 and CRC-32 for the package manager.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/checksum.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines).
//
// SHA-256 moved to cp3/checksum.hpp (the common module) when the native
// binding reader needed the same fingerprint; it is re-exported here so every
// package-manager call site keeps its spelling. CRC-32 stays private: only
// the gzip container needs it.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cp3/checksum.hpp"

namespace cp3::cli {

// Lowercase hex SHA-256 of `data` (the common implementation).
using cp3::sha256Hex;

// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320). Pass the previous
// result as `seed`.
std::uint32_t crc32(std::string_view data, std::uint32_t seed = 0) noexcept;

}  // namespace cp3::cli
