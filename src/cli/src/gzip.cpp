// =============================================================================
// cli/gzip.cpp - RFC 1952 gzip container DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/gzip.hpp.
//
// Writers emit a deterministic header (MTIME = 0, OS = unknown) so packing
// the same contents twice produces byte-identical archives. Readers accept
// every header flag the format defines, including concatenated members.
// =============================================================================
#include "private/gzip.hpp"

#include <cstdint>

#include "private/checksum.hpp"
#include "private/deflate.hpp"

namespace cp3::cli {

namespace {

bool fail(std::string& err, const char* message) {
    err = message;
    return false;
}

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void appendLe32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// Skips a NUL-terminated string (FNAME / FCOMMENT). Returns false when the
// member ends before the terminator.
bool skipCString(std::string_view input, std::size_t& pos) {
    while (pos < input.size()) {
        if (input[pos] == '\0') {
            ++pos;
            return true;
        }
        ++pos;
    }
    return false;
}

bool skipHeader(std::string_view input, std::size_t& pos, unsigned char flags,
                std::string& err) {
    if ((flags & 0x04) != 0) {  // FEXTRA
        if (pos + 2 > input.size()) return fail(err, "truncated gzip extra field");
        const std::size_t extra = static_cast<unsigned char>(input[pos]) |
                                  (static_cast<unsigned char>(input[pos + 1]) << 8);
        pos += 2;
        if (pos + extra > input.size()) {
            return fail(err, "truncated gzip extra field");
        }
        pos += extra;
    }
    if ((flags & 0x08) != 0 && !skipCString(input, pos)) {
        return fail(err, "truncated gzip file name");
    }
    if ((flags & 0x10) != 0 && !skipCString(input, pos)) {
        return fail(err, "truncated gzip comment");
    }
    if ((flags & 0x02) != 0) {  // FHCRC
        if (pos + 2 > input.size()) return fail(err, "truncated gzip header CRC");
        pos += 2;
    }
    return true;
}

}  // namespace

bool gzipCompress(std::string_view input, std::string& out, std::string& err) {
    err.clear();
    out.clear();
    out.push_back(static_cast<char>(0x1F));
    out.push_back(static_cast<char>(0x8B));
    out.push_back(static_cast<char>(8));    // CM = DEFLATE
    out.push_back(static_cast<char>(0));    // FLG
    appendLe32(out, 0);                     // MTIME (reproducible archives)
    out.push_back(static_cast<char>(0));    // XFL
    out.push_back(static_cast<char>(255));  // OS = unknown
    out += deflateRaw(input);
    appendLe32(out, crc32(input));
    appendLe32(out, static_cast<std::uint32_t>(input.size()));
    return true;
}

bool gzipDecompress(std::string_view input, std::string& out, std::string& err) {
    err.clear();
    out.clear();
    std::size_t pos = 0;
    if (input.empty()) return fail(err, "empty gzip stream");
    while (pos < input.size()) {
        if (input.size() - pos < 18) return fail(err, "truncated gzip member");
        const auto* head = reinterpret_cast<const unsigned char*>(input.data() + pos);
        if (head[0] != 0x1F || head[1] != 0x8B) {
            return fail(err, "not a gzip stream (bad magic bytes)");
        }
        if (head[2] != 8) return fail(err, "unsupported gzip compression method");
        const unsigned char flags = head[3];
        pos += 10;
        if (!skipHeader(input, pos, flags, err)) return false;

        std::string chunk;
        std::size_t consumed = 0;
        const std::string_view rest = input.substr(pos);
        if (!inflateRaw(rest, chunk, consumed, err)) return false;
        pos += consumed;
        if (pos + 8 > input.size()) return fail(err, "truncated gzip trailer");
        const auto* trailer =
            reinterpret_cast<const unsigned char*>(input.data() + pos);
        const std::uint32_t wantCrc = le32(trailer);
        const std::uint32_t wantSize = le32(trailer + 4);
        pos += 8;
        if (crc32(chunk) != wantCrc) return fail(err, "gzip CRC-32 mismatch");
        if (static_cast<std::uint32_t>(chunk.size()) != wantSize) {
            return fail(err, "gzip length mismatch");
        }
        out += chunk;
    }
    return true;
}

}  // namespace cp3::cli
