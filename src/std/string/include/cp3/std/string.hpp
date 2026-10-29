// =============================================================================
// cp3/std/string.hpp - primitive `string` method backing (spec §09 surface).
//
// OWNERSHIP: the std/string family (src/std/string/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only.
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
//   * `str[i]` is the raw UTF-8 byte as a number (docs 09); out-of-range or
//     fractional positions panic with a RangeError, the same rule as arrays.
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

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

// --- ASCII case mapping, word at a time --------------------------------------
//
// The obvious per-character form (`if (c >= 'a' && c <= 'z') c -= 32`) compiles
// to a data-dependent branch. On mixed-case text that branch is close to
// unpredictable and costs several cycles per byte, which dominates string
// churn workloads. The word form below is branchless: it maps eight bytes per
// word with two SWAR range tests. Bytes >= 0x80 are never matched, so
// UTF-8 sequences pass through untouched (the mapping is ASCII-only by spec,
// see the header note).
//
// The classic subtract-based byte compare (hasless) was avoided on purpose: a
// borrow from a lower lane propagates into the next one, so it can report a
// byte as "less than n" when its neighbour underflowed. The form below is exact
// per lane: bytes are masked to 7 bits and biased with an add, and a biased add
// cannot carry out of a lane because 0x7f + 0x7f < 0x100.
//
// 0x80 in every lane of `bytes` (each lane <= 0x7f) whose value is >= n, for
// n in [1, 127]; 0 in every other lane.
inline std::uint64_t byteAtLeast(std::uint64_t bytes, std::uint64_t n) {
    return (bytes + 0x0101010101010101ull * (0x80u - n)) & 0x8080808080808080ull;
}

// 0x80 in every ASCII byte of `word` in [lo, hi], 0 elsewhere. Lanes with the
// high bit set (non-ASCII, for example UTF-8 continuation bytes) never match.
inline std::uint64_t asciiRangeMask(std::uint64_t word, unsigned char lo,
                                    unsigned char hi) {
    const std::uint64_t ascii = ~word & 0x8080808080808080ull;
    const std::uint64_t bytes = word & 0x7f7f7f7f7f7f7f7f;
    return ascii & byteAtLeast(bytes, lo) &
           ~byteAtLeast(bytes, static_cast<unsigned char>(hi + 1));
}

// 0x80 in every ASCII lowercase byte of `word`, 0 elsewhere.
inline std::uint64_t lowerWordMask(std::uint64_t word) {
    return asciiRangeMask(word, 'a', 'z');
}

// 0x80 in every ASCII uppercase byte of `word`, 0 elsewhere.
inline std::uint64_t upperWordMask(std::uint64_t word) {
    return asciiRangeMask(word, 'A', 'Z');
}

// Branchless single byte forms, used for the word loop tail.
inline char upperByte(unsigned char c) {
    const unsigned char lower =
        static_cast<unsigned char>((static_cast<unsigned>(c) - 'a') <=
                                   (static_cast<unsigned>('z') - 'a'));
    return static_cast<char>(
        static_cast<unsigned char>(c - static_cast<unsigned char>(lower * 0x20u)));
}

inline char lowerByte(unsigned char c) {
    const unsigned char upper =
        static_cast<unsigned char>((static_cast<unsigned>(c) - 'A') <=
                                   (static_cast<unsigned>('Z') - 'A'));
    return static_cast<char>(
        static_cast<unsigned char>(c + static_cast<unsigned char>(upper * 0x20u)));
}

// toUpper == true maps to uppercase, false maps to lowercase. Byte-at-a-time
// words are endian agnostic: every lane is transformed independently and the
// mask shift (0x80 -> 0x20) stays inside the lane.
//
// Two words per iteration on purpose: the per-word mask chain is a serial
// dependency, and the compiler does not unroll this loop at -O2. Sixteen bytes per
// iteration measured 1.7x the throughput of eight (16.2 vs 9.5 GB/s on the
// string-churn data); thirty-two added nothing.
template <bool kToUpper>
inline void mapAsciiCase(char* data, std::size_t size) {
    std::size_t i = 0;
    for (; i + 16 <= size; i += 16) {
        std::uint64_t a;
        std::uint64_t b;
        std::memcpy(&a, data + i, 8);
        std::memcpy(&b, data + i + 8, 8);
        if (kToUpper) {
            a -= lowerWordMask(a) >> 2;
            b -= lowerWordMask(b) >> 2;
        } else {
            a += upperWordMask(a) >> 2;
            b += upperWordMask(b) >> 2;
        }
        std::memcpy(data + i, &a, 8);
        std::memcpy(data + i + 8, &b, 8);
    }
    for (; i + 8 <= size; i += 8) {
        std::uint64_t word;
        std::memcpy(&word, data + i, 8);
        if (kToUpper) {
            word -= lowerWordMask(word) >> 2;
        } else {
            word += upperWordMask(word) >> 2;
        }
        std::memcpy(data + i, &word, 8);
    }
    for (; i < size; ++i) {
        data[i] = kToUpper ? upperByte(static_cast<unsigned char>(data[i]))
                           : lowerByte(static_cast<unsigned char>(data[i]));
    }
}

