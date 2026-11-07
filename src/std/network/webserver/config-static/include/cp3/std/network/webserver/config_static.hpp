// =============================================================================
// cp3/std/network/webserver/config_static.hpp - createServer() config for
// static files + body limits (+ trust proxy / timeouts), as one object.
//
// OWNERSHIP: the std/network/webserver/config-static family
// (src/std/network/webserver/config-static/**). Header-only (D14): every
// function is `inline`, there is no runtime TU to link. Nothing here is
// included by generated code until the integrator lands WIRING.md.
//
// -----------------------------------------------------------------------------
// CP3 SHAPE (the whole surface of this family)
// -----------------------------------------------------------------------------
//   createServer({
//     threads?: number,                                  // existing, preserved
//     static?:    { dir: string, index?: string|boolean, etag?: boolean,
//                   maxAge?: number, fallthrough?: boolean },
//     bodyLimit?: number | string,                       // bytes / "100kb"
//     json?:      { limit?: number|string, strict?: boolean },
//     urlencoded?:{ extended?: boolean, limit?: number|string },
//     trustProxy?: boolean | number | string | string[],
//     timeouts?:  { request?: number, headers?: number,
//                   idle?: number, keepAlive?: number },  // ms, 0 = disabled
//   })
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * Presence enables: `static` installs the static middleware, `json` /
//     `urlencoded` install the matching body_parse parser. Omitting a key
//     installs nothing for it (no parser is silently added).
//   * Middleware order (registration order on the server): global
//     bodyLimit, then json, then urlencoded, then static. Static is
//     GET/HEAD-only and the parsers are content-type gated, so for a plain
//     GET the parsers are no-ops and static answers directly.
//   * Limit precedence: a parser's own `limit` wins; when absent it
//     inherits the global `bodyLimit`; when neither is set the body_parse
//     default ("100kb") applies. A global `bodyLimit` with no parser still
//     installs makeBodyLimitMiddleware (413 over the limit before routing).
//   * `trustProxy` is not middleware: it is stored into the settings table
//     ("trust proxy" key, see webserver/settings) by applyToSettings() and
//     takes effect for req.ip / req.protocol only when the dispatcher runs
//     the request through webserver::RequestScope (settings family).
//   * `timeouts` has no socket to act on from this header: HttpServer
//     exposes no timeout knob, so the values are validated, stored on the
//     config, and snapshotted by lastCreateServerConfig() for the
//     integrator's server glue (e.g. a uWS idleTimeout mapping). They are
//     NOT enforced by middleware - see LIMITATIONS in WIRING.md.
//   * Static: index resolution, ETag (weak) + If-None-Match 304,
//     Cache-Control from maxAge, and fallthrough vs 404 are implemented
//     here (makeStaticMiddleware(dir) alone cannot express them). Path
//     traversal outside `dir` is refused (falls through / 404s, never
//     serves). Dotfiles are served like any other file (serve-static's
//     dotfiles:"ignore" default is NOT applied - see LIMITATIONS).
// =============================================================================
#pragma once

#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/std/network.hpp"
#include "cp3/std/network/webserver/body_parse.hpp"
#include "cp3/std/network/webserver/settings.hpp"

