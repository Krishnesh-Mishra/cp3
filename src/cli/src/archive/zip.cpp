// =============================================================================
// src/cli/src/archive/zip.cpp - minimal ZIP archive DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/archive/zip.hpp.
//
// The reader walks the central directory (the authoritative catalog), then
// seeks each local header for the payload. It accepts the two methods ZIP
// writers use for native bundles - stored and DEFLATE - and verifies the
// recorded CRC-32, so a truncated or corrupted download is rejected rather
// than extracted. No external unzip tool is involved.
// =============================================================================
#include "detail/archive/zip.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "detail/archive/deflate.hpp"
#include "detail/cache/checksum.hpp"

namespace cp3::cli {

namespace {

constexpr std::uint32_t kEndOfCentralDirectory = 0x06054b50u;
constexpr std::uint32_t kCentralDirectoryHeader = 0x02014b50u;
constexpr std::uint32_t kLocalFileHeader = 0x04034b50u;
constexpr std::uint64_t kMaxMemberBytes = 1ull << 30;  // 1 GiB per file

std::uint16_t readU16(std::string_view bytes, std::size_t at) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(bytes[at]) |
        (static_cast<unsigned char>(bytes[at + 1]) << 8));
}

std::uint32_t readU32(std::string_view bytes, std::size_t at) {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(bytes[at]) |
        (static_cast<unsigned char>(bytes[at + 1]) << 8) |
        (static_cast<unsigned char>(bytes[at + 2]) << 16) |
        (static_cast<unsigned char>(bytes[at + 3]) << 24));
}

bool fail(std::string& err, const std::string& message) {
    err = message;
    return false;
}

// ZIP names are '/'-separated, but Windows-produced archives sometimes use
// backslashes. Normalize, then reject anything that could escape the
// extraction root. Directory entries end in '/' and are skipped by the caller.
bool safeMemberName(std::string name, std::string& normalized,
                    std::string& err) {
    for (char& c : name) {
        if (c == '\\') c = '/';
    }
    if (name.empty()) return fail(err, "ZIP member has an empty name");
    if (name.front() == '/') {
        return fail(err, "ZIP member has an absolute path: " + name);
    }
    if (name.size() >= 2 && name[1] == ':') {
        return fail(err, "ZIP member has a drive-qualified path: " + name);
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::size_t end = slash == std::string::npos ? name.size() : slash;
        const std::string part = name.substr(start, end - start);
        if (part == "..") {
            return fail(err, "ZIP member escapes the extraction root: " + name);
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    normalized = std::move(name);
    return true;
}

struct CentralEntry {
    std::string name;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressed = 0;
    std::uint32_t uncompressed = 0;
    std::uint32_t localOffset = 0;
    std::uint32_t externalAttributes = 0;
};

bool isSymlink(const CentralEntry& entry) {
    const std::uint32_t mode = entry.externalAttributes >> 16;
    return (mode & 0xF000u) == 0xA000u;
}

bool readCentralDirectory(std::string_view bytes, std::size_t centralOffset,
                          std::size_t centralSize, std::size_t count,
                          std::vector<CentralEntry>& out, std::string& err) {
    if (centralOffset > bytes.size() ||
        centralSize > bytes.size() - centralOffset) {
        return fail(err, "ZIP central directory is out of bounds");
    }
    const std::size_t end = centralOffset + centralSize;
    std::size_t at = centralOffset;
    out.clear();
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (at + 46 > end) {
            return fail(err, "truncated ZIP central directory entry");
        }
        if (readU32(bytes, at) != kCentralDirectoryHeader) {
            return fail(err, "bad ZIP central directory signature");
        }
        CentralEntry entry;
        entry.flags = readU16(bytes, at + 8);
        entry.method = readU16(bytes, at + 10);
        entry.crc = readU32(bytes, at + 16);
        entry.compressed = readU32(bytes, at + 20);
        entry.uncompressed = readU32(bytes, at + 24);
        const std::uint16_t nameLen = readU16(bytes, at + 28);
        const std::uint16_t extraLen = readU16(bytes, at + 30);
        const std::uint16_t commentLen = readU16(bytes, at + 32);
        entry.externalAttributes = readU32(bytes, at + 38);
        entry.localOffset = readU32(bytes, at + 42);
        const std::size_t next = at + 46 + nameLen + extraLen + commentLen;
        if (next > end || at + 46 + nameLen > bytes.size()) {
            return fail(err, "truncated ZIP central directory entry");
        }
        entry.name.assign(bytes.data() + at + 46, nameLen);
        out.push_back(std::move(entry));
        at = next;
    }
    return true;
}

bool readMember(std::string_view bytes, const CentralEntry& entry,
                std::string& out, std::string& err) {
    if (entry.localOffset > bytes.size() ||
        entry.localOffset + 30 > bytes.size()) {
        return fail(err, "ZIP local header is out of bounds");
    }
    if (readU32(bytes, entry.localOffset) != kLocalFileHeader) {
        return fail(err, "bad ZIP local header signature");
    }
    const std::uint16_t nameLen = readU16(bytes, entry.localOffset + 26);
    const std::uint16_t extraLen = readU16(bytes, entry.localOffset + 28);
    const std::uint64_t dataAt =
        static_cast<std::uint64_t>(entry.localOffset) + 30 + nameLen + extraLen;
    if (dataAt > bytes.size() ||
        entry.compressed > bytes.size() - static_cast<std::size_t>(dataAt)) {
        return fail(err, "ZIP member payload is out of bounds");
    }
    const std::string_view payload =
        bytes.substr(static_cast<std::size_t>(dataAt), entry.compressed);
    out.clear();
    if (entry.method == 0) {
        if (entry.compressed != entry.uncompressed) {
            return fail(err, "stored ZIP member has inconsistent sizes");
        }
        out.assign(payload.data(), payload.size());
    } else if (entry.method == 8) {
        std::size_t consumed = 0;
        if (!inflateRaw(payload, out, consumed, err)) {
            err = "cannot inflate ZIP member '" + entry.name + "': " + err;
            return false;
        }
    } else {
        return fail(err, "unsupported ZIP compression method " +
                             std::to_string(entry.method) + " for '" +
                             entry.name + "'");
    }
    if (static_cast<std::uint64_t>(out.size()) != entry.uncompressed) {
        return fail(err, "ZIP member '" + entry.name + "' has the wrong size");
    }
    if (crc32(out) != entry.crc) {
        return fail(err, "ZIP member '" + entry.name + "' failed its CRC check");
    }
    return true;
}

}  // namespace

