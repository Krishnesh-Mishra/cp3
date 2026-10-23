// =============================================================================
// src/cli/src/detail/gzip.hpp - RFC 1952 gzip container DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/archive/gzip.cpp. The `.cp3pak` file is a
// gzip-compressed archive (CLI-README §5/§6), so this is the container the
// package manager reads and writes.
// =============================================================================
#pragma once

#include <string>
#include <string_view>

namespace cp3::cli {

// Compresses `input` into one gzip member (header + DEFLATE + CRC-32 + size).
// Always succeeds; returns false only on impossible conditions.
bool gzipCompress(std::string_view input, std::string& out, std::string& err);

// Decompresses a gzip stream, verifying the CRC-32 and length trailer of every
// member (concatenated members are appended, as the format allows). Returns
// false with a message in `err` when the bytes are not a valid gzip stream.
bool gzipDecompress(std::string_view input, std::string& out, std::string& err);

}  // namespace cp3::cli
