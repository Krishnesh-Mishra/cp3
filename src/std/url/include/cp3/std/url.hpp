// =============================================================================
// cp3/std/url.hpp - `std/url` backing: URI encoding and decoding, `URL` and
// `URLSearchParams`.
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
//   `new URL(input)`        -> `::cp3::runtime::url::makeURL(input)`
//   `new URL(input, base)`  -> `::cp3::runtime::url::makeURL(input, base)`
//   `new URLSearchParams()` -> `::cp3::runtime::url::makeURLSearchParams()`
//   `new URLSearchParams(q)`-> `::cp3::runtime::url::makeURLSearchParams(q)`
//   `u.href` / `u.protocol` / ... -> `u->href` / `u->protocol` / ...
//   `u.searchParams`        -> `u->searchParams`
//   `u.toString()`          -> `u->toString()` (returns `href`)
//   `sp.append(k, v)`       -> `sp->append(k, v)`
//   `sp.get(k)`             -> `sp->get(k)` (`std::optional<std::string>`)
//   `sp.getAll(k)`          -> `sp->getAll(k)` (`Array<std::string>`)
//   `sp.has(k)`             -> `sp->has(k)`
//   `sp.set(k, v)`          -> `sp->set(k, v)`
//   `sp.delete(k)`          -> `sp->remove(k)` (`delete` is a C++ keyword)
//   `sp.toString()`         -> `sp->toString()`
//   `sp.entries()`          -> `sp->entries()`
//                              (`Array<std::pair<std::string, std::string>>`)
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
//   * The RFC 3986 parser (`detail::parseRaw`) is hand-written over
//     `std::string_view`: scheme, authority (userinfo, host, port), path,
//     query and fragment are sliced, never matched with std::regex.
//   * `URL` components are stored as plain `std::string` snapshots taken at
//     construction. Mutating `url.searchParams` afterwards does NOT rewrite
//     `url.search` / `url.href` (unlike WHATWG live binding); build a new
//     `URL(typeof, ...)` if a snapshot must reflect the query.
//   * `URLSearchParams` follows the WHATWG application/x-www-form-urlencoded
//     rules: `+` is a space on parse and a space encodes as `+`; malformed
//     `%HH` sequences parse as literal text.
//   * Invalid-URL construction throws a catchable cp3 `Error`
//     (`Thrown::make`), never a `std::exception` the `catch` model cannot take.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/core/value.hpp"

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

// A catchable cp3 error value (docs/language-spec/07): the generated
// `catch (e)` binds the boxed message this carries.
[[noreturn]] inline void fail(const std::string& message) {
    throw Thrown::make(message);
}

// ---------------------------------------------------------------------------
// application/x-www-form-urlencoded (URLSearchParams) encode/decode
// ---------------------------------------------------------------------------

// WHATWG urlencoded byte set: alphanumerics plus `*` `-` `.` `_` stay, a space
// becomes `+`, everything else is `%HH` uppercase.
inline bool isFormUnreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' ||
           c == '_';
}

inline std::string formEncode(const std::string& input) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        if (ch == ' ') {
            out.push_back('+');
        } else if (isFormUnreserved(ch)) {
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

inline std::string formDecode(const std::string& input) {
    // `+` is a literal space; `%HH` follows the same permissive rule as
    // `decode` (malformed sequences stay). The `+` rewrite runs first so that
    // a literal `%2B` still decodes to `+`, not to a space.
    std::string plus(input);
    for (char& c : plus) {
        if (c == '+') c = ' ';
    }
    return decode(plus);
}

// ---------------------------------------------------------------------------
// Hand-written RFC 3986 parser (no std::regex anywhere in this module)
// ---------------------------------------------------------------------------

inline bool isAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

inline bool isSchemeChar(char c) {
    return isAlpha(c) || isDigit(c) || c == '+' || c == '-' || c == '.';
}

inline char lowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

inline std::string lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(lowerAscii(c));
    return out;
}

