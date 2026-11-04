// =============================================================================
// cp3/std/url.hpp - `std/url` backing: URI encoding and decoding.
//
// OWNERSHIP: the std/url family (src/std/url/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only, and mirror the
// STDLIB docs §04 URL signature table one-to-one.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `encodeURIComponent(s)` -> `::cp3::runtime::url::encodeURIComponent(s)`
//   `decodeURIComponent(s)` -> `::cp3::runtime::url::decodeURIComponent(s)`
//   `encodeURI(s)`          -> `::cp3::runtime::url::encodeURI(s)`
//   `decodeURI(s)`          -> `::cp3::runtime::url::decodeURI(s)`
//
// -----------------------------------------------------------------------------
// SEMANTICS (RFC 3986)
// -----------------------------------------------------------------------------
//   * encodeURIComponent escapes everything except A-Z a-z 0-9 - _ . ! ~ * ' ( )
//     (the RFC 3986 unreserved marks plus the ECMAScript extra set).
//   * encodeURI escapes everything except A-Z a-z 0-9 - _ . ! ~ * ' ( ) ;
 //     / ? : @ & = + $ , # (the reserved delimiters stay).
//   * decodeURIComponent / decodeURI decode %HH sequences; a malformed
//     sequence is left as-is (JS throws URIError, but cp3's permissive stance
//     keeps URL construction total).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace cp3::runtime::url {

namespace detail {

inline bool isUnreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
           c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' ||
           c == ')';
}

inline bool isReserved(char c) {
    return isUnreserved(c) || c == ';' || c == '/' || c == '?' || c == ':' ||
           c == '@' || c == '&' || c == '=' || c == '+' || c == '$' ||
           c == ',' || c == '#';
}

inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::string encode(const std::string& input, bool component) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        const bool keep = component ? isUnreserved(ch) : isReserved(ch) || ch == '#';
        if (keep) {
            out.push_back(ch);
        } else {
            const unsigned char b = static_cast<unsigned char>(ch);
            out.push_back('%');
            out.push_back(hex[(b >> 4) & 0xF]);
            out.push_back(hex[b & 0xF]);
        }
    }
    return out;
}

inline std::string decode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            const int hi = hexValue(input[i + 1]);
            const int lo = hexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

}  // namespace detail

inline std::string encodeURIComponent(const std::string& input) {
    return detail::encode(input, true);
}

inline std::string decodeURIComponent(const std::string& input) {
    return detail::decode(input);
}

inline std::string encodeURI(const std::string& input) {
    return detail::encode(input, false);
}

inline std::string decodeURI(const std::string& input) {
    return detail::decode(input);
}

}  // namespace cp3::runtime::url

// Also surface the percentDecode helper the network runtime can adapt.
namespace cp3::runtime {
inline std::string percentDecode(const std::string& input) {
    return url::detail::decode(input);
}
}  // namespace cp3::runtime
