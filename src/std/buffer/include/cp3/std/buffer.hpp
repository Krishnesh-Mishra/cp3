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
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/core/value.hpp"

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

// Buffer payloads in copy-heavy programs are frequently destroyed at the end
// of one iteration and replaced with another payload of the same size. Cache
// freed byte blocks per thread so those cycles reuse their backing allocation.
// Deallocation may happen on a different thread than allocation; each thread
// simply adopts blocks it frees, so no block is shared between unsynchronized
// caches.
class ByteBlockPool {
    struct Block {
        void* data;
        std::size_t size;
    };
    static constexpr std::size_t kMaxRetainedBytes = 2 * 1024 * 1024;

    std::vector<Block> free_;
    std::size_t retained_ = 0;

public:
    ByteBlockPool() = default;
    ByteBlockPool(const ByteBlockPool&) = delete;
    ByteBlockPool& operator=(const ByteBlockPool&) = delete;

    ~ByteBlockPool() {
        for (const Block& block : free_) ::operator delete(block.data);
    }

    void* allocate(std::size_t size) {
        for (std::size_t i = free_.size(); i != 0; --i) {
            const Block block = free_[i - 1];
            if (block.size != size) continue;
            free_[i - 1] = free_.back();
            free_.pop_back();
            retained_ -= block.size;
            return block.data;
        }
        return ::operator new(size == 0 ? 1 : size);
    }

    void deallocate(void* data, std::size_t size) noexcept {
        if (!data) return;
        const std::size_t actualSize = size == 0 ? 1 : size;
        if (actualSize <= kMaxRetainedBytes - retained_) {
            try {
                free_.push_back({data, actualSize});
                retained_ += actualSize;
                return;
            } catch (...) {
                // Allocation failure while caching must not escape a string
                // destructor. Release the block normally instead.
            }
        }
        ::operator delete(data);
    }
};

inline ByteBlockPool& byteBlockPool() {
    thread_local ByteBlockPool pool;
    return pool;
}

template <class T>
class ByteBlockAllocator {
public:
    using value_type = T;
    using is_always_equal = std::true_type;
    template <class U> struct rebind { using other = ByteBlockAllocator<U>; };

    ByteBlockAllocator() noexcept = default;
    template <class U>
    ByteBlockAllocator(const ByteBlockAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        return static_cast<T*>(byteBlockPool().allocate(count * sizeof(T)));
    }

    void deallocate(T* data, std::size_t count) noexcept {
        byteBlockPool().deallocate(data, count * sizeof(T));
    }

    template <class U>
    bool operator==(const ByteBlockAllocator<U>&) const noexcept { return true; }
    template <class U>
    bool operator!=(const ByteBlockAllocator<U>&) const noexcept { return false; }
};

using BufferString = std::basic_string<char, std::char_traits<char>,
                                       ByteBlockAllocator<char>>;

}  // namespace detail

// The `atob` / `btoa` globals: base64 text <-> byte text. `atob` rejects
// malformed input with a catchable cp3 error (the decoder's message names
// Buffer.from; rethrow it under atob's name).
inline std::string atob(const std::string& text) {
    try {
        return detail::base64Decode(text);
    } catch (const Thrown&) {
        detail::fail("atob: invalid base64 input");
    }
}
inline std::string btoa(const std::string& bytes) {
    return detail::base64Encode(bytes);
}

// The nominal `Buffer` byte sequence. Immutable: the factory functions below
// are the only writers, and codegen never spells the constructor itself.
//
// A Buffer is a shared immutable owner plus an [offset, length) view. `from`
// owns one string and `slice` views the same owner, so the documented value
// semantics (immutability, byte equality, `toString` copying) are unchanged
// while a slice no longer copies its bytes.
class Buffer {
public:
    Buffer() = default;
    explicit Buffer(std::string bytes)
        : Buffer(detail::BufferString(bytes.begin(), bytes.end())) {}
    explicit Buffer(detail::BufferString bytes)
        : owner_(std::allocate_shared<const detail::BufferString>(
              ::cp3::runtime::RefPoolAllocator<const detail::BufferString>{},
              std::move(bytes))),
          length_(owner_->size()) {}

    std::string_view view() const noexcept {
        if (!owner_ || length_ == 0) return std::string_view();
        return std::string_view(owner_->data() + offset_, length_);
    }

    // A copy of the bytes. Kept for callers that need an owning std::string;
    // the crypto boundary uses `view` and never materializes one.
    std::string bytes() const { return std::string(view()); }
    double size() const { return static_cast<double>(length_); }

private:
    friend Buffer slice(const Buffer&, double, double);
    Buffer(std::shared_ptr<const detail::BufferString> owner, std::size_t offset,
           std::size_t length)
        : owner_(std::move(owner)), offset_(offset), length_(length) {}

