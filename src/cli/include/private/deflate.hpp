// =============================================================================
// cli/private/deflate.hpp — raw DEFLATE (RFC 1951) DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/inflate.cpp and src/cli/src/deflate.cpp.
// The `.cp3pak` container is gzip-compressed (CLI-README §5/§6); this is the
// deflate layer underneath it, implemented here so the CLI needs no external
// compression library or tool.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace cp3::cli {

// Decompresses a complete raw DEFLATE stream. `consumed` receives the number
// of input bytes the stream used (the last partial byte is counted once).
// Returns false with a message in `err` on malformed input.
bool inflateRaw(std::string_view input, std::string& out, std::size_t& consumed,
                std::string& err);

// Compresses with fixed Huffman codes plus greedy LZ77 matching (RFC 1951,
// BTYPE=01). Always produces a valid stream; very large inputs fall back to
// stored (uncompressed) blocks to bound memory use.
std::string deflateRaw(std::string_view input);

}  // namespace cp3::cli
