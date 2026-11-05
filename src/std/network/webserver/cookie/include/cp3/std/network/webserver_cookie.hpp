// =============================================================================
// cp3/std/network/webserver_cookie.hpp - webserver cookie parity, header-only.
//
// OWNERSHIP: src/std/network/webserver/cookie/** (solo subagent, no shared
// files touched). Built ON TOP of the landed CP3 surface:
//   * `res.cookie(prebuiltSetCookieValue)` (HttpResponse::cookie) already
//     appends one `Set-Cookie` header; this header only *builds* that value.
//   * `req.cookies` (StringMap) already holds the raw `Cookie` header parse
//     (runtime_server.cpp parseCookies); this header adds percent-decoding,
//     first-wins semantics, signing/unsigning and `signedCookies` derivation.
//
// WIRE FORMAT (matches webserver `cookie` + `cookie-signature` on the wire):
//   Set-Cookie: name=value; Expires=...; Max-Age=...; Domain=...; Path=...;
//               Secure; HttpOnly; SameSite=Strict|Lax|None
//   signed value: `s:<value>.<base64(HMAC-SHA1(secret, value))>` with `=`
//               padding stripped (cookie-signature drops `=`; it signs the
//               *raw* value, then webserver percent-encodes the whole thing).
//   HMAC-SHA1/SHA-1 are implemented pure/inline below (no libsodium link, so
//   this header works even when std/crypto is not imported).
//
// secret sources (webserver parity):
//   * `app.set("secret", ...)`  -> lead lowers `app.set`/`app.get` for the
//     "secret" key onto setCookieSecret()/cookieSecrets() below (README).
//   * cookie-parser middleware  -> makeCookieParserMiddleware(secretOrSecrets)
//     registers the secret(s) and calls next(); `req.signedCookies` reads are
//     lowered onto signedCookiesFor(req) which reads the registry lazily.
//
// No .cpp, no native.hpp row needed beyond the include dir (header-only, D14
// additive). Generated code needs `#include "cp3/std/network/webserver_cookie.hpp"`
// and the include dir on the compile line (README: exact wiring).
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::webserver {

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-1), pure / header-only, for cookie-signature parity.
// ---------------------------------------------------------------------------
namespace detail {

inline std::uint32_t rol32(std::uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }

inline std::array<std::uint8_t, 20> sha1(std::string_view data) {
    std::uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::vector<std::uint8_t> msg(data.begin(), data.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xff));
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(msg[off + i * 4]) << 24) | (std::uint32_t(msg[off + i * 4 + 1]) << 16) |
                   (std::uint32_t(msg[off + i * 4 + 2]) << 8) | std::uint32_t(msg[off + i * 4 + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const std::uint32_t tmp = rol32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol32(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::array<std::uint8_t, 20> out{};
    const std::array<std::uint32_t, 5> hs{h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<std::uint8_t>((hs[i] >> (24 - j * 8)) & 0xff);
    return out;
}

inline std::array<std::uint8_t, 20> hmacSha1(std::string_view key, std::string_view data) {
    std::string k(key);
    if (k.size() > 64) {
        const auto hashed = sha1(k);
        k.assign(reinterpret_cast<const char*>(hashed.data()), hashed.size());
    }
    k.resize(64, '\0');
    std::string inner, outer;
    inner.reserve(64 + data.size());
    outer.reserve(64 + 20);
    for (int i = 0; i < 64; ++i) inner.push_back(static_cast<char>(static_cast<std::uint8_t>(k[i]) ^ 0x36));
    inner.append(data.data(), data.size());
    for (int i = 0; i < 64; ++i) outer.push_back(static_cast<char>(static_cast<std::uint8_t>(k[i]) ^ 0x5c));
    const auto innerHash = sha1(inner);
    outer.append(reinterpret_cast<const char*>(innerHash.data()), innerHash.size());
    return sha1(outer);
}

inline std::string base64NoPad(const std::array<std::uint8_t, 20>& bytes) {
    static constexpr char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t n = std::uint32_t(bytes[i]) << 16 |
                                (i + 1 < bytes.size() ? std::uint32_t(bytes[i + 1]) << 8 : 0) |
                                (i + 2 < bytes.size() ? std::uint32_t(bytes[i + 2]) : 0);
        out.push_back(kAlpha[(n >> 18) & 63]);
        out.push_back(kAlpha[(n >> 12) & 63]);
        if (i + 1 < bytes.size()) out.push_back(kAlpha[(n >> 6) & 63]);
        if (i + 2 < bytes.size()) out.push_back(kAlpha[n & 63]);
    }
    return out; // padding omitted, exactly like cookie-signature
}

inline bool constantTimeEq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Percent coding (encodeURIComponent / decodeURIComponent, tolerant decode).
// ---------------------------------------------------------------------------
inline bool isUnreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
}

inline std::string percentEncode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (isUnreserved(ch)) { out.push_back(ch); continue; }
        out.push_back('%'); out.push_back(kHex[c >> 4]); out.push_back(kHex[c & 15]);
    }
    return out;
}