// The components of a parsed RFC 3986 reference, sliced out of the input.
// `hasQuery`/`hasFragment` distinguish `?` from absent and `#` from absent;
// `hasAuthority` distinguishes `//` from absent.
struct Parsed {
    std::string scheme;
    bool hasAuthority = false;
    bool hasUserinfo = false;
    std::string username;
    std::string password;
    std::string host;  // hostname without brackets, lowercased
    std::string port;  // digits only, default ports stripped
    std::string path;
    bool hasQuery = false;
    std::string query;
    bool hasFragment = false;
    std::string fragment;
    bool invalid = false;
};

// Splits `s` at the first occurrence of `delim`; returns the remainder after
// it, or `std::nullopt` when absent.
inline std::optional<std::string_view> cutAt(std::string_view s, char delim) {
    const std::size_t pos = s.find(delim);
    if (pos == std::string_view::npos) return std::nullopt;
    return s.substr(pos + 1);
}

// Parses the authority `rest` (everything after `//` up to the path start).
// `rest` must not contain a path separator; the caller slices at the first
// `/` `?` `#` before calling.
inline Parsed parseAuthority(const Parsed& outer, std::string_view authority) {
    Parsed p = outer;
    std::string_view hostport = authority;
    if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
        // userinfo is everything up to the LAST `@` (an `@` inside the
        // userinfo would be percent-encoded; a raw one still splits here).
        const std::string_view userinfo = authority.substr(0, at);
        p.hasUserinfo = true;
        const auto colon = userinfo.find(':');
        if (colon == std::string_view::npos) {
            p.username = std::string(userinfo);
        } else {
            p.username = std::string(userinfo.substr(0, colon));
            p.password = std::string(userinfo.substr(colon + 1));
        }
        hostport = authority.substr(at + 1);
    }
    if (hostport.empty()) {
        p.host.clear();
        p.port.clear();
        p.invalid = true;
        return p;
    }
    if (hostport.front() == '[') {
        // IPv6 literal `[::1]:8080` / `[::1]`: the host keeps the brackets
        // inside `host` for serialization; `hostname` later strips them.
        const std::size_t close = hostport.find(']');
        if (close != std::string_view::npos) {
            p.host = std::string(hostport.substr(1, close - 1));
            std::string_view tail = hostport.substr(close + 1);
            if (!tail.empty()) {
                if (tail.front() != ':') {
                    p.host.clear();
                    p.port.clear();
                    p.invalid = true;
                    return p;
                }
                const std::string_view digits = tail.substr(1);
                for (char c : digits) {
                    if (!isDigit(c)) {
                        p.host.clear();
                        p.port.clear();
                        p.invalid = true;
                        return p;
                    }
                }
                p.port = std::string(digits);
            }
            return p;
        }
        // Unclosed bracket: treat the whole thing as a (bogus) host.
        p.host = lower(hostport);
        return p;
    }
    // reg-name / IPv4 host with an optional `:port`. Split at the LAST `:`.
    // A host with more than one `:` (an unbracketed IPv6 literal) is a
    // parse failure, the same as a non-digit port.
    bool digits_only = true;
    for (char c : hostport) {
        digits_only = digits_only && isDigit(c);
    }
    if (digits_only) {
        // A bare number is a host name (e.g. `http://123/`), not a port.
        p.host = lower(hostport);
        return p;
    }
    const std::size_t colon = hostport.rfind(':');
    if (colon == std::string_view::npos) {
        p.host = lower(hostport);
        return p;
    }
    const std::string_view portStr = hostport.substr(colon + 1);
    // An unbracketed IPv6 literal (`http://::1/`) carries more than one
    // colon: it is a parse failure, the same as a non-digit port.
    if (hostport.substr(0, colon).find(':') != std::string_view::npos) {
        p.host.clear();
        p.port.clear();
        p.invalid = true;
        return p;
    }
    if (portStr.empty()) {
        p.host = lower(hostport.substr(0, colon));
        return p;
    }
    bool portOk = true;
    for (char c : portStr) {
        if (!isDigit(c)) {
            portOk = false;
            break;
        }
    }
    if (!portOk) {
        p.host.clear();
        p.port.clear();
        p.invalid = true;
        return p;
    }
    p.host = lower(hostport.substr(0, colon));
    p.port = std::string(portStr);
    return p;
}

