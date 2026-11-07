// =============================================================================
// src/std/network/runtime/runtime_web.cpp - Ledger L web surface.
//
// OWNERSHIP: the std/network family (src/std/network/**). Implements the
// appended cp3/std/network.hpp surface: Headers, FormData, AbortSignal (value
// only; the fetch transport honours its timeout), Request/Response,
// fetch(Request), withFormData, and the static/cors/bodyLimit middleware
// factories. No new backend: static file bytes go through the existing
// HttpResponse::sendFile, fetch through the existing fetch(url, init).
// =============================================================================
#include "cp3/std/network.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cp3::runtime::network {
namespace {

std::string lowerAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// uWS consumes TLS material SYNCHRONOUSLY at context creation and only
/// supports FILE paths (never inline PEM), so cert/key must exist and carry
/// PEM markers - fail closed with a precise message before any socket exists.
void readTlsPemFile(const std::string& path, const char* field, const char* marker) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Thrown::make(std::string("[ERR_TLS_INVALID_OPTIONS] tls.") + field +
                           " file not found or unreadable: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();
    if (content.find(marker) == std::string::npos) {
        throw Thrown::make(std::string("[ERR_TLS_INVALID_OPTIONS] tls.") + field +
                           " file does not contain '" + marker + "': " + path);
    }
}

}  // namespace

// --- Headers -----------------------------------------------------------------
Headers::Headers(const StringMap& map) {
    if (map) {
        // `entries()` returns an owning `Array` (a `Ref`); keep it in a named
        // local so the vector outlives the loop (no deref of a prvalue Ref).
        const auto entries = map->entries();
        for (const auto& [k, v] : *entries) entries_.emplace_back(k, v);
    }
}
Headers::Headers(std::vector<std::pair<std::string, std::string>> pairs)
    : entries_(std::move(pairs)) {}
std::string Headers::get(const std::string& name) const {
    const std::string want = lowerAscii(name);
    for (const auto& [k, v] : entries_) if (lowerAscii(k) == want) return v;
    return {};
}
bool Headers::has(const std::string& name) const {
    const std::string want = lowerAscii(name);
    for (const auto& [k, v] : entries_) { (void)v; if (lowerAscii(k) == want) return true; }
    return false;
}
void Headers::set(const std::string& name, const std::string& value) {
    const std::string want = lowerAscii(name);
    bool found = false;
    for (auto& [k, v] : entries_) if (lowerAscii(k) == want) { v = value; found = true; }
    if (!found) entries_.emplace_back(name, value);
}
void Headers::append(const std::string& name, const std::string& value) {
    entries_.emplace_back(name, value);
}
void Headers::remove(const std::string& name) {
    const std::string want = lowerAscii(name);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
        [&](const auto& p) { return lowerAscii(p.first) == want; }), entries_.end());
}
StringMap Headers::toMap() const {
    StringMap map = collections::makeMap<std::string, std::string>();
    for (const auto& [k, v] : entries_) map->set(lowerAscii(k), v);
    return map;
}

// --- FormData ----------------------------------------------------------------
void FormData::append(const std::string& name, const std::string& value) { entries_.emplace_back(name, value); }
void FormData::append(const std::string& name, const buffer::Buffer& value) { entries_.emplace_back(name, value.bytes()); }
std::string FormData::get(const std::string& name) const {
    for (const auto& [k, v] : entries_) if (k == name) return v;
    return {};
}
bool FormData::has(const std::string& name) const {
    for (const auto& [k, v] : entries_) if (k == name) return true;
    return false;
}
void FormData::set(const std::string& name, const std::string& value) {
    bool found = false;
    for (auto& [k, v] : entries_) if (k == name) { v = value; found = true; }
    if (!found) entries_.emplace_back(name, value);
}
void FormData::remove(const std::string& name) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
        [&](const auto& p) { return p.first == name; }), entries_.end());
}
std::string FormData::encode(const std::string& boundary) const {
    std::string out;
    for (const auto& [k, v] : entries_) {
        out += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + k + "\"\r\n\r\n" + v + "\r\n";
    }
    out += "--" + boundary + "--\r\n";
    return out;
}

FetchInit withFormData(FetchInit init, const FormData& form) {
    const std::string boundary = "----cp3formboundary7c1a";
    init.body = form.encode(boundary);
    init.hasBody = true;
    if (!init.headers) init.headers = collections::makeMap<std::string, std::string>();
    init.headers->set("content-type", "multipart/form-data; boundary=" + boundary);
    return init;
}

// --- Request / Response --------------------------------------------------------
Request::Request(std::string url_) : url(std::move(url_)) {}
Request::Request(std::string url_, RequestInit init)
    : url(std::move(url_)), method(init.method), body(std::move(init.body)),
      hasBody(init.hasBody), form(init.form), signal(init.signal) {
    if (init.headersObj) headers = *init.headersObj;
    else if (init.headers) headers = Headers(init.headers);
}
Response::Response(std::string body_) : body(std::move(body_)) { ok = status >= 200 && status < 300; }
Response::Response(std::string body_, double code, Headers h)
    : status(static_cast<std::uint16_t>(code)), headers(std::move(h)), body(std::move(body_)) {
    ok = status >= 200 && status < 300;
}

Promise<FetchResponse> fetch(const Request& request) {
    FetchInit init;
    init.method = request.method;
    init.body = request.body;
    init.hasBody = request.hasBody;
    init.headers = request.headers.toMap();
    init.signal = request.signal;
    if (request.form) {
        init = withFormData(init, *request.form);
        init.method = request.method;
        init.signal = request.signal;
    }
    return fetch(request.url, init);
}