bool zipRead(std::string_view input, std::vector<TarEntry>& entries,
             std::string& err) {
    entries.clear();
    if (input.size() < 22) return fail(err, "truncated ZIP archive");
    // The EOCD is the last 22 bytes plus an optional comment of at most 65535
    // bytes. Search backwards for its signature.
    const std::size_t earliest = input.size() > 22 + 65535
                                     ? input.size() - 22 - 65535
                                     : 0;
    std::size_t eocd = std::string_view::npos;
    for (std::size_t at = input.size() - 22;; --at) {
        if (readU32(input, at) == kEndOfCentralDirectory) {
            eocd = at;
            break;
        }
        if (at == earliest) break;
    }
    if (eocd == std::string_view::npos) {
        return fail(err, "ZIP end-of-central-directory record not found");
    }
    if (eocd + 22 > input.size()) return fail(err, "truncated ZIP EOCD");
    const std::uint16_t count = readU16(input, eocd + 10);
    const std::uint32_t centralSize = readU32(input, eocd + 12);
    const std::uint32_t centralOffset = readU32(input, eocd + 16);
    if (count == 0xFFFFu || centralOffset == 0xFFFFFFFFu) {
        return fail(err, "ZIP64 archives are not supported");
    }
    std::vector<CentralEntry> directory;
    if (!readCentralDirectory(input, centralOffset, centralSize, count,
                              directory, err)) {
        return false;
    }
    entries.reserve(directory.size());
    for (const CentralEntry& entry : directory) {
        if (!entry.name.empty() && entry.name.back() == '/') continue;
        if ((entry.flags & 0x0001u) != 0) {
            return fail(err, "encrypted ZIP member is not supported: " +
                                 entry.name);
        }
        if (isSymlink(entry)) {
            return fail(err, "ZIP symlink member is not supported: " +
                                 entry.name);
        }
        if (entry.uncompressed > kMaxMemberBytes) {
            return fail(err, "ZIP member is too large: " + entry.name);
        }
        std::string name;
        if (!safeMemberName(entry.name, name, err)) return false;
        TarEntry file;
        file.name = std::move(name);
        if (!readMember(input, entry, file.data, err)) return false;
        entries.push_back(std::move(file));
    }
    return true;
}

}  // namespace cp3::cli