// Strips the well-known default port for the scheme (WHATWG): `http`/`ws` 80
// and `https`/`wss` 443 serialize without the port.
inline void stripDefaultPort(Parsed& p) {
    if (p.port.empty()) return;
    const bool def = (p.scheme == "http" || p.scheme == "ws") && p.port == "80" ||
                     (p.scheme == "https" || p.scheme == "wss") && p.port == "443";
    if (def) p.port.clear();
}

// Parses one RFC 3986 reference (the WHATWG URL input, trimming ASCII
// whitespace/C0 first). Returns `std::nullopt` when the input is not a valid
// reference at all (missing scheme for a non-relative value) so the caller
// can report `TypeError: Invalid URL`.
inline std::optional<Parsed> parseRaw(std::string_view raw) {
    // ASCII-whitespace trim (CP3 strings are UTF-8 byte strings; the WHATWG
    // input trims C0 control + space from both ends).
    while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && static_cast<unsigned char>(raw.back()) <= 0x20) {
        raw.remove_suffix(1);
    }
    if (raw.empty()) return std::nullopt;

    Parsed p;
    std::string_view rest = raw;

    // Fragment first: `#` opens the fragment, everything after it is opaque.
    if (const auto frag = cutAt(rest, '#')) {
        p.fragment = std::string(*frag);
        p.hasFragment = true;
        rest = rest.substr(0, rest.size() - frag->size() - 1);
    }
    // Query: `?` opens the query (before the fragment, already cut away).
    if (const auto q = cutAt(rest, '?')) {
        p.query = std::string(*q);
        p.hasQuery = true;
        rest = rest.substr(0, rest.size() - q->size() - 1);
    }

    // Scheme: the text before the first `:` is a scheme only when it matches
    // the RFC 3986 grammar (alpha, then alphanumerics / `+` `-` `.`). A `:`
    // before any `/` decides; scheme letters never contain `/`.
    const std::size_t colon = rest.find(':');
    const std::size_t slash = rest.find('/');
    const bool colonWins = colon != std::string_view::npos &&
                         (slash == std::string_view::npos || colon < slash);
    if (colonWins) {
        const std::string_view candidate = rest.substr(0, colon);
        bool schemeOk = !candidate.empty() && isAlpha(candidate.front());
        for (char c : candidate) {
            schemeOk = schemeOk && isSchemeChar(c);
        }
        if (!schemeOk) return std::nullopt;
        p.scheme = lower(candidate);
        rest = rest.substr(colon + 1);
    } else if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
        // Network-path reference `//host/path`: a scheme is required; the
        // caller resolves it against the base scheme.
        p.scheme.clear();
    } else {
        // No scheme: a relative reference (the caller resolves it against
        // the base, or fails closed when there is no base).
        p.scheme.clear();
        p.path = std::string(rest);
        stripDefaultPort(p);
        return p;
    }

    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
        p.hasAuthority = true;
        std::string_view after = rest.substr(2);
        const std::size_t end = after.find('/');
        const std::string_view authority =
            end == std::string_view::npos ? after : after.substr(0, end);
        p = parseAuthority(p, authority);
        rest = end == std::string_view::npos ? std::string_view{} : after.substr(end);
    }
    p.path = std::string(rest);
    stripDefaultPort(p);
    return p;
}