inline std::string percentDecodeTolerant(std::string_view value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(value[i + 1]), lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; continue; }
        }
        out.push_back(value[i]); // malformed escape: keep raw (cookie-parser fallback)
    }
    return out;
}

// ---------------------------------------------------------------------------
// CookieOptions + Set-Cookie building.
// ---------------------------------------------------------------------------
enum class SameSite { kUnset, kStrict, kLax, kNone };

struct CookieOptions {
    double maxAge = 0.0;            // seconds (webserver `maxAge` is ms; CP3 codegen divides by 1000, see README)
    bool hasMaxAge = false;
    std::string expires;            // IMF-fixdate text, e.g. "Wed, 21 Oct 2015 07:28:00 GMT"; empty = unset
    std::string path;               // empty = unset (webserver default "/" applied by clearCookie only)
    std::string domain;             // empty = unset
    bool secure = false;
    bool httpOnly = false;
    SameSite sameSite = SameSite::kUnset;
    bool signed_ = false;           // webserver `signed: true`
    bool encode = true;             // webserver always encodes; kept adjustable for tests
};

inline void validateCookieName(std::string_view name) {
    if (name.empty()) throw std::invalid_argument("cookie name must not be empty");
    for (char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c >= 0x7f || ch == ';' || ch == ',' || ch == '=')
            throw std::invalid_argument("invalid cookie name");
    }
}

/// `res.cookie(name, value, options)` header value (before signing).
inline std::string buildSetCookie(std::string_view name, std::string_view value, const CookieOptions& opts) {
    validateCookieName(name);
    std::string header(name);
    header.push_back('=');
    header += opts.encode ? percentEncode(value) : std::string(value);
    if (!opts.expires.empty()) { header += "; Expires="; header += opts.expires; }
    if (opts.hasMaxAge) {
        header += "; Max-Age=";
        header += std::to_string(static_cast<long long>(opts.maxAge));
    }
    if (!opts.domain.empty()) { header += "; Domain="; header += opts.domain; }
    if (!opts.path.empty()) { header += "; Path="; header += opts.path; }
    if (opts.secure) header += "; Secure";
    if (opts.httpOnly) header += "; HttpOnly";
    switch (opts.sameSite) {
        case SameSite::kStrict: header += "; SameSite=Strict"; break;
        case SameSite::kLax: header += "; SameSite=Lax"; break;
        case SameSite::kNone: header += "; SameSite=None"; break;
        case SameSite::kUnset: break;
    }
    return header;
}

