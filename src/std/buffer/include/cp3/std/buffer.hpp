// =============================================================================
// cp3/std/buffer.hpp - `std/buffer` `Buffer` backing surface.
//
// OWNERSHIP: the std/buffer family (src/std/buffer/**). Additive to the
// generated-code ABI (D14): everything is `inline`, header-only, and mirrors
// the STDLIB docs §04 buffer class one-to-one.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 section buffer)
// -----------------------------------------------------------------------------
//   Buffer.from(input)             -> ::cp3::runtime::buffer::from(input)
//   Buffer.from(input, encoding)   -> ::cp3::runtime::buffer::from(input, enc)
//   Buffer.alloc(size)             -> ::cp3::runtime::buffer::alloc(size)
//   buf.toString()                 -> ::cp3::runtime::buffer::toString(buf)
//   buf.toString(encoding)         -> ::cp3::runtime::buffer::toString(buf, enc)
//   buf.slice(start)               -> ::cp3::runtime::buffer::slice(buf, start)
//   buf.slice(start, end)          -> ::cp3::runtime::buffer::slice(buf, a, b)
//   buf.length                     -> ::cp3::runtime::buffer::size(buf)
// (the encoding argument is always `std::string`). The C++ spelling of the cp3
// `Buffer` type itself is `::cp3::runtime::buffer::Buffer` (codegen classify).
//
// -----------------------------------------------------------------------------
// SEMANTICS (pinned by tests/cases/stdlib/buffer_*)
// -----------------------------------------------------------------------------
//   * A Buffer is an immutable byte sequence with value semantics: `slice`
//     copies (the documented surface has no writes, so there is no aliasing to
//     preserve) and `==`/`!=` compare the bytes.
//   * Encodings (D47): "utf8" (default; a cp3 string is already a UTF-8 byte
//     string), "base64" (RFC 4648 with padding) and "hex" (lowercase on
//     encode; decode accepts either case). A literal in any other encoding is
//     rejected at compile time by sema (CP3401); when the encoding is not a
//     literal this header still validates it and throws a catchable cp3 error
//     instead of decoding wrongly.
//   * `alloc(size)` is zero-filled; `size` must be a non-negative integer (a
//     negative, fractional or absurd size is a catchable cp3 error, never a
//     std::bad_alloc that no cp3 `catch` could take).
//   * `slice(start, end)` follows JS `Buffer.prototype.slice`: fractional
//     indices truncate, negative indices count from the end, out-of-range
//     indices clamp, and an omitted `end` is the current length.
//   * `console.log(buf)` renders Node's `<Buffer 68 65 6c 6c 6f>` form
//     (`inspect`), and a Buffer is always truthy (it is an object; the
//     generated prelude provides the `cp3_ts`/`cp3_truthy` overloads).
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include "cp3/core/error.hpp"

namespace cp3::runtime::buffer {

namespace detail {

// Node's cap for `alloc`: keeps an absurd size a catchable cp3 error instead
// of a std::bad_alloc (the same cap crypto.randomBytes uses).
inline constexpr double kMaxSize = 2147483647.0;  // 2^31 - 1

// A catchable cp3 error value (docs/language-spec/07): the generated
// `catch (e)` binds the boxed message this carries.
[[noreturn]] inline void fail(const std::string& message) {
    throw Thrown::make(message);
}

inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::string hexEncode(const std::string& bytes) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (char c : bytes) {
        const unsigned char b = static_cast<unsigned char>(c);
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0f]);
    }
    return out;
}

inline std::string hexDecode(const std::string& text) {
    if (text.size() % 2 != 0) {
        fail("Buffer.from: hex input must have an even number of digits");
    }
    std::string out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = hexValue(text[i]);
        const int lo = hexValue(text[i + 1]);
        if (hi < 0 || lo < 0) {
            fail("Buffer.from: invalid hex digit in \"" + text + "\"");
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

inline std::string base64Encode(const std::string& bytes) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const unsigned triple =
            (static_cast<unsigned char>(bytes[i]) << 16) |
            (static_cast<unsigned char>(bytes[i + 1]) << 8) |
            static_cast<unsigned char>(bytes[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3f]);
        out.push_back(kAlphabet[triple & 0x3f]);
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1) {
        const unsigned triple = static_cast<unsigned char>(bytes[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (rest == 2) {
        const unsigned triple =
            (static_cast<unsigned char>(bytes[i]) << 16) |
            (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

inline int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Whitespace is ignored (PEM-style wrapped input); `=` is accepted only as the
// trailing padding, and the final group must not leave a dangling 6-bit unit.
inline std::string base64Decode(const std::string& text) {
    std::string out;
    out.reserve(text.size() / 4 * 3 + 3);
    unsigned acc = 0;
    int bits = 0;
    int padding = 0;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding != 0) {
            fail("Buffer.from: \"=\" is only allowed at the end of base64 input");
        }
        const int value = base64Value(c);
        if (value < 0) fail("Buffer.from: invalid base64 input");
        acc = (acc << 6) | static_cast<unsigned>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xffu));
        }
    }
    if (bits >= 6 || padding > 2) fail("Buffer.from: invalid base64 input");
    return out;
}

// The `start`/`end` pair of `slice` as absolute [from, to) bounds (JS
// `Buffer.prototype.slice`: truncate, count negatives from the end, clamp).
inline std::size_t sliceBegin(double start, double len) {
    double from = std::isnan(start) ? 0.0 : std::trunc(start);
    if (from < 0.0) from = std::max(0.0, len + from);
    if (from > len) from = len;
    return static_cast<std::size_t>(from);
}

inline std::size_t sliceEnd(double end, double len) {
    double to = std::isnan(end) ? len : std::trunc(end);
    if (to < 0.0) to = std::max(0.0, len + to);
    if (to > len) to = len;
    return static_cast<std::size_t>(to);
}

}  // namespace detail