// RFC 3986 section 5.2.4 dot-segment removal, over the raw path bytes.
inline std::string removeDotSegments(std::string_view path) {
    std::string out;
    std::string_view in = path;
    while (!in.empty()) {
        if (in.substr(0, 3) == "../" || in.substr(0, 2) == "./") {
            in.remove_prefix(in.substr(0, 3) == "../" ? 3 : 2);
        } else if (in.substr(0, 3) == "/./") {
            in = in.substr(2);
        } else if (in == "/.") {
            in = "/";
        } else if (in.substr(0, 4) == "/../") {
            in = in.substr(3);
            const std::size_t last = out.rfind('/');
            out = last == std::string::npos ? std::string() : out.substr(0, last);
        } else if (in == "/..") {
            in = "/";
            const std::size_t last = out.rfind('/');
            out = last == std::string::npos ? std::string() : out.substr(0, last);
        } else if (in == "." || in == "..") {
            in.remove_prefix(in.size());
        } else {
            const std::size_t next = in.find('/', 1);
            if (next == std::string_view::npos) {
                out.append(in);
                in.remove_prefix(in.size());
            } else {
                out.append(in.substr(0, next));
                in.remove_prefix(next);
            }
        }
    }
    return out;
}

// RFC 3986 section 5.2.3: the merged path for a relative reference. The base
// path's last segment (the file) is replaced by the input path.
inline std::string mergePaths(const Parsed& base, const std::string& inputPath) {
    if (base.hasAuthority && base.path.empty()) return "/" + inputPath;
    const std::size_t slash = base.path.rfind('/');
    if (slash == std::string::npos) return inputPath;
    return base.path.substr(0, slash + 1) + inputPath;
}

// RFC 3986 section 5.2.2: resolves a relative reference against the base.
inline Parsed resolve(Parsed base, const Parsed& input) {
    Parsed target;
    if (!input.scheme.empty()) {
        target = input;
        target.path = removeDotSegments(input.path);
        return target;
    }
    target.scheme = base.scheme;
    if (input.hasAuthority) {
        target = input;
        target.scheme = base.scheme;
        target.path = removeDotSegments(input.path);
        // The merged scheme may make the port a default port
        // (`//example.com:80/` against an `http://` base).
        stripDefaultPort(target);
        return target;
    }
    target.hasAuthority = base.hasAuthority;
    target.hasUserinfo = base.hasUserinfo;
    target.username = base.username;
    target.password = base.password;
    target.host = base.host;
    target.port = base.port;
    if (input.path.empty()) {
        target.path = base.path;
        if (input.hasQuery) {
            target.hasQuery = true;
            target.query = input.query;
        } else {
            target.hasQuery = base.hasQuery;
            target.query = base.query;
        }
    } else if (!input.path.empty() && input.path.front() == '/') {
        target.path = removeDotSegments(input.path);
        target.hasQuery = input.hasQuery;
        target.query = input.query;
    } else {
        target.path = removeDotSegments(mergePaths(base, input.path));
        target.hasQuery = input.hasQuery;
        target.query = input.query;
    }
    target.hasFragment = input.hasFragment;
    target.fragment = input.fragment;
    return target;
}

// The serialized `host` component: WHATWG `host` keeps brackets for IPv6 and
// carries the port when present; `hostname` is the bare lowercased name.
inline std::string hostWithPort(const Parsed& p) {
    std::string out;
    if (p.host.find(':') != std::string::npos) {
        out = "[" + p.host + "]";
    } else {
        out = p.host;
    }
    if (!p.port.empty()) {
        out += ":" + p.port;
    }
    return out;
}

// WHATWG `origin`: `scheme://host[:port]` for special schemes, "" otherwise
// (opaque schemes and `file:` have no serializable origin here).
inline std::string originOf(const Parsed& p) {
    const bool special = p.scheme == "http" || p.scheme == "https" ||
                         p.scheme == "ws" || p.scheme == "wss" ||
                         p.scheme == "ftp";
    if (!p.hasAuthority || !special || p.host.empty()) return std::string();
    std::string out = p.scheme + "://" + hostWithPort(p);
    return out;
}

