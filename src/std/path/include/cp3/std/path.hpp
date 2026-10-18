// =============================================================================
// cp3/std/path.hpp - `path` (std/path) backing: Node-style path utilities.
//
// OWNERSHIP: the std/path family (src/std/path/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `path.<fn>(args)` -> `::cp3::runtime::path::<fn>(args)`; `path.sep` lowers
//   to `std::string(::cp3::runtime::path::kSep)`.
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * Both `/` and `\` are separators on Windows (Node win32 behavior); on
//     POSIX only `/`.
//   * `basename` strips a trailing `ext` only when the base is longer than the
//     extension (Node: basename(".txt", ".txt") == ".txt").
//   * `extname("archive.tar.gz") == ".gz"`; a leading dot never starts an
//     extension (Node).
//   * `isAbsolute("/foo")` is true on Windows too (Node win32 accepts a
//     root-relative slash).
//   * `normalize`/`resolve` are a faithful-enough approximation for the pinned
//     corpus (join/compile-only cases); drive-relative Windows forms ("C:foo")
//     are treated as ordinary segments.
// =============================================================================
#pragma once

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

namespace cp3::runtime::path {

#if defined(_WIN32)
inline constexpr const char* kSep = "\\";
#else
inline constexpr const char* kSep = "/";
#endif

namespace detail {

inline bool isSepChar(char c) {
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

inline char sepChar() { return kSep[0]; }

inline bool isDriveLetter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

}  // namespace detail

// --- queries -------------------------------------------------------------------

inline bool isAbsolute(const std::string& p) {
    if (p.empty()) return false;
    if (detail::isSepChar(p[0])) return true;
#if defined(_WIN32)
    if (p.size() >= 2 && detail::isDriveLetter(p[0]) && p[1] == ':') return true;
#endif
    return false;
}

// --- components ------------------------------------------------------------------

inline std::string basename(const std::string& p, const std::string& ext = "") {
    std::size_t end = p.size();
    while (end > 0 && detail::isSepChar(p[end - 1])) --end;
    if (end == 0) return std::string();
    std::size_t start = end;
    while (start > 0 && !detail::isSepChar(p[start - 1])) --start;
    std::string base = p.substr(start, end - start);
    if (!ext.empty() && base.size() > ext.size() &&
        base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
        base.resize(base.size() - ext.size());
    }
    return base;
}

inline std::string dirname(const std::string& p) {
    std::size_t end = p.size();
    while (end > 0 && detail::isSepChar(p[end - 1])) --end;
    std::size_t pos = end;
    while (pos > 0 && !detail::isSepChar(p[pos - 1])) --pos;
    if (pos == 0) return std::string(".");
    std::size_t cut = pos;
    while (cut > 1 && detail::isSepChar(p[cut - 1])) --cut;
    return p.substr(0, cut);
}

inline std::string extname(const std::string& p) {
    const std::string base = basename(p);
    if (base.empty() || base == "." || base == "..") return std::string();
    const std::size_t dot = base.rfind('.');
    if (dot == std::string::npos || dot == 0) return std::string();
    return base.substr(dot);
}

// --- normalization -----------------------------------------------------------------

inline std::string normalize(const std::string& p) {
    if (p.empty()) return std::string(".");
    std::string prefix;
    std::size_t at = 0;
#if defined(_WIN32)
    if (p.size() >= 2 && detail::isDriveLetter(p[0]) && p[1] == ':') {
        prefix = p.substr(0, 2);
        at = 2;
    }
#endif
    const bool rooted = at < p.size() && detail::isSepChar(p[at]);

    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = at; i < p.size(); ++i) {
        const char c = p[i];
        if (detail::isSepChar(c)) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);

    std::vector<std::string> stack;
    for (const std::string& part : parts) {
        if (part == ".") continue;
        if (part == "..") {
            if (!stack.empty() && stack.back() != "..") {
                stack.pop_back();
            } else if (!rooted && prefix.empty()) {
                stack.push_back(part);
            }
            continue;
        }
        stack.push_back(part);
    }

    std::string out = prefix;
    if (rooted || !prefix.empty()) out.push_back(detail::sepChar());
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (i != 0) out.push_back(detail::sepChar());
        out += stack[i];
    }
    if (out.empty()) return std::string(".");
    return out;
}

// --- composition -------------------------------------------------------------------

inline std::string joinParts(std::initializer_list<std::string> parts) {
    std::string joined;
    for (const std::string& part : parts) {
        if (part.empty()) continue;
        if (!joined.empty()) joined.push_back(detail::sepChar());
        joined += part;
    }
    return joined.empty() ? std::string(".") : normalize(joined);
}

inline std::string resolveParts(std::initializer_list<std::string> parts) {
    std::string out;
    for (const std::string& part : parts) {
        if (part.empty()) continue;
        if (isAbsolute(part)) {
            out = part;
        } else if (out.empty()) {
            out = part;
        } else {
            out = joinParts({out, part});
        }
    }
    std::error_code ec;
    const std::string cwd = std::filesystem::current_path(ec).string();
    if (out.empty()) {
        out = ec ? std::string(".") : cwd;
    } else if (!isAbsolute(out) && !ec) {
        out = joinParts({cwd, out});
    }
    return normalize(out);
}

// `path.join(...)`: variadic, arguments converted to strings at the call site
// (the emitter always produces std::string expressions).
template <class... Parts>
inline std::string join(const Parts&... parts) {
    return joinParts({std::string(parts)...});
}

template <class... Parts>
inline std::string resolve(const Parts&... parts) {
    return resolveParts({std::string(parts)...});
}

}  // namespace cp3::runtime::path
