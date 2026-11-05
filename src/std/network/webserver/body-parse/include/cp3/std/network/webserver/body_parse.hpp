// =============================================================================
// src/std/network/webserver/body-parse/include/cp3/std/network/webserver/body_parse.hpp
// webserver `body-parser` equivalents for `std/network`: webserver.json(),
// webserver.urlencoded(), webserver.text() and webserver.raw() as app.use()
// middleware, plus the `req.body` accessors the parsers feed.
//
// OWNERSHIP: src/std/network/webserver/body-parse/** ONLY. This header is
// self-contained (it only needs the public cp3/std/network.hpp contract plus
// the std/json and std/url headers) and it does NOT modify the existing
// HttpRequest / Next / dispatcher surface: the integrator wires it in with
// the exact lines in README.md (WIRING). Nothing here is included by
// generated code until that wiring lands.
//
// webserver MAPPING (body-parser 1.x / webserver 4 parity unless noted):
//   * webserver.json()            -> makeJsonMiddleware()
//     application/json (and any `*+json`) only; `strict` (default true)
//     requires a top-level object/array; empty body -> {}; the parsed value
//     lands on req.body (see parsedBody()).
//   * webserver.urlencoded()      -> makeUrlEncodedMiddleware()
//     application/x-www-form-urlencoded only; `extended` (default true) is
//     the qs bracket grammar (`a[b]=1`, `a[]=1`, `a[0]=1`); `extended: false`
//     is the Node querystring flat grammar. Values are strings (qs keeps
//     everything a string; repeated keys become arrays).
//   * webserver.text()            -> makeTextMiddleware()
//     text/plain only by default; req.body is the raw string.
//   * webserver.raw()             -> makeRawMiddleware()
//     application/octet-stream only by default; req.body is the raw bytes
//     (rawBodyText() / rawBodyBuffer()).
//   * every parser honours `limit` (bytes as a number, or a bytes.js string
//     such as "100kb" / "1.5mb" / "512b"; default "100kb") and `type`
//     (exact media type, or "*/*" for any). A content-type mismatch skips
//     the parser untouched, exactly like body-parser.
//   * parser errors (malformed JSON, strict-mode violation, limit exceeded)
//     fail the request with 400 (413 for the limit) WITHOUT calling next():
//     the stock dispatcher has no next(err) overload, so a thrown error
//     would land in app.onError()/500. The error value is still available
//     as BodyParseError for the next(err) wiring (see README WIRING).
//
// STORAGE MODEL (why no HttpRequest change is needed):
//   HttpRequest owns one buffered body string (setBody/text/bodySize) and no
//   parsed slot. A successful json/urlencoded parse therefore writes the
//   CANONICAL JSON of the parsed value back with setBody(), and the
//   req.body accessors below re-read it (parsedBody reparses the canonical
//   text into a json::Value). text/raw leave the body byte-for-byte, so
//   req.text()/req.json<T>() keep working downstream unchanged. See
//   LIMITATIONS in README.md for the consequences (number reformatting,
//   re-parse cost, double-parser guard).
// =============================================================================
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/network.hpp"
#include "cp3/std/buffer.hpp"