// The nominal `Buffer` byte sequence. Immutable: the factory functions below
// are the only writers, and codegen never spells the constructor itself.
class Buffer {
public:
    Buffer() = default;
    explicit Buffer(std::string bytes) : bytes_(std::move(bytes)) {}

    const std::string& bytes() const { return bytes_; }
    double size() const { return static_cast<double>(bytes_.size()); }

private:
    std::string bytes_;
};

/// `Buffer.alloc(size)`: a zero-filled buffer of `size` bytes.
inline Buffer alloc(double size) {
    if (!(size >= 0.0) || size != std::floor(size) || size > detail::kMaxSize) {
        detail::fail("Buffer.alloc: size must be a non-negative integer");
    }
    return Buffer(std::string(static_cast<std::size_t>(size), '\0'));
}

/// `Buffer.from(input)`: the UTF-8 bytes of `input` (the default encoding).
inline Buffer from(const std::string& input) { return Buffer(input); }

/// `Buffer.from(input, encoding)`: decode `input` from `utf8`/`base64`/`hex`.
inline Buffer from(const std::string& input, const std::string& encoding) {
    if (encoding == "utf8") return Buffer(input);
    if (encoding == "base64") return Buffer(detail::base64Decode(input));
    if (encoding == "hex") return Buffer(detail::hexDecode(input));
    detail::fail("Buffer.from: unsupported encoding \"" + encoding +
                 "\" (only \"utf8\", \"base64\" and \"hex\")");
}

/// `buf.toString()`: the buffer's bytes as a UTF-8 string.
inline std::string toString(const Buffer& value) { return value.bytes(); }

/// `buf.toString(encoding)`: `utf8` (identity), `base64` or lowercase `hex`.
inline std::string toString(const Buffer& value, const std::string& encoding) {
    if (encoding == "utf8") return value.bytes();
    if (encoding == "base64") return detail::base64Encode(value.bytes());
    if (encoding == "hex") return detail::hexEncode(value.bytes());
    detail::fail("Buffer.toString: unsupported encoding \"" + encoding +
                 "\" (only \"utf8\", \"base64\" and \"hex\")");
}

/// `buf.slice(start, end)`: the bytes in [start, end) (JS semantics, a copy).
inline Buffer slice(const Buffer& value, double start, double end) {
    const std::string& data = value.bytes();
    const double len = static_cast<double>(data.size());
    const std::size_t from = detail::sliceBegin(start, len);
    const std::size_t to = detail::sliceEnd(end, len);
    if (to <= from) return Buffer(std::string());
    return Buffer(data.substr(from, to - from));
}

/// `buf.slice(start)`: the bytes from `start` to the end (a copy).
inline Buffer slice(const Buffer& value, double start) {
    return slice(value, start, static_cast<double>(value.bytes().size()));
}

/// `buf.length`: the byte count (codegen casts this to the cp3 `number`).
inline double size(const Buffer& value) { return value.size(); }

/// The raw bytes, for the std families that consume a Buffer (crypto digests).
inline const std::string& bytes(const Buffer& value) { return value.bytes(); }

/// `console.log(buf)`: Node's `<Buffer 68 65 6c 6c 6f>` rendering.
inline std::string inspect(const Buffer& value) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out = "<Buffer ";
    for (std::size_t i = 0; i < value.bytes().size(); ++i) {
        if (i != 0) out += " ";
        const unsigned char b = static_cast<unsigned char>(value.bytes()[i]);
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0f]);
    }
    out += ">";
    return out;
}

/// Byte-content equality (the value semantics of an immutable byte sequence).
inline bool operator==(const Buffer& a, const Buffer& b) {
    return a.bytes() == b.bytes();
}

inline bool operator!=(const Buffer& a, const Buffer& b) { return !(a == b); }

}  // namespace cp3::runtime::buffer