/// IMF-fixdate for an epoch-ms instant (webserver `expires: Date` lowered by
/// codegen from a number; a string `expires` passes through untouched).
inline std::string httpDateFromEpochMs(double epochMs) {
    const std::time_t t = static_cast<std::time_t>(epochMs / 1000.0);
    std::tm tm{};
#if defined(_MSC_VER)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    // RFC 1123: "Wed, 21 Oct 2015 07:28:00 GMT" (locale-independent enough here:
    // strftime uses the C locale in the runtime).
    if (std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S GMT", &tm) == 0) return "";
    return buf;
}

/// `res.clearCookie(name, options)` header value: past expiry + Max-Age=0.
inline std::string buildClearCookie(std::string_view name, const CookieOptions& opts) {
    validateCookieName(name);
    CookieOptions cleared = opts;
    cleared.expires = "Thu, 01 Jan 1970 00:00:00 GMT";
    cleared.maxAge = 0.0;
    cleared.hasMaxAge = true;
    if (cleared.path.empty()) cleared.path = "/";
    cleared.signed_ = false;
    return buildSetCookie(name, "", cleared);
}

// ---------------------------------------------------------------------------
// Signing (cookie-signature compatible: s:<value>.<base64 HMAC-SHA1>).
// ---------------------------------------------------------------------------
inline std::string signValue(std::string_view value, std::string_view secret) {
    const auto mac = detail::hmacSha1(secret, value);
    return "s:" + std::string(value) + "." + detail::base64NoPad(mac);
}

/// All secrets are tried (key rotation, webserver `string | string[]`).
inline bool unsignValue(std::string_view signedValue, const std::vector<std::string>& secrets,
                        std::string& unsignedOut) {
    if (signedValue.size() < 3 || signedValue.substr(0, 2) != "s:") return false;
    const std::size_t dot = signedValue.rfind('.');
    if (dot == std::string_view::npos || dot < 2) return false;
    const std::string_view value = signedValue.substr(2, dot - 2);
    const std::string_view sig = signedValue.substr(dot + 1);
    for (const std::string& secret : secrets) {
        if (secret.empty()) continue;
        const auto mac = detail::hmacSha1(secret, value);
        if (detail::constantTimeEq(sig, detail::base64NoPad(mac))) {
            unsignedOut = std::string(value);
            return true;
        }
    }
    return false;
}

inline bool unsignValue(std::string_view signedValue, std::string_view secret, std::string& unsignedOut) {
    return unsignValue(signedValue, std::vector<std::string>{std::string(secret)}, unsignedOut);
}

// ---------------------------------------------------------------------------
// Secret registry (process-global; see header comment + README limitations).
// ---------------------------------------------------------------------------
inline std::vector<std::string>& cookieSecrets() {
    static std::vector<std::string> secrets;
    return secrets;
}
inline void setCookieSecret(std::string secret) { cookieSecrets() = {std::move(secret)}; }
inline void setCookieSecrets(std::vector<std::string> secrets) { cookieSecrets() = std::move(secrets); }

// ---------------------------------------------------------------------------
// Parsing `req.cookies` / `req.signedCookies`.
// ---------------------------------------------------------------------------
/// webserver `cookie` parse: `;`-separated, first occurrence wins, values
/// percent-decoded tolerantly. (The landed server parse is last-wins + raw;
/// lead may swap it for this, or keep both: this is the parity reference.)
inline StringMap parseCookieHeader(std::string_view header) {
    StringMap map = collections::makeMap<std::string, std::string>();
    std::size_t start = 0;
    while (start < header.size()) {
        const std::size_t semi = header.find(';', start);
        const std::size_t end = semi == std::string_view::npos ? header.size() : semi;
        const std::string_view part = header.substr(start, end - start);
        const std::size_t eq = part.find('=');
        if (eq != std::string_view::npos) {
            const std::size_t b = part.substr(0, eq).find_first_not_of(' ');
            const std::size_t e = part.substr(0, eq).find_last_not_of(' ');
            if (b != std::string_view::npos) {
                const std::string name(part.substr(b, e - b + 1));
                if (!map->has(name))
                    map->set(name, percentDecodeTolerant(part.substr(eq + 1)));
            }
        }
        if (semi == std::string_view::npos) break;
        start = semi + 1;
    }
    return map;
}

/// `req.signedCookies`: only cookies whose `s:` value verifies against one of
/// `secrets` (or the registry when `secrets` is empty). Tampered/unsigned
/// cookies are omitted (webserver sets them to `false`; Map<string,string>
/// cannot represent that, see README).
inline StringMap signedCookiesFor(const HttpRequest& req, const std::vector<std::string>& secrets = {}) {
    const std::vector<std::string>& keys = secrets.empty() ? cookieSecrets() : secrets;
    StringMap out = collections::makeMap<std::string, std::string>();
    if (keys.empty()) return out;
    req.cookies->forEach([&](const std::string& value, const std::string& name) {
        std::string unsignedValue;
        if (unsignValue(value, keys, unsignedValue)) out->set(name, unsignedValue);
    });
    return out;
}

// ---------------------------------------------------------------------------
// Response helpers (thin wrappers over the landed res.cookie append).
// ---------------------------------------------------------------------------
inline HttpResponse& setCookie(HttpResponse& res, std::string_view name, std::string_view value,
                               const CookieOptions& opts, const std::vector<std::string>& secrets = {}) {
    std::string stored(value);
    if (opts.signed_) {
        const std::vector<std::string>& keys = secrets.empty() ? cookieSecrets() : secrets;
        if (keys.empty() || keys.front().empty())
            throw std::invalid_argument("signed cookie requires a secret (app.set('secret', ...) or cookieParser(secret))");
        stored = signValue(value, keys.front());
    }
    return res.cookie(buildSetCookie(name, stored, opts));
}

inline HttpResponse& clearCookie(HttpResponse& res, std::string_view name, const CookieOptions& opts = {}) {
    return res.cookie(buildClearCookie(name, opts));
}

// ---------------------------------------------------------------------------
// cookie-parser middleware (webserver `app.use(cookieParser(secret))`).
// Lazily registers the secret; `req.signedCookies` is derived on read by
// signedCookiesFor(), so no HttpRequest field is needed. With no secret the
// middleware is a pass-through (unsigned parsing only, already done server-side).
// ---------------------------------------------------------------------------
struct CookieParserOptions {
    std::vector<std::string> secrets; // string | string[] in webserver
};

inline MiddlewareFn makeCookieParserMiddleware(std::vector<std::string> secrets) {
    return [secrets = std::move(secrets)](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
        (void)req; (void)res;
        if (!secrets.empty()) setCookieSecrets(secrets);
        next();
        return Promise<void>::resolve();
    };
}
inline MiddlewareFn makeCookieParserMiddleware(std::string secret) {
    return makeCookieParserMiddleware(std::vector<std::string>{std::move(secret)});
}
inline MiddlewareFn makeCookieParserMiddleware() {
    return [](HttpRequest& req, HttpResponse& res, Next& next) -> Promise<void> {
        (void)req; (void)res;
        next();
        return Promise<void>::resolve();
    };
}
inline MiddlewareFn makeCookieParserMiddleware(const CookieParserOptions& options) {
    return makeCookieParserMiddleware(options.secrets);
}

}  // namespace cp3::runtime::network::webserver