namespace cp3::runtime::network::webserver::body_parse {

// body-parser's default limit ("100kb").
inline constexpr double kDefaultLimitBytes = 100.0 * 1024.0;

// ---------------------------------------------------------------------------
// Options (one row per parser; codegen builds these from the options object)
// ---------------------------------------------------------------------------

/// `webserver.json({ limit?, strict?, type? })`.
struct JsonBodyOptions {
    double limitBytes = kDefaultLimitBytes;  // <= 0 = unlimited (deviation,
                                             // see README LIMITATIONS)
    bool strict = true;                      // top-level object/array only
    std::string type;                        // "" = application/json (+ *+json)
};

/// `webserver.urlencoded({ limit?, extended?, type? })`.
struct UrlEncodedBodyOptions {
    double limitBytes = kDefaultLimitBytes;
    bool extended = true;  // qs brackets; false = querystring flat
    std::string type;      // "" = application/x-www-form-urlencoded
};

/// `webserver.text({ limit?, type? })`.
struct TextBodyOptions {
    double limitBytes = kDefaultLimitBytes;
    std::string type;  // "" = text/plain
};

/// `webserver.raw({ limit?, type? })`.
struct RawBodyOptions {
    double limitBytes = kDefaultLimitBytes;
    std::string type;  // "" = application/octet-stream
};

// ---------------------------------------------------------------------------
// limit + content-type helpers (pure; also used by the codegen snippets)
// ---------------------------------------------------------------------------

/// Parses a bytes.js-style limit: a number with an optional unit
/// (b, kb, mb, gb, tb, pb; 1024-based, case-insensitive, decimals allowed).
/// "100" -> 100 bytes. Throws std::invalid_argument on garbage.
double parseLimitText(const std::string& text);

/// The bare media type of a Content-Type header value: parameters ("; ...")
/// stripped, trimmed, lowercased. "" when the header is absent/empty.
std::string mediaTypeOf(const std::string& contentTypeHeader);

/// True for `application/json` and any `application/*+json` media type.
bool isJsonContentType(const std::string& contentTypeHeader);
/// True for exactly `application/x-www-form-urlencoded`.
bool isUrlEncodedContentType(const std::string& contentTypeHeader);

/// Case-insensitive request header lookup ("content-type" et al).
std::string headerValue(const HttpRequest& req, const std::string& name);

// ---------------------------------------------------------------------------
// Pure parsers (no request state; throw json::ParseError / propagate)
// ---------------------------------------------------------------------------

/// Parses one JSON body. Empty/whitespace-only text yields {} (body-parser).
/// With `strict`, a top-level scalar/null is a ParseError. Throws
/// json::ParseError on malformed JSON.
::cp3::runtime::json::Value parseJsonBody(const std::string& text, bool strict);

/// One decoded `key=value` form pair, in order (duplicates kept).
using FormPairs = std::vector<std::pair<std::string, std::string>>;
/// Splits + decodes an application/x-www-form-urlencoded body. `+` is a
/// space; `%HH` decoding reuses std/url percentDecode and is permissive
/// (malformed sequences stay literal, like URLSearchParams).
FormPairs parseFormPairs(const std::string& text);

/// Parses a form body into the value tree req.body exposes: an object of
/// strings (repeated keys -> arrays). `extended` enables the qs bracket
/// grammar (depth 5, array limit 20, parameter limit 1000, like qs).
/// Never throws: undecodable input stays literal.
::cp3::runtime::json::Value parseUrlEncodedBody(const std::string& text,
                                                bool extended);

// ---------------------------------------------------------------------------
// req.body accessors (read the canonical body the middleware stored)
// ---------------------------------------------------------------------------

/// The buffered body text, synchronously (HttpRequest::text() is an already
/// resolved promise; this is its value).
std::string rawBodyText(const HttpRequest& req);
/// The same bytes as a std/buffer Buffer (webserver.raw() downstream shape).
buffer::Buffer rawBodyBuffer(const HttpRequest& req);
/// `req.body` as a value tree: the parsed JSON/form object, the raw string
/// for text/raw bodies, {} for an empty body. A non-JSON non-empty body
/// reads back as a string Value (never throws).
::cp3::runtime::json::Value parsedBody(const HttpRequest& req);
/// `req.body` flattened to Map<string, string> for Map-shaped handlers:
/// string fields verbatim, numbers/booleans in JSON spelling, other values
/// in compact JSON. A non-object body yields an empty Map.
StringMap formBodyMap(const HttpRequest& req);

// ---------------------------------------------------------------------------
// Error value (the next(err) shape; the middleware answers 400/413 directly
// until the dispatcher grows a Next(err) overload -- see README WIRING)
// ---------------------------------------------------------------------------

/// A parser failure: `status` is 400 (malformed) or 413 (limit exceeded),
/// `code` is the body-parser machine code ("EBADJSON" / "EPARSE" /
/// "ELIMIT"), `message` is client-safe and is what the middleware sends.
struct BodyParseError {
    int status = 400;
    std::string code;
    std::string message;

    Thrown toThrown() const { return Thrown::make(*this); }
    static bool is(Thrown const& t) { return t.get<BodyParseError>() != nullptr; }
    static BodyParseError fromThrown(Thrown const& t, int fallback = 400);
};

// ---------------------------------------------------------------------------
// Middleware factories (MiddlewareFn: drop into app.use(...))
// ---------------------------------------------------------------------------

/// `app.use(webserver.json())` / `app.use(webserver.json({ limit: "1mb" }))`.
MiddlewareFn makeJsonMiddleware(JsonBodyOptions options = {});
/// `app.use(webserver.urlencoded({ extended: false }))`.
MiddlewareFn makeUrlEncodedMiddleware(UrlEncodedBodyOptions options = {});
/// `app.use(webserver.text())`.
MiddlewareFn makeTextMiddleware(TextBodyOptions options = {});
/// `app.use(webserver.raw())`.
MiddlewareFn makeRawMiddleware(RawBodyOptions options = {});

// Short aliases spelling the webserver.* factory names (codegen targets).
inline MiddlewareFn jsonParser(JsonBodyOptions options = {}) {
    return makeJsonMiddleware(std::move(options));
}
inline MiddlewareFn urlEncodedParser(UrlEncodedBodyOptions options = {}) {
    return makeUrlEncodedMiddleware(std::move(options));
}
inline MiddlewareFn textParser(TextBodyOptions options = {}) {
    return makeTextMiddleware(std::move(options));
}
inline MiddlewareFn rawParser(RawBodyOptions options = {}) {
    return makeRawMiddleware(std::move(options));
}

// ---------------------------------------------------------------------------
// sema / codegen helpers (pure string tables; the splices live in README.md)
// ---------------------------------------------------------------------------
namespace sema_codegen {

/// True for the four parser factory names (bare `json(urlencoded(...))`
/// calls and the `webserver.json()` namespace spelling alike).
inline bool isBodyParserName(const std::string& name) {
    return name == "json" || name == "urlencoded" || name == "text" ||
           name == "raw";
}

/// C++ call text for a parser factory: `makeJsonMiddleware(<options>)`
/// (`<options>` already built by the options-object walker; "0" for none).
std::string middlewareCallText(const std::string& name,
                               const std::string& optionsExpr);

/// C++ read text for `req.body`: parsedBody(<req>).
std::string parsedBodyCallText(const std::string& reqExpr);

/// Diagnostic for an unknown options key ("" when the key is supported).
/// json/text/raw share { limit, type } (+ strict for json); urlencoded adds
/// extended. `inflate` / `verify` are recognised but reported as unsupported
/// (no zlib in this build; see README LIMITATIONS).
std::string validateOptionKey(const std::string& parser,
                              const std::string& key);

}  // namespace sema_codegen

}  // namespace cp3::runtime::network::webserver::body_parse
