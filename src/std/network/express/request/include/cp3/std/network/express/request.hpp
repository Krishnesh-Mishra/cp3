// =============================================================================
// cp3/std/network/express/request.hpp - Express `req` helpers (std/network/express/request).
//
// OWNERSHIP: the express/request family (src/std/network/express/request/**).
// Additive to the generated-code ABI (D14): header-only, everything `inline`.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   Properties (member reads):
//     req.protocol    -> protocol(req)
//     req.secure      -> secure(req)          (protocol == "https")
//     req.hostname    -> hostname(req)
//     req.subdomains  -> subdomains(req)      (std::vector<std::string>)
//     req.baseUrl     -> baseUrl(req)
//     req.originalUrl -> originalUrl(req)
//     req.path        -> path(req)            (passthrough of HttpRequest::path)
//     req.fresh       -> fresh(req)
//     req.stale       -> stale(req)           (!fresh(req))
//     req.xhr         -> xhr(req)
//   Methods (member calls, receiver first):
//     req.is(...types)              -> is(req, std::vector<std::string>{...})
//     req.accepts(...types)         -> accepts(req, std::vector<std::string>{...})
//     req.acceptsEncodings(...enc)  -> acceptsEncodings(req, std::vector<std::string>{...})
//     req.acceptsCharsets(...cs)    -> acceptsCharsets(req, std::vector<std::string>{...})
//
//   Every helper takes an optional trailing `ExpressRequestOptions` for the
//   cases plain `HttpRequest` cannot carry (trust-proxy, TLS, router state):
//     req.protocol      -> protocol(req, ExpressRequestOptions{...})
//   Generated code always uses the default options; hand-written C++ callers
//   (and a future router) use the options / `ExpressRequest` view below.
//
// -----------------------------------------------------------------------------
// SEMANTICS (Express 4/5 `req` surface, docs/standard-library/04 network)
// -----------------------------------------------------------------------------
//   * protocol: when `trustProxy` is set and `X-Forwarded-Proto` is present,
//     its first comma-separated token (lower-cased, trimmed); otherwise "https"
//     when the socket is TLS (`options.tls`), else "http". The beta-one server
//     is plain HTTP (D70, TLS off), so a default request reports "http".
//   * hostname / subdomains: `X-Forwarded-Host` (first token) replaces `Host`
//     only under `trustProxy` (Express `trust proxy`); the port is stripped
//     (including `[IPv6]:port`). subdomains drops the last `subdomainOffset`
//     (default 2) labels: `a.b.example.com` -> ["a", "b"].
//   * baseUrl / originalUrl: router state, not on the wire. They come from
//     `ExpressRequestOptions` (set by the router layer); both default to the
//     request path when unset. NOTE: `HttpRequest::path` already had the query
//     string split off by the server, so a default originalUrl has no query.
//   * fresh / stale: RFC 7232 freshness of the (not yet sent) response against
//     the request's conditional headers. GET/HEAD only. If-None-Match wins:
//     a `*` or a weak-comparison hit on the response ETag is fresh. Otherwise
//     If-Modified-Since vs the response Last-Modified (HTTP dates). No
//     conditional header, or no response ETag/Last-Modified to compare
//     against, is NOT fresh (Express `fresh` module behaviour).
//   * xhr: `X-Requested-With` equals "XMLHttpRequest" (case-insensitive).
//   * is / accepts / acceptsEncodings / acceptsCharsets: content negotiation
//     over the request's Content-Type / Accept / Accept-Encoding /
//     Accept-Charset headers with q-values (see below). `is` returns the
//     first offered type that matches ("" when none, i.e. Express `false`);
//     the accepts* family returns the best offered type ("" when the client
//     accepts none of them). Extensions ("json", "html", ...) expand both
//     ways ("json" <-> "application/json", "+json" suffixes included).
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::express::request {

namespace detail {

// --- small text helpers -------------------------------------------------------

inline std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

inline std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return std::string();
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

// Case-insensitive header lookup. The server lower-cases names on fill
// (runtime_server.cpp requestHeaders); try the lower-cased name first, then
// the caller's spelling, so a hand-built HttpRequest still resolves.
inline std::string header(const network::HttpRequest& req,
                          const std::string& name) {
    if (!req.headers) return std::string();
    if (auto value = req.headers->get(lowerAscii(name))) return *value;
    if (auto value = req.headers->get(name)) return *value;
    return std::string();
}

// First comma-separated token of a header value (`X-Forwarded-Proto` chains).
inline std::string firstToken(const std::string& value) {
    return trim(value.substr(0, value.find(',')));
}

inline std::vector<std::string> splitLabels(const std::string& host) {
    std::vector<std::string> labels;
    std::size_t start = 0;
    while (start < host.size()) {
        const std::size_t dot = host.find('.', start);
        const std::size_t end = dot == std::string::npos ? host.size() : dot;
        if (end > start) labels.push_back(host.substr(start, end - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return labels;
}

// --- HTTP dates (RFC 7231 §7.1.1.1) -----------------------------------------

inline std::int64_t daysFromCivil(int y, int m, int d) {
    const int yy = y - (m <= 2 ? 1 : 0);
    const std::int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const std::int64_t yoe = yy - era * 400;
    const std::int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline int monthFromName(const std::string& name) {
    static const char* kNames[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                   "jul", "aug", "sep", "oct", "nov", "dec"};
    const std::string short3 = lowerAscii(name.substr(0, 3));
    for (int i = 0; i < 12; ++i) {
        if (short3 == kNames[i]) return i + 1;
    }
    return 0;
}

inline bool digitsAt(const std::string& s, std::size_t i, std::size_t n,
                     int& out) {
    if (i + n > s.size()) return false;
    int value = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const char c = s[i + k];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

// Parses an HTTP date to epoch seconds; std::nullopt on bad input.
inline std::optional<std::int64_t> parseHttpDate(const std::string& text) {
    const std::string s = trim(text);
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    // IMF-fixdate: Sun, 06 Nov 1994 08:49:37 GMT
    // RFC 850:     Sunday, 06-Nov-94 08:49:37 GMT
    // asctime:     Sun Nov  6 08:49:37 1994
    const std::size_t comma = s.find(',');
    if (comma != std::string::npos) {
        const std::string rest = trim(s.substr(comma + 1));
        if (rest.size() < 5) return std::nullopt;
        if (rest[2] == '-') {  // RFC 850, dd-Mon-yy
            if (!digitsAt(rest, 0, 2, day)) return std::nullopt;
            const std::size_t dash2 = rest.find('-', 3);
            if (dash2 == std::string::npos) return std::nullopt;
            month = monthFromName(rest.substr(3, dash2 - 3));
            if (!digitsAt(rest, dash2 + 1, 2, year)) return std::nullopt;
            year += year < 70 ? 2000 : 1900;
            const std::size_t sp = rest.find(' ', dash2 + 1);
            if (sp == std::string::npos) return std::nullopt;
            const std::string time = rest.substr(sp + 1);
            if (!digitsAt(time, 0, 2, hour) || time.size() < 8 ||
                time[2] != ':' || !digitsAt(time, 3, 2, minute) ||
                time[5] != ':' || !digitsAt(time, 6, 2, second)) {
                return std::nullopt;
            }
        } else {  // IMF-fixdate, dd Mon yyyy
            if (!digitsAt(rest, 0, 2, day) || rest.size() < 7) {
                return std::nullopt;
            }
            month = monthFromName(rest.substr(3, 3));
            if (!digitsAt(rest, 7, 4, year)) return std::nullopt;
            const std::size_t sp = rest.find(' ', 11);
            if (sp == std::string::npos) return std::nullopt;
            const std::string time = rest.substr(sp + 1);
            if (!digitsAt(time, 0, 2, hour) || time.size() < 8 ||
                time[2] != ':' || !digitsAt(time, 3, 2, minute) ||
                time[5] != ':' || !digitsAt(time, 6, 2, second)) {
                return std::nullopt;
            }
        }
    } else {  // asctime: Mon dd hh:mm:ss yyyy (day may be space-padded)
        if (s.size() < 20) return std::nullopt;
        month = monthFromName(s.substr(4, 3));
        std::size_t i = 8;
        while (i < s.size() && s[i] == ' ') ++i;
        int parsedDay = 0;
        std::size_t j = i;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') {
            parsedDay = parsedDay * 10 + (s[j] - '0');
            ++j;
        }
        if (j == i || j >= s.size() || s[j] != ' ') return std::nullopt;
        day = parsedDay;
        const std::string time = s.substr(j + 1);
        if (!digitsAt(time, 0, 2, hour) || time.size() < 8 || time[2] != ':' ||
            !digitsAt(time, 3, 2, minute) || time[5] != ':' ||
            !digitsAt(time, 6, 2, second)) {
            return std::nullopt;
        }
        const std::size_t sp = time.find(' ', 8);
        if (sp == std::string::npos ||
            !digitsAt(time, sp + 1, 4, year)) {
            return std::nullopt;
        }
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 60 || year < 1970) {
        return std::nullopt;
    }
    return daysFromCivil(year, month, day) * 86400 + hour * 3600 +
           minute * 60 + second;
}

// Strips a weak-validator prefix and surrounding quotes (ETag comparison).
inline std::string etagValue(const std::string& text) {
    std::string s = trim(text);
    if (s.starts_with("W/")) s = trim(s.substr(2));
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

inline bool etagMatches(const std::string& ifNoneMatch,
                        const std::string& etag) {
    const std::string wanted = etagValue(etag);
    std::size_t start = 0;
    while (start < ifNoneMatch.size()) {
        const std::size_t comma = ifNoneMatch.find(',', start);
        const std::size_t end =
            comma == std::string::npos ? ifNoneMatch.size() : comma;
        const std::string token = trim(ifNoneMatch.substr(start, end - start));
        if (token == "*") return true;
        if (!token.empty() && etagValue(token) == wanted) return true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

// --- content negotiation -----------------------------------------------------

inline std::string mimeForExtension(const std::string& ext) {
    const std::string e = lowerAscii(trim(ext));
    const std::string bare = e.starts_with('.') ? e.substr(1) : e;
    if (bare == "html" || bare == "htm") return "text/html";
    if (bare == "json") return "application/json";
    if (bare == "txt" || bare == "text") return "text/plain";
    if (bare == "xml") return "application/xml";
    if (bare == "htm") return "text/html";
    if (bare == "css") return "text/css";
    if (bare == "js" || bare == "mjs") return "text/javascript";
    if (bare == "csv") return "text/csv";
    if (bare == "svg") return "image/svg+xml";
    if (bare == "png") return "image/png";
    if (bare == "jpg" || bare == "jpeg") return "image/jpeg";
    if (bare == "gif") return "image/gif";
    if (bare == "webp") return "image/webp";
    if (bare == "ico") return "image/x-icon";
    if (bare == "pdf") return "application/pdf";
    if (bare == "wasm") return "application/wasm";
    if (bare == "zip") return "application/zip";
    if (bare == "gz") return "application/gzip";
    if (bare == "mp3") return "audio/mpeg";
    if (bare == "mp4") return "video/mp4";
    if (bare == "webm") return "video/webm";
    if (bare == "ogg") return "audio/ogg";
    if (bare == "wav") return "audio/wav";
    if (bare == "form") return "application/x-www-form-urlencoded";
    if (bare == "urlencoded") return "application/x-www-form-urlencoded";
    return std::string();
}

// Normalizes one offered type: strips parameters/whitespace, lower-cases,
// expands bare extensions. Returns "" for wildcards/invalid shapes the
// caller handles separately.
inline std::string normalizeType(const std::string& offered) {
    std::string s = lowerAscii(trim(offered.substr(0, offered.find(';'))));
    if (s.empty() || s.find('/') != std::string::npos) return s;
    const std::string mime = mimeForExtension(s);
    return mime.empty() ? s : mime;
}

inline bool typeMatches(const std::string& acceptRange,
                        const std::string& offered) {
    if (acceptRange.empty()) return false;
    if (acceptRange == "*/*" || acceptRange == "*") return true;
    const std::size_t slash = acceptRange.find('/');
    if (slash == std::string::npos) return acceptRange == offered;
    if (slash + 1 < acceptRange.size() && acceptRange[slash + 1] == '*') {
        return offered.starts_with(acceptRange.substr(0, slash + 1));
    }
    if (acceptRange == offered) return true;
    // Structured-suffix match: `application/*+json` style ranges and
    // `application/json` covering an offered `application/ld+json` do NOT
    // match (Express `accepts` semantics); only exact/suffix-of-offered.
    const std::size_t oSlash = offered.find('/');
    if (oSlash == std::string::npos) return false;
    if (acceptRange.substr(0, slash) != offered.substr(0, oSlash)) {
        return false;
    }
    const std::string& sub = acceptRange.substr(slash + 1);
    if (sub.starts_with("*+")) {
        return offered.size() > sub.size() - 2 &&
               offered.ends_with(sub.substr(1));
    }
    return false;
}

struct AcceptEntry {
    std::string range;  // normalized, may be a wildcard
    double quality = 1.0;
    std::size_t order = 0;
    int specificity = 0;  // 2 exact, 1 type/*, 0 */*
};

inline std::vector<AcceptEntry> parseAccept(const std::string& headerValue) {
    std::vector<AcceptEntry> entries;
    std::size_t start = 0;
    std::size_t order = 0;
    while (start < headerValue.size()) {
        const std::size_t comma = headerValue.find(',', start);
        const std::size_t end =
            comma == std::string::npos ? headerValue.size() : comma;
        const std::string part = headerValue.substr(start, end - start);
        if (!part.empty()) {
            AcceptEntry entry;
            entry.order = order++;
            const std::size_t semi = part.find(';');
            entry.range =
                normalizeType(trim(part.substr(0, semi)));
            if (entry.range.empty()) entry.range = "*/*";
            // q-value: first `q=` parameter wins; invalid q clamps to 0.
            std::size_t p = semi;
            while (p != std::string::npos && p < part.size()) {
                const std::size_t next = part.find(';', p + 1);
                const std::string param = trim(part.substr(
                    p + 1, next == std::string::npos ? std::string::npos
                                                      : next - p - 1));
                if (param.starts_with("q=") || param.starts_with("Q=")) {
                    try {
                        entry.quality = std::stod(param.substr(2));
                    } catch (...) {
                        entry.quality = 0.0;
                    }
                    entry.quality = std::clamp(entry.quality, 0.0, 1.0);
                    break;
                }
                p = next;
            }
            if (entry.range == "*/*" || entry.range == "*") {
                entry.specificity = 0;
            } else if (entry.range.ends_with("/*")) {
                entry.specificity = 1;
            } else {
                entry.specificity = 2;
            }
            entries.push_back(std::move(entry));
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const AcceptEntry& a, const AcceptEntry& b) {
                         if (a.quality != b.quality) return a.quality > b.quality;
                         if (a.specificity != b.specificity) {
                             return a.specificity > b.specificity;
                         }
                         return a.order < b.order;
                     });
    return entries;
}

// Best offered type for one Accept-style header. Absent header -> the first
// offered type (the client accepts anything). `fallbackWildcard` lets
// encodings/charsets treat a missing header as accepting everything too.
inline std::string negotiate(const std::string& headerValue,
                             const std::vector<std::string>& offered,
                             bool absentAcceptsAll) {
    if (offered.empty()) return std::string();
    if (trim(headerValue).empty()) {
        return absentAcceptsAll ? offered.front() : std::string();
    }
    const std::vector<AcceptEntry> entries = parseAccept(headerValue);
    for (const AcceptEntry& entry : entries) {
        if (entry.quality <= 0.0) continue;  // explicitly refused
        for (const std::string& candidate : offered) {
            if (typeMatches(entry.range, normalizeType(candidate))) {
                return candidate;
            }
        }
    }
    return std::string();
}

}  // namespace detail

// -----------------------------------------------------------------------------
// Options: the router/socket state `HttpRequest` does not carry.
// -----------------------------------------------------------------------------
struct ExpressRequestOptions {
    /// Express `trust proxy`: honour X-Forwarded-Proto / X-Forwarded-Host.
    bool trustProxy = false;
    /// The socket is TLS-terminated (https). Plain HTTP server: false.
    bool tls = false;
    /// Labels dropped from the right of the hostname for `subdomains`.
    int subdomainOffset = 2;
    /// Router mount point; "" means "no mount" (Express default).
    std::string baseUrl;
    /// Full original URL incl. query; "" falls back to `path`.
    std::string originalUrl;
    /// The response ETag / Last-Modified the `fresh` check compares against
    /// (Express compares against the res headers; set them before reading
    /// `req.fresh`). Empty = not set.
    std::string responseEtag;
    std::string responseLastModified;
};

// -----------------------------------------------------------------------------
// Properties
// -----------------------------------------------------------------------------

inline std::string protocol(const network::HttpRequest& req,
                            const ExpressRequestOptions& opts = {}) {
    if (opts.trustProxy) {
        const std::string forwarded =
            detail::firstToken(detail::header(req, "x-forwarded-proto"));
        if (!forwarded.empty()) return detail::lowerAscii(forwarded);
    }
    return opts.tls ? "https" : "http";
}

inline bool secure(const network::HttpRequest& req,
                   const ExpressRequestOptions& opts = {}) {
    return protocol(req, opts) == "https";
}

inline std::string hostname(const network::HttpRequest& req,
                            const ExpressRequestOptions& opts = {}) {
    std::string host;
    if (opts.trustProxy) {
        host = detail::firstToken(detail::header(req, "x-forwarded-host"));
    }
    if (host.empty()) host = detail::header(req, "host");
    host = detail::trim(host);
    if (host.starts_with("[")) {  // [IPv6]:port
        const std::size_t close = host.find(']');
        if (close != std::string::npos) return host.substr(1, close - 1);
        return host;
    }
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    return host;
}

inline std::vector<std::string> subdomains(
    const network::HttpRequest& req, const ExpressRequestOptions& opts = {}) {
    const std::vector<std::string> labels =
        detail::splitLabels(hostname(req, opts));
    const std::size_t keep = opts.subdomainOffset < 0
                                 ? 0
                                 : static_cast<std::size_t>(opts.subdomainOffset);
    if (labels.size() <= keep) return {};
    return std::vector<std::string>(labels.begin(),
                                    labels.end() - static_cast<std::ptrdiff_t>(keep));
}

inline std::string baseUrl(const network::HttpRequest& req,
                           const ExpressRequestOptions& opts = {}) {
    (void)req;
    return opts.baseUrl;
}

inline std::string originalUrl(const network::HttpRequest& req,
                               const ExpressRequestOptions& opts = {}) {
    if (!opts.originalUrl.empty()) return opts.originalUrl;
    return req.path;
}

inline std::string path(const network::HttpRequest& req,
                        const ExpressRequestOptions& = {}) {
    return req.path;
}

inline bool fresh(const network::HttpRequest& req,
                  const ExpressRequestOptions& opts = {}) {
    if (req.method != "GET" && req.method != "HEAD") return false;
    const std::string ifNoneMatch = detail::header(req, "if-none-match");
    if (!ifNoneMatch.empty()) {
        if (opts.responseEtag.empty()) return false;
        return detail::etagMatches(ifNoneMatch, opts.responseEtag);
    }
    const std::string ifModifiedSince =
        detail::header(req, "if-modified-since");
    if (ifModifiedSince.empty()) return false;
    if (opts.responseLastModified.empty()) return false;
    const auto since = detail::parseHttpDate(ifModifiedSince);
    const auto modified = detail::parseHttpDate(opts.responseLastModified);
    if (!since || !modified) return false;
    return *modified <= *since;
}

inline bool stale(const network::HttpRequest& req,
                  const ExpressRequestOptions& opts = {}) {
    return !fresh(req, opts);
}

inline bool xhr(const network::HttpRequest& req,
                const ExpressRequestOptions& = {}) {
    return detail::lowerAscii(detail::trim(detail::header(req, "x-requested-with"))) ==
           "xmlhttprequest";
}

// -----------------------------------------------------------------------------
// Methods
// -----------------------------------------------------------------------------

// `req.is(...types)`: the first offered type matching the request's
// Content-Type, or "" (Express `false`). No Content-Type -> "".
inline std::string is(const network::HttpRequest& req,
                      const std::vector<std::string>& types) {
    const std::string contentType = detail::lowerAscii(detail::header(req, "content-type"));
    if (contentType.empty()) return std::string();
    const std::string mime = detail::normalizeType(contentType);
    for (const std::string& offered : types) {
        const std::string wanted = detail::normalizeType(offered);
        if (detail::typeMatches(wanted, mime)) return offered;
    }
    return std::string();
}

inline std::string is(const network::HttpRequest& req, const std::string& type) {
    return is(req, std::vector<std::string>{type});
}

inline std::string accepts(const network::HttpRequest& req,
                           const std::vector<std::string>& types) {
    return detail::negotiate(detail::header(req, "accept"), types, true);
}

inline std::string acceptsEncodings(
    const network::HttpRequest& req, const std::vector<std::string>& encodings) {
    const std::string headerValue = detail::header(req, "accept-encoding");
    if (detail::trim(headerValue).empty()) {
        return encodings.empty() ? std::string() : encodings.front();
    }
    // Accept-Encoding tokens are bare names ("gzip"), not MIME types; reuse
    // the q-value machinery via exact-token matching.
    std::vector<std::string> lowered;
    lowered.reserve(encodings.size());
    for (const std::string& e : encodings) {
        lowered.push_back(detail::lowerAscii(detail::trim(e)));
    }
    const std::string best = detail::negotiate(headerValue, lowered, true);
    if (best.empty()) return std::string();
    for (std::size_t i = 0; i < lowered.size(); ++i) {
        if (lowered[i] == best) return encodings[i];
    }
    return best;
}

inline std::string acceptsCharsets(
    const network::HttpRequest& req, const std::vector<std::string>& charsets) {
    const std::string headerValue = detail::header(req, "accept-charset");
    if (detail::trim(headerValue).empty()) {
        return charsets.empty() ? std::string() : charsets.front();
    }
    std::vector<std::string> lowered;
    lowered.reserve(charsets.size());
    for (const std::string& c : charsets) {
        lowered.push_back(detail::lowerAscii(detail::trim(c)));
    }
    const std::string best = detail::negotiate(headerValue, lowered, true);
    if (best.empty()) return std::string();
    for (std::size_t i = 0; i < lowered.size(); ++i) {
        if (lowered[i] == best) return charsets[i];
    }
    return best;
}

// -----------------------------------------------------------------------------
// ExpressRequest view: HttpRequest + options, Express-shaped member calls.
// Generated code uses the free functions above (default options); this view
// exists for hand-written C++ and for the router layer that owns the state.
// -----------------------------------------------------------------------------
class ExpressRequest {
public:
    ExpressRequest(const network::HttpRequest& req,
                   ExpressRequestOptions options = {})
        : req_(&req), options_(std::move(options)) {}

    std::string protocol() const { return protocol(*req_, options_); }
    bool secure() const { return secure(*req_, options_); }
    std::string hostname() const { return hostname(*req_, options_); }
    std::vector<std::string> subdomains() const {
        return subdomains(*req_, options_);
    }
    std::string baseUrl() const { return baseUrl(*req_, options_); }
    std::string originalUrl() const { return originalUrl(*req_, options_); }
    std::string path() const { return path(*req_, options_); }
    bool fresh() const { return fresh(*req_, options_); }
    bool stale() const { return stale(*req_, options_); }
    bool xhr() const { return xhr(*req_, options_); }
    std::string is(const std::vector<std::string>& types) const {
        return is(*req_, types);
    }
    std::string accepts(const std::vector<std::string>& types) const {
        return accepts(*req_, types);
    }
    std::string acceptsEncodings(
        const std::vector<std::string>& encodings) const {
        return acceptsEncodings(*req_, encodings);
    }
    std::string acceptsCharsets(
        const std::vector<std::string>& charsets) const {
        return acceptsCharsets(*req_, charsets);
    }

    const network::HttpRequest& request() const { return *req_; }
    const ExpressRequestOptions& options() const { return options_; }

private:
    const network::HttpRequest* req_;
    ExpressRequestOptions options_;
};

}  // namespace cp3::runtime::express::request