// --- Middleware factories ------------------------------------------------------
MiddlewareFn makeStaticMiddleware(std::string dir) {
    return [dir = std::move(dir)](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
        if (req.method != "GET" && req.method != "HEAD") { next(); return Promise<void>::resolve(); }
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path base = fs::weakly_canonical(fs::path(dir), ec);
        if (ec) { next(); return Promise<void>::resolve(); }
        // `req.path` is absolute (`/hello.txt`): joining it directly would
        // discard `base` (a rooted path replaces the accumulator). Strip the
        // leading slashes so the file resolves under the static directory.
        std::string relText = req.path;
        while (!relText.empty() &&
               (relText.front() == '/' || relText.front() == '\\')) {
            relText.erase(relText.begin());
        }
        fs::path candidate = fs::weakly_canonical(base / fs::path(relText), ec);
        if (ec) { next(); return Promise<void>::resolve(); }
        const std::string b = base.string(), c = candidate.string();
        const bool inside = c == b || (c.size() > b.size() && c.compare(0, b.size(), b) == 0 && (c[b.size()] == '/' || c[b.size()] == '\\'));
        if (!inside || !fs::is_regular_file(candidate, ec)) { next(); return Promise<void>::resolve(); }
        res.sendFile(candidate.string());
        return Promise<void>::resolve();
    };
}

MiddlewareFn makeCorsMiddleware(CorsOptions options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
        res.header("Access-Control-Allow-Origin", options.origin);
        res.header("Access-Control-Allow-Methods", options.methods);
        if (!options.headers.empty()) res.header("Access-Control-Allow-Headers", options.headers);
        if (options.credentials) res.header("Access-Control-Allow-Credentials", "true");
        if (options.maxAge > 0) res.header("Access-Control-Max-Age", std::to_string(static_cast<long long>(options.maxAge)));
        if (req.method == "OPTIONS") { res.status(204).send(); return Promise<void>::resolve(); }
        next();
        return Promise<void>::resolve();
    };
}

MiddlewareFn makeBodyLimitMiddleware(double limit) {
    return [limit](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
        if (limit >= 0 && static_cast<double>(req.bodySize()) > limit) {
            res.status(413).text("payload too large");
            return Promise<void>::resolve();
        }
        next();
        return Promise<void>::resolve();
    };
}

/// `tls({...})` factory body; validation lives here so the error points at
/// the options object, not the server call.
tls::TlsOptions makeTlsOptions(const std::string& cert, const std::string& key,
                               const std::string& ca, const std::string& passphrase,
                               const std::string& minProtocol) {
    tls::TlsOptions opts;
    opts.cert = cert;
    opts.key = key;
    opts.ca = ca;  // ignored by the v0.2 server (createServerWithConfig refuses it)
    opts.keyPassphrase = passphrase;
    const std::string v = minProtocol.empty() ? "TLSv1.2" : minProtocol;
    if (v == "TLSv1.2" || v == "1.2") opts.minProtocol = "TLSv1.2";
    else if (v == "TLSv1.3" || v == "1.3") opts.minProtocol = "TLSv1.3";
    else {
        throw Thrown::make("[ERR_TLS_INVALID_OPTIONS] tls minProtocol must be \"TLSv1.2\" or \"TLSv1.3\" (got \"" +
                           minProtocol + "\")");
    }
    return opts;
}

HttpServer createServerWithConfig(const CreateServerConfig& config) {
    // Fail closed BEFORE any HttpServer exists: a TLS request must never
    // produce a plaintext server (uWS cannot take inline PEM, only files).
    tls::TlsOptions tlsOptions;
    bool tlsEnabled = false;
    if (config.tls) {
        const tls::TlsOptions& opts = config.tlsOptionsSet ? config.tlsOptions : tls::TlsOptions{};
        if (opts.cert.empty() || opts.key.empty()) {
            throw Thrown::make(
                "[ERR_TLS_INVALID_OPTIONS] createServer({tls}) requires tls.cert and tls.key (PEM file paths)");
        }
        if (!opts.ca.empty()) {
            throw Thrown::make(
                "[ERR_TLS_INVALID_OPTIONS] tls.ca (client-cert mTLS) is not supported in v0.2");
        }
        readTlsPemFile(opts.cert, "cert", "-----BEGIN CERTIFICATE-----");
        readTlsPemFile(opts.key, "key", "PRIVATE KEY-----");
        tlsOptions = opts;
        tlsEnabled = true;
    }
    HttpServer server = config.threads > 0.0 ? HttpServer(config.threads)
                                             : HttpServer();
    if (tlsEnabled) server.setTlsOptions(tlsOptions);
    if (config.hasBodyLimit) {
        server.use(makeBodyLimitMiddleware(config.bodyLimit));
    }
    if (config.hasCors) {
        server.use(makeCorsMiddleware(config.cors));
    }
    if (config.hasStatic) {
        server.use(makeStaticMiddleware(config.staticDir));
    }
    if (config.trustProxy) {
        server.setTrustProxy(true);
    }
    if (config.hasViews) {
        setwebserverResponseViewDir(config.viewsDir);
    }
    if (config.hasOnError && config.onError) {
        server.onError(config.onError);
    }
    return server;
}

}  // namespace cp3::runtime::network
