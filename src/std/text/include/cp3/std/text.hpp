// =============================================================================
// cp3/std/text.hpp - `std/text` `TextEncoder` / `TextDecoder` backing surface.
//
// OWNERSHIP: the std/text family (src/std/text/**). Additive to the
// generated-code ABI (D14): everything is `inline`, header-only, and mirrors
// the STDLIB docs `TextEncoder` / `TextDecoder` surface one-to-one.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   import { TextEncoder, TextDecoder } from "std/text";
//   new TextEncoder()              -> ::cp3::runtime::text::TextEncoder()
//   new TextEncoder(encoding)      -> ::cp3::runtime::text::TextEncoder(enc)
//   new TextDecoder()              -> ::cp3::runtime::text::TextDecoder()
//   new TextDecoder(encoding)      -> ::cp3::runtime::text::TextDecoder(enc)
//   enc.encode(text)               -> (enc).encode(text)   (Uint8Array)
//   dec.decode(bytes)              -> (dec).decode(bytes)  (string)
//   enc.encoding / dec.encoding    -> (enc).encoding()     ("utf-8")
//
// The C++ spellings are plain value classes in namespace
// `cp3::runtime::text` (the std/date value-type pattern, not a `Ref` handle):
// both are stateless apart from the validated encoding label.
//
// -----------------------------------------------------------------------------
// SEMANTICS (pinned by tests/cases/stdlib/02/text)
// -----------------------------------------------------------------------------
//   * Only UTF-8 exists. The encoding label is normalized (ASCII whitespace
//     trimmed, lowercased) and `"utf-8"` / `"utf8"` are accepted; any other
//     label is a catchable cp3 error from the constructor, never a silent
//     fallback to UTF-8. A string-literal label outside the set is rejected
//     at compile time by sema (CP3401, the std/buffer D47 pattern); a label
//     known only at runtime is validated here instead.
//   * A cp3 string is already a UTF-8 byte string, so `encode` copies the
//     bytes verbatim into a fresh `Uint8Array`: no transcoding, no
//     normalization, no BOM is added.
//   * `decode` hand-validates the byte stream (no `simdutf` dependency; the
//     roadmap's hand-written fallback is the shipped path) and replaces each
//     invalid maximal subpart with U+FFFD (the WHATWG/Unicode substitution
//     rule), including a truncated sequence at the end of input and an
//     encoded surrogate or an out-of-range code point. A leading UTF-8 BOM
//     is stripped, matching `TextDecoder`'s default.
//   * `decode` is total: it never throws on malformed bytes. The only
//     catchable error this module raises is the unsupported-encoding error.
//
// The `Uint8Array` is the JS-compat family type
// `::cp3::runtime::TypedArray<::cp3::runtime::Uint8Tag>` (cp3/typed_array.hpp,
// src/std/typed-arrays/sema.cpp). The native row bundles that include tree
// alongside this one, exactly as std/fs bundles std/buffer for its `Buffer`
// chunk (src/std/fs/native.hpp).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "cp3/core/error.hpp"
#include "cp3/typed_array.hpp"

namespace cp3::runtime::text {

/// The cp3 `Uint8Array` value type (the JS-compat typed-array family).
using Uint8Array = ::cp3::runtime::TypedArray<::cp3::runtime::Uint8Tag>;

namespace detail {

/// A catchable cp3 error value (docs/language-spec/07): the generated
/// `catch (e)` binds the boxed message this carries.
[[noreturn]] inline void fail(const std::string& message) {
    throw Thrown::make(message);
}

/// The one supported encoding, as the `encoding` property reports it.
inline constexpr const char* kEncoding = "utf-8";

/// Normalize an encoding label (trim ASCII whitespace, lowercase) and return
/// the canonical `"utf-8"`, or throw a catchable cp3 error. `what` names the
/// constructor for the message (`TextEncoder` / `TextDecoder`).
inline std::string checkedEncoding(const std::string& encoding,
                                   const char* what) {
    std::string label;
    label.reserve(encoding.size());
    for (char c : encoding) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c == '\v') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        label.push_back(c);
    }
    if (label == "utf-8" || label == "utf8") return kEncoding;
    fail(std::string(what) + ": unsupported encoding \"" + encoding +
         "\" (only \"utf-8\")");
}

/// The UTF-8 bytes of U+FFFD, the replacement for each invalid maximal
/// subpart.
inline constexpr char kReplacement[] = "\xEF\xBF\xBD";

