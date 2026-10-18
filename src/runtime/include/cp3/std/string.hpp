// =============================================================================
// cp3/std/string.hpp - primitive `string` method backing (spec §09 surface).
//
// OWNERSHIP: runtime module. Additive to the generated-code ABI (D14): all
// functions are `inline`, header-only.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `s.<method>(args)` on a `string` receiver lowers to
//   `::cp3::runtime::strings::<method>(s, args)`. `s.length` lowers to
//   `static_cast<double>(s.size())` (no runtime call).
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * `.length` is the UTF-8 byte count (D63; str_length_utf8 pins "😀" == 4),
//     and every index-taking method here counts *codepoints*, matching
//     charAt("😀", 0) returning the whole 4-byte emoji (D63 corpus evidence).
//   * `charAt(i)` past the end returns "" (JS behavior; the out-of-range
//     question is parked - this is the defensive JS-faithful reading).
//   * `repeat` clamps a negative count to 0 (JS throws RangeError; no case
//     pins the throw yet).
//   * `trim`/`trimStart`/`trimEnd` strip the ASCII whitespace set; the full
//     Unicode whitespace table is not needed by any case.
//   * `toUpperCase`/`toLowerCase` are ASCII-only: the corpus is ASCII and a
//     full Unicode case mapping needs ICU (a non-starter for a std-only
//     runtime).
//   * `slice`/`substring` follow JS argument normalization on byte indices
//     (consistent with `.length` bytes): slice counts negatives from the end,
//     substring clamps at 0 and swaps start/end when start > end.
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace cp3::runtime::strings {

namespace detail {

// True for a UTF-8 continuation byte (0b10xxxxxx): every other byte starts a
// codepoint. Input is assumed well-formed.
inline bool isCodePointStart(unsigned char c) {
    return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u;
}

inline bool isTrimSpace(unsigned char c) {
    switch (c) {
        case ' ': case '\t': case '\n': case '\v': case '\f': case '\r':
            return true;
        default:
            return false;
    }
}

inline double normalizeIndex(double index, double length, double fallback) {
    if (std::isnan(index)) return fallback;
    index = std::trunc(index);
    if (index < 0.0) {
        index += length;
        if (index < 0.0) return 0.0;
    }
    if (index > length) return length;
    return index;
}

}  // namespace detail

// --- indexing ----------------------------------------------------------------

inline std::string charAt(const std::string& s, double index) {
    if (!(index >= 0.0)) return std::string();
    const std::size_t want = static_cast<std::size_t>(std::trunc(index));
    std::size_t at = 0;
    std::size_t codepoint = 0;
    while (at < s.size()) {
        std::size_t next = at + 1;
        while (next < s.size() &&
               !detail::isCodePointStart(static_cast<unsigned char>(s[next]))) {
            ++next;
        }
        if (codepoint == want) return s.substr(at, next - at);
        at = next;
        ++codepoint;
    }
    return std::string();
}

// --- predicates ---------------------------------------------------------------

inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool includes(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

// Membership by equality: the lowering of `["a", "b"].includes(x)` for a
// literal *element set*. Array values become first-class in wave 3; until
// then this scalar form is what codegen can represent, so it lives here.
inline bool oneOf(const std::string& value,
                  std::initializer_list<std::string> candidates) {
    for (const std::string& candidate : candidates) {
        if (value == candidate) return true;
    }
    return false;
}

// --- search -------------------------------------------------------------------

// Codepoint index of the first occurrence, or -1 (JS `indexOf`).
inline double indexOf(const std::string& s, const std::string& needle) {
    const std::size_t pos = s.find(needle);
    if (pos == std::string::npos) return -1.0;
    std::size_t codepoints = 0;
    for (std::size_t i = 0; i < pos; ++i) {
        if (detail::isCodePointStart(static_cast<unsigned char>(s[i]))) ++codepoints;
    }
    return static_cast<double>(codepoints);
}

// Codepoint index of the last occurrence, or -1 (JS `lastIndexOf`).
inline double lastIndexOf(const std::string& s, const std::string& needle) {
    const std::size_t pos = s.rfind(needle);
    if (pos == std::string::npos) return -1.0;
    std::size_t codepoints = 0;
    for (std::size_t i = 0; i < pos; ++i) {
        if (detail::isCodePointStart(static_cast<unsigned char>(s[i]))) ++codepoints;
    }
    return static_cast<double>(codepoints);
}

// --- building -----------------------------------------------------------------

// JS `repeat`; a negative count yields "" here (JS throws RangeError).
inline std::string repeat(const std::string& s, double count) {
    if (!(count > 0.0) || s.empty()) return std::string();
    const std::size_t times = static_cast<std::size_t>(std::trunc(count));
    std::string out;
    out.reserve(s.size() * times);
    for (std::size_t i = 0; i < times; ++i) out += s;
    return out;
}

namespace detail {

inline std::string pad(const std::string& s, double target, std::string filler,
                       bool atStart) {
    if (!(target > static_cast<double>(s.size())) || filler.empty()) return s;
    const std::size_t width =
        static_cast<std::size_t>(std::trunc(target)) - s.size();
    std::string padding;
    padding.reserve(width);
    while (padding.size() < width) padding += filler;
    padding.resize(width);
    return atStart ? padding + s : s + padding;
}

}  // namespace detail

inline std::string padStart(const std::string& s, double target,
                            std::string filler = " ") {
    return detail::pad(s, target, std::move(filler), true);
}

inline std::string padEnd(const std::string& s, double target,
                          std::string filler = " ") {
    return detail::pad(s, target, std::move(filler), false);
}

// --- trimming + case mapping ---------------------------------------------------

inline std::string trimStart(const std::string& s) {
    std::size_t at = 0;
    while (at < s.size() && detail::isTrimSpace(static_cast<unsigned char>(s[at]))) {
        ++at;
    }
    return s.substr(at);
}

inline std::string trimEnd(const std::string& s) {
    std::size_t end = s.size();
    while (end > 0 && detail::isTrimSpace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(0, end);
}

inline std::string trim(const std::string& s) {
    return trimStart(trimEnd(s));
}

// ASCII-only case mapping (see the header note).
inline std::string toUpperCase(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    }
    return out;
}

inline std::string toLowerCase(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return out;
}

// --- replacement ---------------------------------------------------------------

// First occurrence only (JS `replace` with a string pattern). An empty pattern
// returns the receiver unchanged (JS would insert at every position).
inline std::string replace(const std::string& s, const std::string& from,
                           const std::string& to) {
    if (from.empty()) return s;
    const std::size_t pos = s.find(from);
    if (pos == std::string::npos) return s;
    return s.substr(0, pos) + to + s.substr(pos + from.size());
}

inline std::string replaceAll(const std::string& s, const std::string& from,
                              const std::string& to) {
    if (from.empty()) return s;
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t pos = s.find(from, at);
        if (pos == std::string::npos) break;
        out.append(s, at, pos - at);
        out += to;
        at = pos + from.size();
    }
    out.append(s, at, std::string::npos);
    return out;
}

// --- extraction ----------------------------------------------------------------

// JS `slice`: negatives count from the end; NaN (an omitted argument) means 0
// for start and "to the end" for end.
inline std::string slice(const std::string& s, double start,
                         double end = std::numeric_limits<double>::quiet_NaN()) {
    const double length = static_cast<double>(s.size());
    const double begin = detail::normalizeIndex(start, length, 0.0);
    const double stop = detail::normalizeIndex(end, length, length);
    if (!(stop > begin)) return std::string();
    return s.substr(static_cast<std::size_t>(begin),
                    static_cast<std::size_t>(stop - begin));
}

// JS `substring`: negative indices clamp to 0 and start/end swap when needed.
inline std::string substring(const std::string& s, double start,
                             double end = std::numeric_limits<double>::quiet_NaN()) {
    const double length = static_cast<double>(s.size());
    double begin = detail::normalizeIndex(start, length, 0.0);
    double stop = detail::normalizeIndex(end, length, length);
    if (begin > stop) {
        const double tmp = begin;
        begin = stop;
        stop = tmp;
    }
    return s.substr(static_cast<std::size_t>(begin),
                    static_cast<std::size_t>(stop - begin));
}

}  // namespace cp3::runtime::strings