namespace cp3::runtime::network::webserver::config_static {

// ---------------------------------------------------------------------------
// Config structs (one row per CP3 key; codegen builds these field by field)
// ---------------------------------------------------------------------------

/// `static: { dir, index?, etag?, maxAge?, fallthrough? }`.
struct StaticConfig {
    bool enabled = false;             // `static` key present
    std::string dir;                  // required when enabled
    std::string index = "index.html"; // directory index file name
    bool serveIndex = true;           // `index: false` disables index files
    bool etag = true;                 // weak ETag + If-None-Match 304
    double maxAge = 0.0;              // seconds; <= 0 = no Cache-Control
    bool fallthrough = true;          // miss -> next(), else 404
};

/// `json: { limit?, strict? }` (body_parse JsonBodyOptions + enable flag).
struct JsonConfig {
    bool enabled = false;
    bool hasLimit = false;
    double limitBytes = 0.0;  // bytes; <= 0 = unlimited (body_parse rule)
    bool strict = true;
};

/// `urlencoded: { extended?, limit? }`.
struct UrlEncodedConfig {
    bool enabled = false;
    bool hasLimit = false;
    double limitBytes = 0.0;
    bool extended = true;  // qs brackets; false = querystring flat
};

/// `timeouts: { request?, headers?, idle?, keepAlive? }`, all in
/// milliseconds, 0 = disabled. Stored + validated only (see header note).
struct ServerTimeouts {
    double requestMs = 0.0;
    double headersMs = 0.0;
    double idleMs = 0.0;
    double keepAliveMs = 0.0;
};

/// The whole `createServer({...})` options object.
struct CreateServerConfig {
    double threads = 0.0;  // 0 = unspecified (single loop, D79 rule)
    StaticConfig static_;
    bool hasBodyLimit = false;
    double bodyLimitBytes = 0.0;  // global limit (makeBodyLimitMiddleware rule)
    JsonConfig json;
    UrlEncodedConfig urlencoded;
    bool hasTrustProxy = false;
    webserver::SettingValue trustProxy;  // bool | number | string | string[]
    ServerTimeouts timeouts;
};

// ---------------------------------------------------------------------------
// Validation (pure; sema mirrors the key tables, runtime enforces values)
// ---------------------------------------------------------------------------

inline std::vector<std::string> validateConfig(const CreateServerConfig& cfg) {
    std::vector<std::string> errors;
    const auto bad = [&errors](const std::string& what, const std::string& why) {
        errors.push_back("createServer() " + what + " " + why);
    };
    if (cfg.threads < 0.0 || (cfg.threads > 0.0 && !std::isfinite(cfg.threads))) {
        bad("option 'threads'", "must be a number >= 1 (0 = unspecified)");
    }
    if (cfg.static_.enabled && cfg.static_.dir.empty()) {
        bad("option 'static.dir'", "is required when 'static' is present");
    }
    if (cfg.static_.maxAge < 0.0 || !std::isfinite(cfg.static_.maxAge)) {
        bad("option 'static.maxAge'", "must be a non-negative number of seconds");
    }
    if (cfg.static_.serveIndex && cfg.static_.index.empty()) {
        bad("option 'static.index'", "must not be an empty string (use false to disable)");
    }
    if (cfg.hasBodyLimit && cfg.bodyLimitBytes < 0.0) {
        bad("option 'bodyLimit'", "must be a non-negative byte count or a bytes string");
    }
    if (cfg.json.hasLimit && cfg.json.limitBytes < 0.0) {
        bad("option 'json.limit'", "must be a non-negative byte count or a bytes string");
    }
    if (cfg.urlencoded.hasLimit && cfg.urlencoded.limitBytes < 0.0) {
        bad("option 'urlencoded.limit'", "must be a non-negative byte count or a bytes string");
    }
    const auto timeout = [&bad](const char* key, double value) {
        if (value < 0.0 || !std::isfinite(value)) {
            bad(std::string("option 'timeouts.") + key + "'",
                "must be a non-negative number of milliseconds (0 = disabled)");
        }
    };
    timeout("request", cfg.timeouts.requestMs);
    timeout("headers", cfg.timeouts.headersMs);
    timeout("idle", cfg.timeouts.idleMs);
    timeout("keepAlive", cfg.timeouts.keepAliveMs);
    return errors;
}

/// Throws `Error` naming the first problem (the dispatcher answers 500 /
/// onError, like any other construction-time failure).
inline void ensureValid(const CreateServerConfig& cfg) {
    const std::vector<std::string> errors = validateConfig(cfg);
    if (!errors.empty()) throw Error(errors.front());
}

/// Effective parser limit: parser's own limit, else the global bodyLimit,
/// else the body_parse default. (body_parse semantics: <= 0 = unlimited.)
inline double effectiveParserLimit(bool parserHasLimit, double parserLimit,
                                   const CreateServerConfig& cfg) {
    if (parserHasLimit) return parserLimit;
    if (cfg.hasBodyLimit) return cfg.bodyLimitBytes;
    return body_parse::kDefaultLimitBytes;
}

// ---------------------------------------------------------------------------
// Static middleware (index / etag / maxAge / fallthrough; own implementation
// because makeStaticMiddleware(dir) cannot express those options)
// ---------------------------------------------------------------------------

namespace detail {

inline std::string weakEtag(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    const auto mtime = std::filesystem::last_write_time(file, ec);
    const auto ticks = ec ? 0 : mtime.time_since_epoch().count();
    const std::string body = std::to_string(size) + "-" + std::to_string(ticks);
    return "W/\"" + body + "\"";
}

/// Case-insensitive request header lookup (server stores names lowercased,
/// but a hand-built request may not).
inline std::string requestHeader(const HttpRequest& req, const std::string& name) {
    if (!req.headers) return "";
    std::string wanted = name;
    for (char& c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // `entries()` returns an owning `Array` (a `Ref`); keep it in a named
    // local so the vector outlives the loop (no deref of a prvalue Ref).
    const auto entries = req.headers->entries();
    for (const auto& entry : *entries) {
        std::string key = entry.first;
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (key == wanted) return entry.second;
    }
    return "";
}

}  // namespace detail

/// The `static` middleware for one StaticConfig. GET/HEAD only; a miss
/// (no such file, traversal outside `dir`, a directory with index disabled
/// or without the index file) either continues the chain (fallthrough) or
/// answers 404. A hit answers through res.sendFile with ETag /
/// Cache-Control applied first; a matching If-None-Match answers 304.
inline MiddlewareFn makeConfigStaticMiddleware(StaticConfig options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res,
                                          Next& next) mutable -> Promise<void> {
        if (req.method != "GET" && req.method != "HEAD") {
            next();
            return Promise<void>::resolve();
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path base = fs::weakly_canonical(fs::path(options.dir), ec);
        if (ec) {
            if (!options.fallthrough) res.status(404).text("Not Found");
            else next();
            return Promise<void>::resolve();
        }
        fs::path candidate =
            fs::weakly_canonical(base / fs::path(req.path).lexically_normal(), ec);
        if (ec) {
            if (!options.fallthrough) res.status(404).text("Not Found");
            else next();
            return Promise<void>::resolve();
        }
        const std::string b = base.string(), c = candidate.string();
        const bool inside = c == b || (c.size() > b.size() &&
                                       c.compare(0, b.size(), b) == 0 && c[b.size()] == '/');
        if (!inside) {
            if (!options.fallthrough) res.status(404).text("Not Found");
            else next();
            return Promise<void>::resolve();
        }
        if (fs::is_directory(candidate, ec)) {
            if (!options.serveIndex) {
                if (!options.fallthrough) res.status(404).text("Not Found");
                else next();
                return Promise<void>::resolve();
            }
            candidate /= options.index;
        }
        if (!fs::is_regular_file(candidate, ec)) {
            if (!options.fallthrough) res.status(404).text("Not Found");
            else next();
            return Promise<void>::resolve();
        }
        if (options.etag) {
            const std::string etag = detail::weakEtag(candidate);
            res.header("ETag", etag);
            const std::string inm = detail::requestHeader(req, "if-none-match");
            if (!inm.empty() && (inm == "*" || inm == etag)) {
                res.status(304).send();
                return Promise<void>::resolve();
            }
        }
        if (options.maxAge > 0) {
            res.header("Cache-Control",
                       "public, max-age=" +
                           std::to_string(static_cast<long long>(options.maxAge)));
        }
        res.sendFile(candidate.string());
        return Promise<void>::resolve();
    };
}

// ---------------------------------------------------------------------------
// Composition: config -> middleware chain -> server
// ---------------------------------------------------------------------------

/// The middleware chain for one config, in registration order:
/// bodyLimit, json, urlencoded, static (only the enabled pieces).
inline std::vector<MiddlewareFn> middlewaresFor(const CreateServerConfig& cfg) {
    ensureValid(cfg);
    std::vector<MiddlewareFn> chain;
    if (cfg.hasBodyLimit) {
        chain.push_back(makeBodyLimitMiddleware(cfg.bodyLimitBytes));
    }
    if (cfg.json.enabled) {
        body_parse::JsonBodyOptions opts;
        opts.limitBytes = effectiveParserLimit(cfg.json.hasLimit, cfg.json.limitBytes, cfg);
        opts.strict = cfg.json.strict;
        chain.push_back(body_parse::makeJsonMiddleware(opts));
    }
    if (cfg.urlencoded.enabled) {
        body_parse::UrlEncodedBodyOptions opts;
        opts.limitBytes =
            effectiveParserLimit(cfg.urlencoded.hasLimit, cfg.urlencoded.limitBytes, cfg);
        opts.extended = cfg.urlencoded.extended;
        chain.push_back(body_parse::makeUrlEncodedMiddleware(opts));
    }
    if (cfg.static_.enabled) {
        chain.push_back(makeConfigStaticMiddleware(cfg.static_));
    }
    return chain;
}

/// Installs the chain on `app` (app.use per middleware, in order).
inline void installCreateServerConfig(const HttpServer& app,
                                      const CreateServerConfig& cfg) {
    for (const MiddlewareFn& mw : middlewaresFor(cfg)) app.use(mw);
}

/// Stores `trustProxy` into the settings table under "trust proxy" (the
/// settings family's key). No-op when the key was absent.
inline void applyToSettings(const CreateServerConfig& cfg,
                            webserver::Settings& settings) {
    if (cfg.hasTrustProxy) settings.set("trust proxy", cfg.trustProxy);
}

inline CreateServerConfig& lastCreateServerConfigSlot() {
    static thread_local CreateServerConfig last;
    return last;
}

/// The most recent config handed to createServerWithConfig on this thread
/// (timeouts + trustProxy live here for the server glue; the HttpServer
/// itself carries only routes).
inline const CreateServerConfig& lastCreateServerConfig() {
    return lastCreateServerConfigSlot();
}

/// `createServer({...})` as one call: validates, constructs the server
/// (scaled when threads > 1, D79), installs the middleware chain, and
/// snapshots the config for lastCreateServerConfig(). This is the single
/// codegen target for the object form (see WIRING.md).
inline HttpServer createServerWithConfig(const CreateServerConfig& cfg) {
    ensureValid(cfg);
    HttpServer app = cfg.threads > 0 ? HttpServer(cfg.threads) : HttpServer();
    installCreateServerConfig(app, cfg);
    lastCreateServerConfigSlot() = cfg;
    return app;
}

// ---------------------------------------------------------------------------
// sema / codegen helpers (pure string tables; the splices live in WIRING.md)
// ---------------------------------------------------------------------------
namespace sema_codegen {

/// Top-level createServer() option keys (threads is the pre-existing key).
inline bool isTopLevelKey(const std::string& key) {
    return key == "threads" || key == "static" || key == "bodyLimit" ||
           key == "json" || key == "urlencoded" || key == "trustProxy" ||
           key == "timeouts";
}

/// Sub-keys of `static: { ... }`.
inline bool isStaticKey(const std::string& key) {
    return key == "dir" || key == "index" || key == "etag" || key == "maxAge" ||
           key == "fallthrough";
}

/// Sub-keys of `json: { ... }`.
inline bool isJsonKey(const std::string& key) {
    return key == "limit" || key == "strict";
}

/// Sub-keys of `urlencoded: { ... }`.
inline bool isUrlEncodedKey(const std::string& key) {
    return key == "extended" || key == "limit";
}

/// Sub-keys of `timeouts: { ... }` (milliseconds).
inline bool isTimeoutKey(const std::string& key) {
    return key == "request" || key == "headers" || key == "idle" ||
           key == "keepAlive";
}

/// Diagnostic for an unknown key ("" when supported). `group` names the
/// object the key appeared in ("createServer", "static", "json",
/// "urlencoded", "timeouts").
inline std::string validateOptionKey(const std::string& group,
                                     const std::string& key) {
    bool known = false;
    if (group == "createServer") known = isTopLevelKey(key);
    else if (group == "static") known = isStaticKey(key);
    else if (group == "json") known = isJsonKey(key);
    else if (group == "urlencoded") known = isUrlEncodedKey(key);
    else if (group == "timeouts") known = isTimeoutKey(key);
    if (known) return "";
    return "unknown 'createServer() " + group + "' option '" + key + "'";
}

/// C++ call text for the whole options object: the createServerWithConfig
/// target (codegen builds `<configExpr>` field by field; see WIRING.md).
inline std::string createServerCallText(const std::string& configExpr) {
    return "::cp3::runtime::network::webserver::config_static::createServerWithConfig(" +
           configExpr + ")";
}

/// C++ text for a `limit` value: a string literal goes through
/// body_parse::parseLimitText ("100kb"), anything else is bytes already.
inline std::string limitValueText(const std::string& valueExpr, bool isStringLiteral) {
    if (!isStringLiteral) return valueExpr;
    return "::cp3::runtime::network::webserver::body_parse::parseLimitText(" + valueExpr + ")";
}

}  // namespace sema_codegen

}  // namespace cp3::runtime::network::webserver::config_static
