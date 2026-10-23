// =============================================================================
// src/cli/src/archive/tar.cpp - minimal POSIX ustar archive DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/tar.hpp.
//
// The writer emits plain ustar headers (long paths are split across the
// name/prefix fields). The reader understands regular files, skips directory
// and pax-extension headers, resolves GNU long names ('L' headers), and
// refuses unsafe paths so extraction can never escape the package directory.
// =============================================================================
#include "detail/archive/tar.hpp"

#include <cstdint>
#include <cstring>

namespace cp3::cli {

namespace {

constexpr std::size_t kBlock = 512;

// Field offsets in the ustar header.
constexpr std::size_t kNameOffset = 0;
constexpr std::size_t kNameSize = 100;
constexpr std::size_t kModeOffset = 100;
constexpr std::size_t kModeSize = 8;
constexpr std::size_t kSizeOffset = 124;
constexpr std::size_t kSizeSize = 12;
constexpr std::size_t kMtimeOffset = 136;
constexpr std::size_t kMtimeSize = 12;
constexpr std::size_t kChksumOffset = 148;
constexpr std::size_t kChksumSize = 8;
constexpr std::size_t kTypeOffset = 156;
constexpr std::size_t kMagicOffset = 257;
constexpr std::size_t kPrefixOffset = 345;
constexpr std::size_t kPrefixSize = 155;

bool fail(std::string& err, const std::string& message) {
    err = message;
    return false;
}

std::size_t paddedSize(std::size_t size) {
    return (size + kBlock - 1) / kBlock * kBlock;
}

// Parses a NUL/space-terminated octal field.
std::uint64_t readOctal(const char* field, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        const char c = field[i];
        if (c == '\0' || c == ' ') break;
        if (c < '0' || c > '7') return value;
        value = value * 8 + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

// Writes `value` as a zero-padded octal number into a fixed field.
void writeOctal(char* field, std::size_t width, std::uint64_t value) {
    std::memset(field, '0', width);
    field[width - 1] = '\0';
    std::size_t i = width - 1;
    while (i > 0 && value > 0) {
        field[--i] = static_cast<char>('0' + (value & 7u));
        value >>= 3;
    }
}

bool checksumMatches(const char* header) {
    const std::uint64_t stored =
        readOctal(header + kChksumOffset, kChksumSize);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) {
        const unsigned char c = static_cast<unsigned char>(header[i]);
        sum += (i >= kChksumOffset && i < kChksumOffset + kChksumSize)
                   ? static_cast<unsigned char>(' ')
                   : c;
    }
    return sum == stored;
}

// Splits `name` into the ustar name/prefix fields. Returns false when the
// name cannot be represented (a single component longer than 100 bytes).
bool splitName(const std::string& name, std::string& nameField,
               std::string& prefixField) {
    if (name.size() <= kNameSize) {
        nameField = name;
        prefixField.clear();
        return true;
    }
    // Split at a '/' such that the suffix (name field) fits in 100 bytes.
    std::size_t split = std::string::npos;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] != '/') continue;
        const std::size_t suffix = name.size() - i - 1;
        if (suffix <= kNameSize && i <= kPrefixSize) {
            split = i;
            break;
        }
    }
    if (split == std::string::npos) return false;
    prefixField = name.substr(0, split);
    nameField = name.substr(split + 1);
    return true;
}

void writeHeader(const TarEntry& entry, const std::string& name,
                 const std::string& prefix, char* header) {
    std::memset(header, 0, kBlock);
    std::memcpy(header + kNameOffset, name.data(),
                std::min(name.size(), kNameSize));
    writeOctal(header + kModeOffset, kModeSize, 0644);
    writeOctal(header + 108, 8, 0);  // uid
    writeOctal(header + 116, 8, 0);  // gid
    writeOctal(header + kSizeOffset, kSizeSize, entry.data.size());
    writeOctal(header + kMtimeOffset, kMtimeSize, 0);
    header[kTypeOffset] = '0';  // regular file
    std::memcpy(header + kMagicOffset, "ustar", 6);  // "ustar\0" (POSIX)
    std::memcpy(header + kMagicOffset + 6, "00", 2);
    std::memcpy(header + kPrefixOffset, prefix.data(),
                std::min(prefix.size(), kPrefixSize));
    std::memset(header + kChksumOffset, ' ', kChksumSize);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) {
        sum += static_cast<unsigned char>(header[i]);
    }
    writeOctal(header + kChksumOffset, 7, sum);  // 6 digits + NUL
    header[kChksumOffset + 7] = ' ';
}