    std::shared_ptr<const detail::BufferString> owner_;
    std::size_t offset_ = 0;
    std::size_t length_ = 0;
};

/// `Buffer.alloc(size)`: a zero-filled buffer of `size` bytes.
inline Buffer alloc(double size) {
    if (!(size >= 0.0) || size != std::floor(size) || size > detail::kMaxSize) {
        detail::fail("Buffer.alloc: size must be a non-negative integer");
    }
    return Buffer(detail::BufferString(static_cast<std::size_t>(size), '\0'));
}

/// `Buffer.from(input)`: the UTF-8 bytes of `input` (the default encoding).
inline Buffer from(const std::string& input) { return Buffer(input); }

/// The rvalue overload moves the bytes into the shared owner. A temporary
/// string from a fused concatenation therefore reaches the Buffer without an
/// extra copy.
inline Buffer from(std::string&& input) { return Buffer(std::move(input)); }

/// `Buffer.from(input, encoding)`: decode `input` from `utf8`/`base64`/`hex`.
inline Buffer from(const std::string& input, const std::string& encoding) {
    if (encoding == "utf8") return from(input);
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

namespace detail {

inline std::size_t concatSize(const Buffer& value) {
    return value.view().size();
}
inline std::size_t concatSize(const std::string& value) { return value.size(); }
inline std::size_t concatSize(const char* value) {
    return value ? std::strlen(value) : 0;
}

inline std::size_t concatSizeChecked(std::size_t total, std::size_t part,
                                    std::size_t maxSize) {
    if (part > maxSize - total) {
        throw std::length_error("Buffer concatenation is too large");
    }
    return total + part;
}

inline void concatCopy(BufferString& out, std::size_t& offset,
                       const Buffer& value) {
    const std::string_view bytes = value.view();
    if (!bytes.empty()) std::memcpy(out.data() + offset, bytes.data(), bytes.size());
    offset += bytes.size();
}
inline void concatCopy(BufferString& out, std::size_t& offset,
                       const std::string& value) {
    if (!value.empty()) std::memcpy(out.data() + offset, value.data(), value.size());
    offset += value.size();
}
inline void concatCopy(BufferString& out, std::size_t& offset,
                       const char* value) {
    const std::size_t size = value ? std::strlen(value) : 0;
    if (size != 0) std::memcpy(out.data() + offset, value, size);
    offset += size;
}

}  // namespace detail

/// Build one Buffer from string/Buffer parts without a `toString` round trip.
/// `Buffer.from(a + b.toString() + c.toString())` lowers here; the result is
/// byte-identical to concatenating the strings and calling `from`.
template <class... Parts>
Buffer fromConcat(const Parts&... parts) {
    detail::BufferString out;
    std::size_t size = 0;
    ((size = detail::concatSizeChecked(size, detail::concatSize(parts),
                                       out.max_size())), ...);
    out.resize(size);
    std::size_t offset = 0;
    (detail::concatCopy(out, offset, parts), ...);
    return Buffer(std::move(out));
}

/// `buf.slice(start, end)`: the bytes in [start, end) (JS semantics). The
/// result is an immutable view of the same owner, not an observable alias:
/// there is no write surface, and `toString` still copies.
inline Buffer slice(const Buffer& value, double start, double end) {
    const double len = value.size();
    const std::size_t from = detail::sliceBegin(start, len);
    const std::size_t to = detail::sliceEnd(end, len);
    if (to <= from) return Buffer();
    return Buffer(value.owner_, value.offset_ + from, to - from);
}

/// `buf.slice(start)`: the bytes from `start` to the end.
inline Buffer slice(const Buffer& value, double start) {
    return slice(value, start, value.size());
}

/// `buf.length`: the byte count (codegen casts this to the cp3 `number`).
inline double size(const Buffer& value) { return value.size(); }

/// The bytes as a non-owning view, for std families that consume a Buffer
/// without needing an owning copy (crypto digests).
inline std::string_view view(const Buffer& value) { return value.view(); }

/// The bytes as an owning copy, for callers that need a `std::string`.
inline std::string bytes(const Buffer& value) { return value.bytes(); }

/// `console.log(buf)`: Node's `<Buffer 68 65 6c 6c 6f>` rendering.
inline std::string inspect(const Buffer& value) {
    static const char kDigits[] = "0123456789abcdef";
    const std::string_view data = value.view();
    std::string out = "<Buffer ";
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i != 0) out += " ";
        const unsigned char b = static_cast<unsigned char>(data[i]);
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0f]);
    }
    out += ">";
    return out;
}

/// Byte-content equality (the value semantics of an immutable byte sequence).
inline bool operator==(const Buffer& a, const Buffer& b) {
    return a.view() == b.view();
}

inline bool operator!=(const Buffer& a, const Buffer& b) { return !(a == b); }

}  // namespace cp3::runtime::buffer