/// Hand-written UTF-8 validation + decode: valid sequences pass through
/// verbatim (a cp3 string is already UTF-8, so no transcoding is needed) and
/// each invalid maximal subpart is replaced by U+FFFD. The second-byte ranges
/// exclude overlong forms (E0/F0), surrogates (ED) and code points above
/// U+10FFFF (F4); a leading BOM is stripped.
inline std::string decodeUtf8(const std::uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve(size);
    std::size_t i = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        i = 3;
    }
    while (i < size) {
        const std::uint8_t lead = data[i];
        if (lead < 0x80) {
            out.push_back(static_cast<char>(lead));
            ++i;
            continue;
        }
        std::size_t length = 0;
        std::uint8_t lo = 0x80;
        std::uint8_t hi = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead == 0xE0) {
            length = 3;
            lo = 0xA0;
        } else if (lead >= 0xE1 && lead <= 0xEC) {
            length = 3;
        } else if (lead == 0xED) {
            length = 3;
            hi = 0x9F;
        } else if (lead == 0xEE || lead == 0xEF) {
            length = 3;
        } else if (lead == 0xF0) {
            length = 4;
            lo = 0x90;
        } else if (lead >= 0xF1 && lead <= 0xF3) {
            length = 4;
        } else if (lead == 0xF4) {
            length = 4;
            hi = 0x8F;
        } else {
            // 0x80..0xC1 and 0xF5..0xFF can never start a sequence.
            out += kReplacement;
            ++i;
            continue;
        }
        // Validate the continuation bytes; `consumed` counts the maximal
        // subpart the error covers (the lead byte plus the well-formed
        // continuations before the failure, or the whole truncated tail).
        std::size_t consumed = 1;
        bool valid = true;
        for (std::size_t k = 1; k < length; ++k) {
            if (i + k >= size) {
                valid = false;
                break;
            }
            const std::uint8_t byte = data[i + k];
            const std::uint8_t byteLo = k == 1 ? lo : 0x80;
            const std::uint8_t byteHi = k == 1 ? hi : 0xBF;
            if (byte < byteLo || byte > byteHi) {
                valid = false;
                break;
            }
            ++consumed;
        }
        if (valid) {
            out.append(reinterpret_cast<const char*>(data + i), length);
            i += length;
        } else {
            out += kReplacement;
            i += consumed;
        }
    }
    return out;
}

}  // namespace detail

// =============================================================================
// TextEncoder
// =============================================================================
/// `new TextEncoder()` / `new TextEncoder(encoding)`. Stateless apart from
/// the validated encoding label; `encode` copies the cp3 string's UTF-8
/// bytes into a fresh `Uint8Array`.
class TextEncoder {
public:
    explicit TextEncoder(std::string encoding = "utf-8")
        : encoding_(detail::checkedEncoding(encoding, "TextEncoder")) {}

    /// The UTF-8 bytes of `text` as a fresh `Uint8Array` (the byte view is
    /// exactly the string's bytes, so `decode(encode(s)) == s`).
    Uint8Array encode(const std::string& text) const {
        Uint8Array out = ::cp3::runtime::makeTypedArray<
            ::cp3::runtime::Uint8Tag>(static_cast<double>(text.size()));
        if (!text.empty()) {
            std::memcpy(out->buffer->data(), text.data(), text.size());
        }
        return out;
    }

    /// The canonical encoding label: always `"utf-8"`.
    std::string encoding() const { return encoding_; }

private:
    std::string encoding_;
};

// =============================================================================
// TextDecoder
// =============================================================================
/// `new TextDecoder()` / `new TextDecoder(encoding)`. `decode` hand-validates
/// the bytes as UTF-8, replacing each invalid maximal subpart with U+FFFD.
class TextDecoder {
public:
    explicit TextDecoder(std::string encoding = "utf-8")
        : encoding_(detail::checkedEncoding(encoding, "TextDecoder")) {}

    /// The text in `bytes`, decoded as UTF-8 with U+FFFD replacement (a
    /// leading BOM is stripped). Never throws on malformed bytes.
    std::string decode(const Uint8Array& bytes) const {
        if (bytes->length == 0) return std::string();
        const std::uint8_t* data =
            bytes->buffer->data() + bytes->byteOffset;
        return detail::decodeUtf8(data, bytes->length);
    }

    /// The canonical encoding label: always `"utf-8"`.
    std::string encoding() const { return encoding_; }

private:
    std::string encoding_;
};

}  // namespace cp3::runtime::text
