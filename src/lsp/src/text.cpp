// =============================================================================
// src/lsp/src/text.cpp - URI/line/position conversion from detail/lsp_text.hpp.
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements detail/lsp_text.hpp.
//
// All conversions are total: malformed input degrades to a clamped position or
// an empty line, never to a crash (the client controls every string here).
// =============================================================================
#include "detail/lsp_text.hpp"

#include <cstddef>

namespace cp3::lsp {
namespace {

bool startsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decoding; a malformed escape keeps its '%' literally (best effort).
std::string percentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int high = hexValue(text[i + 1]);
            const int low = hexValue(text[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

bool isDriveLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Byte offsets of a 1-based line, without its EOL. False when the line does
// not exist in the document.
bool lineBounds(const std::string& text, std::uint32_t line,
                std::size_t& begin, std::size_t& end) {
    if (line == 0) return false;
    begin = 0;
    for (std::uint32_t current = 1; current < line; ++current) {
        const std::size_t newline = text.find('\n', begin);
        if (newline == std::string::npos) return false;
        begin = newline + 1;
    }
    end = text.find('\n', begin);
    if (end == std::string::npos) end = text.size();
    while (end > begin && text[end - 1] == '\r') --end;
    return true;
}

std::uint32_t lineCount(const std::string& text) {
    std::uint32_t count = 1;
    for (const char c : text) {
        if (c == '\n') ++count;
    }
    return count;
}

// UTF-16 code units in a UTF-8 byte slice. A malformed sequence counts one
// unit per byte so the result never runs backwards.
std::uint32_t utf16UnitsIn(std::string_view bytes) {
    std::uint32_t units = 0;
    std::size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        std::uint32_t width = 1;
        std::uint32_t cp = lead;
        if (lead >= 0xF0) {
            width = 4;
            cp = lead & 0x07u;
        } else if (lead >= 0xE0) {
            width = 3;
            cp = lead & 0x0Fu;
        } else if (lead >= 0xC0) {
            width = 2;
            cp = lead & 0x1Fu;
        }
        const bool goodLead = lead >= 0xC2 && lead <= 0xF4;  // no overlongs
        bool valid = width > 1 && goodLead && i + width <= bytes.size();
        for (std::uint32_t k = 1; valid && k < width; ++k) {
            const unsigned char next = static_cast<unsigned char>(bytes[i + k]);
            if ((next & 0xC0u) != 0x80u) valid = false;
            else cp = (cp << 6) | (next & 0x3Fu);
        }
        units += (valid && cp > 0xFFFF) ? 2 : 1;
        i += valid ? width : 1;
    }
    return units;
}

}  // namespace

bool uriToPath(const std::string& uri, std::string& path) {
    constexpr std::string_view kScheme = "file://";
    if (!startsWith(uri, kScheme)) return false;
    const std::size_t afterScheme = kScheme.size();
    const std::size_t slash = uri.find('/', afterScheme);
    const std::string authority = slash == std::string::npos
                                      ? uri.substr(afterScheme)
                                      : uri.substr(afterScheme, slash - afterScheme);
    const std::string tail =
        slash == std::string::npos ? std::string() : uri.substr(slash);
    std::string decoded = percentDecode(tail);
    if (!authority.empty()) {
        // "file://host/share" is a UNC path on Windows.
        path = "//" + authority + decoded;
        return true;
    }
    std::size_t start = 0;
    if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':' &&
        isDriveLetter(decoded[1])) {
        start = 1;
    }
    path = decoded.substr(start);
    return !path.empty();
}

std::string_view lineOf(const std::string& text, std::uint32_t line) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!lineBounds(text, line, begin, end)) return {};
    return std::string_view(text).substr(begin, end - begin);
}

LspPosition toLspPosition(const std::string& text, std::uint32_t line,
                          std::uint32_t column) {
    LspPosition pos;
    std::uint32_t wanted = line == 0 ? 1 : line;
    const std::uint32_t last = lineCount(text);
    if (wanted > last) wanted = last;
    pos.line = wanted - 1;

    std::size_t begin = 0;
    std::size_t end = 0;
    if (!lineBounds(text, wanted, begin, end)) return pos;
    const std::size_t width = end - begin;
    std::size_t byteIndex = column > 0 ? static_cast<std::size_t>(column - 1) : 0;
    if (byteIndex > width) byteIndex = width;
    pos.character = utf16UnitsIn(std::string_view(text).substr(begin, byteIndex));
    return pos;
}

LspPosition advanceOne(const std::string& text, LspPosition start) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!lineBounds(text, start.line + 1, begin, end)) return start;
    const std::string_view line = std::string_view(text).substr(begin, end - begin);
    LspPosition out = start;
    if (out.character < utf16UnitsIn(line)) ++out.character;
    return out;
}

}  // namespace cp3::lsp