// The serialized `href`: RFC 3986 section 5.3, WHATWG reading. A pathless
// authority URL serializes with `/`.
inline std::string serializeHref(const Parsed& p) {
    std::string out = p.scheme + ":";
    if (p.hasAuthority) {
        out += "//";
        if (p.hasUserinfo) {
            out += p.username;
            // `userinfo` without a password serializes bare (no colon)
            // per WHATWG; a password keeps `user:pass@`.
            if (!p.password.empty()) {
                out += ":" + p.password;
            }
            out += "@";
        }
        out += hostWithPort(p);
    }
    std::string path = p.path;
    if (path.empty() && p.hasAuthority) {
        path = "/";
    }
    out += path;
    if (p.hasQuery) {
        out += "?" + p.query;
    }
    if (p.hasFragment) {
        out += "#" + p.fragment;
    }
    return out;
}

}  // namespace detail

// =============================================================================
// URLSearchParams
// =============================================================================

// The parsed `application/x-www-form-urlencoded` pair list, in order. `get`
// yields the first value, `getAll` every value, `set` replaces the first and
// drops the rest, `remove` (the cp3 `delete`) drops every pair with the key.
class URLSearchParamsData {
public:
    URLSearchParamsData() = default;

    // `new URLSearchParams("a=1&b=2")`; a leading `?` is skipped.
    explicit URLSearchParamsData(const std::string& query) { initFromQuery(query); }

    void initFromQuery(const std::string& query) {
        std::string_view s = query;
        while (!s.empty() && s.front() == '?') s.remove_prefix(1);
        std::size_t pos = 0;
        while (pos <= s.size()) {
            const std::size_t amp = s.find('&', pos);
            const std::string_view part =
                amp == std::string_view::npos ? s.substr(pos) : s.substr(pos, amp - pos);
            if (!part.empty()) {
                const std::size_t eq = part.find('=');
                if (eq == std::string_view::npos) {
                    pairs_.emplace_back(detail::formDecode(std::string(part)),
                                        std::string());
                } else {
                    pairs_.emplace_back(
                        detail::formDecode(std::string(part.substr(0, eq))),
                        detail::formDecode(std::string(part.substr(eq + 1))));
                }
            }
            if (amp == std::string_view::npos) break;
            pos = amp + 1;
        }
    }

    // Copy appends the freshly parsed pairs on top of the existing ones.
    void appendParams(const URLSearchParamsData& other) {
        pairs_.insert(pairs_.end(), other.pairs_.begin(), other.pairs_.end());
    }

    void append(const std::string& name, const std::string& value) {
        pairs_.emplace_back(name, value);
    }

    std::optional<std::string> get(const std::string& name) const {
        for (const auto& kv : pairs_) {
            if (kv.first == name) return kv.second;
        }
        return std::nullopt;
    }

    Array<std::string> getAll(const std::string& name) const {
        std::vector<std::string> out;
        for (const auto& kv : pairs_) {
            if (kv.first == name) out.push_back(kv.second);
        }
        return makeArray<std::string>(std::move(out));
    }

    bool has(const std::string& name) const {
        for (const auto& kv : pairs_) {
            if (kv.first == name) return true;
        }
        return false;
    }

    void set(const std::string& name, const std::string& value) {
        bool seen = false;
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(pairs_.size() + 1);
        for (auto& kv : pairs_) {
            if (kv.first != name) {
                out.push_back(kv);
            } else if (!seen) {
                out.emplace_back(name, value);
                seen = true;
            }
            // Later duplicates of the key are dropped (WHATWG `set`).
        }
        if (!seen) out.emplace_back(name, value);
        pairs_ = std::move(out);
    }

    // The cp3 surface calls this `delete`; `remove` avoids the keyword
    // clash. Removes every pair with the key, returning whether any stayed.
    bool remove(const std::string& name) {
        const std::size_t before = pairs_.size();
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(pairs_.size());
        for (const auto& kv : pairs_) {
            if (kv.first != name) out.push_back(kv);
        }
        pairs_ = std::move(out);
        return pairs_.size() != before;
    }

    std::string toString() const {
        std::string out;
        for (std::size_t i = 0; i < pairs_.size(); ++i) {
            if (i != 0) out.push_back('&');
            out += detail::formEncode(pairs_[i].first);
            out.push_back('=');
            out += detail::formEncode(pairs_[i].second);
        }
        return out;
    }