bool pathIsSafe(const std::string& name) {
    if (name.empty()) return false;
    if (name[0] == '/' || name[0] == '\\') return false;
    if (name.size() >= 2 && name[1] == ':') return false;  // Windows drive
    if (name.find('\\') != std::string::npos) return false;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::string component =
            name.substr(start, slash == std::string::npos ? std::string::npos
                                                          : slash - start);
        if (component.empty() && slash != std::string::npos) {
            // "a//b" and leading "/" are rejected; trailing '/' marks a dir.
            if (start != 0) return false;
        }
        if (component == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

struct HeaderInfo {
    bool zeroBlock = false;
    char type = '0';
    std::uint64_t size = 0;
    std::string name;
};

bool parseHeader(const char* header, HeaderInfo& info, std::string& err) {
    bool allZero = true;
    for (std::size_t i = 0; i < kBlock && allZero; ++i) {
        allZero = header[i] == '\0';
    }
    if (allZero) {
        info.zeroBlock = true;
        return true;
    }
    if (!checksumMatches(header)) return fail(err, "tar header checksum mismatch");
    info.type = header[kTypeOffset] == '\0' ? '0' : header[kTypeOffset];
    info.size = readOctal(header + kSizeOffset, kSizeSize);
    std::string name(header + kNameOffset,
                     strnlen(header + kNameOffset, kNameSize));
    std::string prefix(header + kPrefixOffset,
                       strnlen(header + kPrefixOffset, kPrefixSize));
    if (!prefix.empty()) name = prefix + "/" + name;
    info.name = std::move(name);
    return true;
}

// Consumes `size` bytes (including padding) at `pos`, reporting the start.
bool consumeData(std::string_view input, std::size_t& pos, std::uint64_t size,
                 std::string_view& data, std::string& err) {
    if (size > input.size() || pos + size > input.size()) {
        return fail(err, "truncated tar entry data");
    }
    data = input.substr(pos, static_cast<std::size_t>(size));
    pos += paddedSize(static_cast<std::size_t>(size));
    if (pos > input.size()) return fail(err, "truncated tar entry padding");
    return true;
}

}  // namespace

bool tarWrite(const std::vector<TarEntry>& entries, std::string& out,
              std::string& err) {
    err.clear();
    out.clear();
    for (const TarEntry& entry : entries) {
        if (!pathIsSafe(entry.name)) {
            return fail(err, "refusing to archive unsafe path '" + entry.name +
                                 "'");
        }
        char header[kBlock];
        std::string nameField;
        std::string prefixField;
        if (!splitName(entry.name, nameField, prefixField)) {
            return fail(err, "tar path too long to represent: '" + entry.name +
                                 "'");
        }
        writeHeader(entry, nameField, prefixField, header);
        out.append(header, kBlock);
        out += entry.data;
        out.append(paddedSize(entry.data.size()) - entry.data.size(), '\0');
    }
    out.append(2 * kBlock, '\0');
    return true;
}

bool tarRead(std::string_view input, std::vector<TarEntry>& entries,
             std::string& err) {
    err.clear();
    entries.clear();
    std::size_t pos = 0;
    std::string longName;
    while (pos + kBlock <= input.size()) {
        HeaderInfo info;
        if (!parseHeader(input.data() + pos, info, err)) return false;
        pos += kBlock;
        if (info.zeroBlock) break;

        std::string_view data;
        if (!consumeData(input, pos, info.size, data, err)) return false;
        if (info.type == '5') continue;  // directory entry
        if (info.type == 'L') {          // GNU long name for the next entry
            longName.assign(data.data(), data.size());
            continue;
        }
        if (info.type == 'x' || info.type == 'g' || info.type == 'K') {
            continue;  // pax/long-linkname extensions: nothing to apply here
        }
        if (info.type != '0' && info.type != '\0') continue;

        TarEntry entry;
        entry.name = longName.empty() ? info.name : longName;
        longName.clear();
        if (!pathIsSafe(entry.name)) {
            return fail(err, "unsafe path in archive: '" + entry.name + "'");
        }
        entry.data.assign(data.data(), data.size());
        entries.push_back(std::move(entry));
    }
    return true;
}

}  // namespace cp3::cli