// Result buffer for a case mapping. A heap-sized result gets a small growth
// tail because the common JS shape is `s.toLowerCase() + suffix`, and with a
// capacity of exactly size that append reallocates and copies the whole string
// (measured 20% of string-churn before this). Results that fit the small
// string buffer are left alone: reserving there would force the allocation the
// reserve is meant to avoid. Capacity is not observable from cp3.
inline std::string caseMapResult(const std::string& s) {
    std::string out;
    if (s.size() > 15) {
        out.reserve(s.size() + 16);
        out.assign(s);
    } else {
        out = s;
    }
    return out;
}

// --- fused concatenation -----------------------------------------------------
//
// `a + b + c` lowers here instead of nesting `std::string` temporaries: one
// result buffer is reserved from the known part sizes, then every part is
// appended in order. The parts are the string operands the type checker already
// accepted, so the bytes are identical to the left-associative `+` chain.
inline std::size_t concatSize(const std::string& s) { return s.size(); }
inline std::size_t concatSize(std::string_view s) { return s.size(); }
inline std::size_t concatSize(const char* s) {
    return s ? std::strlen(s) : 0;
}
inline std::size_t concatSize(char) { return 1; }

inline void concatAppend(std::string& out, const std::string& s) { out += s; }
inline void concatAppend(std::string& out, std::string_view s) {
    if (!s.empty()) out.append(s.data(), s.size());
}
inline void concatAppend(std::string& out, const char* s) {
    if (s) out += s;
}
inline void concatAppend(std::string& out, char c) { out.push_back(c); }

}  // namespace detail

template <class... Parts>
std::string concat(const Parts&... parts) {
    std::string out;
    out.reserve((detail::concatSize(parts) + ... + 0));
    (detail::concatAppend(out, parts), ...);
    return out;
}

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

// The string's code points, in order: the elements a `for (const ch of s)`
// walks (the JS for-of order - a whole code point, not a byte). cp3 strings
// are UTF-8 (docs 09), so a multi-byte character is one element.
inline std::vector<std::string> codePoints(const std::string& s) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < s.size()) {
        std::size_t next = at + 1;
        while (next < s.size() &&
               !detail::isCodePointStart(static_cast<unsigned char>(s[next]))) {
            ++next;
        }
        out.push_back(s.substr(at, next - at));
        at = next;
    }
    return out;
}

/// Docs 09: `str[i]` is the raw UTF-8 byte at byte position `i` as a number
/// (a deliberate departure from JS's UTF-16 code unit: a multi-byte character
/// yields one of its bytes, not the character). An out-of-range or
/// non-integer position is the same hard RangeError as array indexing
/// (exit 1, not catchable by a cp3 catch).
[[noreturn]] inline void indexFail() {
    std::fprintf(stderr, "cp3: RangeError: String index out of range\n");
    std::fflush(stderr);
    std::exit(1);
}

inline double byteAt(const std::string& s, double index) {
    if (std::isnan(index) || std::isinf(index) || std::trunc(index) != index ||
        index < 0.0 || index >= static_cast<double>(s.size())) {
        indexFail();
    }
    return static_cast<double>(
        static_cast<unsigned char>(s[static_cast<std::size_t>(index)]));
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

// ASCII-only case mapping (see the header note). Word at a time and branchless
// (detail::mapAsciiCase): the per-character branch was the single largest cost
// in string-heavy programs, because mixed-case ASCII makes it unpredictable.
inline std::string toUpperCase(const std::string& s) {
    std::string out = detail::caseMapResult(s);
    detail::mapAsciiCase<true>(out.data(), out.size());
    return out;
}

inline std::string toLowerCase(const std::string& s) {
    std::string out = detail::caseMapResult(s);
    detail::mapAsciiCase<false>(out.data(), out.size());
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

// JS `split`: every occurrence of the separator ends a piece; a separator
// that never matches yields the whole receiver. An empty separator splits into
// single bytes (the byte view of a string, D63), not codepoints.
inline std::vector<std::string> split(const std::string& s,
                                      const std::string& sep) {
    std::vector<std::string> out;
    if (sep.empty()) {
        out.reserve(s.size());
        for (char c : s) out.emplace_back(1, c);
        return out;
    }
    std::size_t at = 0;
    while (true) {
        const std::size_t pos = s.find(sep, at);
        if (pos == std::string::npos) {
            out.push_back(s.substr(at));
            return out;
        }
        out.push_back(s.substr(at, pos - at));
        at = pos + sep.size();
    }
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