    // The optional `entries()` reading: the pairs as `[name, value]` tuples.
    Array<std::pair<std::string, std::string>> entries() const {
        return makeArray<std::pair<std::string, std::string>>(pairs_);
    }

private:
    std::vector<std::pair<std::string, std::string>> pairs_;
};

// The cp3 `URLSearchParams` handle: `Ref<URLSearchParamsData>` (the
// collections `Map` deliberately spells `Ref<MapData>` the same way).
using URLSearchParams = Ref<URLSearchParamsData>;

inline URLSearchParams makeURLSearchParams() {
    return makeRef<URLSearchParamsData>();
}

inline URLSearchParams makeURLSearchParams(const std::string& query) {
    return makeRef<URLSearchParamsData>(query);
}

// =============================================================================
// URL
// =============================================================================

// The parsed URL. The ten public string components are snapshots taken at
// construction; `searchParams` is a live handle owned by the same object.
class URLData {
public:
    std::string href;
    std::string protocol;
    std::string host;
    std::string hostname;
    std::string port;
    std::string pathname;
    std::string search;
    std::string hash;
    std::string origin;
    std::string username;
    std::string password;
    URLSearchParams searchParams;

    // The WHATWG serialization (`u.toString()` returns `u.href`). Snapshot
    // semantics: mutations to `searchParams` do not rewrite `href`.
    std::string toString() const { return href; }
};

// The cp3 `URL` handle.
using URL = Ref<URLData>;

namespace detail {

// Turns a resolved `Parsed` into the public snapshot object. Defined after
// `URLData` so it can name `URLSearchParams`.
inline URLData finish(const Parsed& p) {
    URLData out;
    out.protocol = p.scheme + ":";
    out.hostname = p.host;
    out.port = p.port;
    out.host = hostWithPort(p);
    out.pathname = p.path;
    // A pathless authority URL reports `/`, matching WHATWG serialization.
    if (out.pathname.empty() && p.hasAuthority) {
        out.pathname = "/";
    }
    out.search = p.hasQuery ? "?" + p.query : std::string();
    out.hash = p.hasFragment ? "#" + p.fragment : std::string();
    out.origin = originOf(p);
    out.username = p.username;
    out.password = p.password;
    out.searchParams = makeRef<URLSearchParamsData>(p.query);
    out.href = serializeHref(p);
    return out;
}

}  // namespace detail

// `new URL(input)`: absolute, no base. A relative input throws a catchable
// cp3 `Error`, matching the WHATWG `TypeError`.
inline URL makeURL(const std::string& input) {
    auto raw = detail::parseRaw(input);
    if (!raw || raw->scheme.empty() || raw->invalid) {
        detail::fail("TypeError: Invalid URL");
    }
    detail::Parsed target = *raw;
    target.path = detail::removeDotSegments(target.path);
    auto data = makeRef<URLData>(detail::finish(target));
    return data;
}

// `new URL(input, base)`: RFC 3986 section 5.2 resolution against `base`.
inline URL makeURL(const std::string& input, const std::string& base) {
    auto rawBase = detail::parseRaw(base);
    if (!rawBase || rawBase->scheme.empty() || rawBase->invalid) {
        detail::fail("TypeError: Invalid URL");
    }
    auto rawInput = detail::parseRaw(input);
    if (!rawInput || rawInput->invalid) {
        detail::fail("TypeError: Invalid URL");
    }
    const detail::Parsed target = detail::resolve(*rawBase, *rawInput);
    // A resolved value with no scheme is still relative (an unresolvable
    // base/input pair); the WHATWG contract is a throw there too.
    if (target.scheme.empty() || target.invalid) {
        detail::fail("TypeError: Invalid URL");
    }
    auto data = makeRef<URLData>(detail::finish(target));
    return data;
}

// =============================================================================
// Percent-encode/decode helpers (the original std/url surface, unchanged)
// =============================================================================

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
