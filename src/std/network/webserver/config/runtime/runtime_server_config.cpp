// =============================================================================
// src/std/network/webserver/config/runtime/runtime_server_config.cpp
// createServer(options) runtime: TLS+CORS+limits validation (fail closed),
// CORS middleware, HttpServer factory. See config.hpp for the contract.
// OWNERSHIP: src/std/network/webserver/config/** only (SOLO subtree).
// Builds only on cp3/std/network.hpp + std::filesystem. No server internals.
// =============================================================================
#include "cp3/std/network/webserver/config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace cp3::runtime::network::webserver::config {
namespace {

[[noreturn]] void fail(const char* code, const std::string& message) {
    throw Thrown::make(std::string("[") + code + "] " + message);
}

std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string upperAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isInteger(double v) {
    return std::isfinite(v) && v == std::floor(v);
}

void checkCount(double v, const char* name, double max) {
    if (!isInteger(v) || v < 0.0 || v > max) {
        fail(ServerConfigErrorCode::kInvalidConfig,
             std::string("createServer() '") + name + "' must be an integer in 0.." +
                 std::to_string(static_cast<long long>(max)));
    }
}

bool hasCrlfOrSpace(const std::string& s) {
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') return true;
    }
    return false;
}

/// RFC 9110 token characters (tchar).
bool isToken(const std::string& s) {
    if (s.empty()) return false;
    static const std::string kTchars = "!#$%&'*+-.^_`|~0123456789"
                                       "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (char c : s) {
        if (kTchars.find(c) == std::string::npos) return false;
    }
    return true;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Read a TLS file field; rejects PEM-text values and missing/empty files.
std::string readTlsFile(const std::string& value, const char* field,
                        const char* pemMarker) {
    if (value.find("-----BEGIN") != std::string::npos || value.find('\n') != std::string::npos) {
        fail(ServerConfigErrorCode::kInvalidOptions,
             std::string("tls.") + field +
                 " must be a file path, not PEM text (uWS SSL takes file names; "
                 "write the PEM to a file first)");
    }
    const std::string content = readFile(value);
    if (content.empty()) {
        fail(ServerConfigErrorCode::kInvalidOptions,
             std::string("tls.") + field + " file not found or empty: " + value);
    }
    if (content.find(pemMarker) == std::string::npos) {
        fail(ServerConfigErrorCode::kInvalidOptions,
             std::string("tls.") + field + " file does not contain '" + pemMarker +
                 "': " + value);
    }
    return content;
}

std::string headerGet(const HttpRequest& req, const std::string& name) {
    if (!req.headers) return {};
    auto v = req.headers->get(lowerAscii(name));
    return v ? *v : std::string{};
}

void validateTls(const TlsOptions& tls) {
    if (tls.cert.empty()) {
        fail(ServerConfigErrorCode::kInvalidOptions, "tls.cert is required when tls is set");
    }
    if (tls.key.empty()) {
        fail(ServerConfigErrorCode::kInvalidOptions, "tls.key is required when tls is set");
    }
    if (tls.cert == tls.key) {
        fail(ServerConfigErrorCode::kInvalidOptions,
             "tls.cert and tls.key must be different files");
    }
    // Throws on an unsupported protocol (floor enforcement).
    (void)tlsMinProtocolVersion(tls.minProtocol);
    readTlsFile(tls.cert, "cert", "-----BEGIN CERTIFICATE-----");
    readTlsFile(tls.key, "key", "PRIVATE KEY-----");
    if (!tls.chain.empty()) readTlsFile(tls.chain, "chain", "-----BEGIN CERTIFICATE-----");
    if (!tls.ca.empty()) readTlsFile(tls.ca, "ca", "-----BEGIN CERTIFICATE-----");
}

void validateCors(const CorsOptions& cors) {
    if (cors.origin.empty() || hasCrlfOrSpace(cors.origin)) {
        fail(ServerConfigErrorCode::kInvalidCors,
             "cors.origin must be \"*\" or an http(s) origin, without spaces");
    }
    for (const std::string& origin : splitList(cors.origin)) {
        if (origin != "*" && origin.rfind("http://", 0) != 0 && origin.rfind("https://", 0) != 0) {
            fail(ServerConfigErrorCode::kInvalidCors,
                 "cors.origin entries must be \"*\" or http(s) origins: " + origin);
        }
    }
    for (const std::string& method : splitList(cors.methods)) {
        if (!isToken(upperAscii(method))) {
            fail(ServerConfigErrorCode::kInvalidCors,
                 "cors.methods contains an invalid HTTP method token: " + method);
        }
    }
    if (!cors.headers.empty()) {
        for (const std::string& header : splitList(cors.headers)) {
            if (!isToken(header)) {
                fail(ServerConfigErrorCode::kInvalidCors,
                     "cors.headers contains an invalid header name: " + header);
            }
        }
    }
    if (!isInteger(cors.maxAge) || cors.maxAge < 0.0 || cors.maxAge > 31536000.0) {
        fail(ServerConfigErrorCode::kInvalidCors,
             "cors.maxAge must be an integer in 0..31536000 (seconds)");
    }
}

}  // namespace

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::string part = trim(value.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!part.empty()) out.push_back(part);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

std::string joinStrings(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += ", ";
        out += parts[i];
    }
    return out;
}

std::string normalizeMinProtocol(const std::string& value) {
    const std::string v = trim(value);
    if (v.empty() || v == "TLSv1.2" || v == "1.2") return "TLSv1.2";
    if (v == "TLSv1.3" || v == "1.3") return "TLSv1.3";
    fail(ServerConfigErrorCode::kInvalidOptions,
         "tls.minProtocol must be \"TLSv1.2\" or \"TLSv1.3\" (got \"" + value +
             "\"); TLSv1.0/1.1 and SSLv3 are refused");
}

int tlsMinProtocolVersion(const std::string& value) {
    return normalizeMinProtocol(value) == "TLSv1.3" ? 0x0304 : 0x0303;
}

ServerConfig validateServerConfig(const ServerConfig& config) {
    ServerConfig out = config;
    if (!isInteger(out.threads) || out.threads < 1.0 || out.threads > 4096.0) {
        fail(ServerConfigErrorCode::kInvalidConfig,
             "createServer() 'threads' must be an integer in 1..4096");
    }
    checkCount(out.maxConnections, "maxConnections", 2147483647.0);
    checkCount(out.idleTimeout, "idleTimeout", 86400.0);
    checkCount(out.maxWriteBuffer, "maxWriteBuffer", 1073741824.0);
    checkCount(out.timeout, "timeout", 86400.0);
    if (out.tls.enabled) {
        out.tls.minProtocol = normalizeMinProtocol(out.tls.minProtocol);
        validateTls(out.tls);
    }
    if (out.cors.present) {
        // Normalise method spelling; the list itself is validated, not widened.
        std::vector<std::string> methods = splitList(out.cors.methods);
        for (std::string& m : methods) m = upperAscii(m);
        out.cors.methods = methods.empty()
            ? std::string("GET,POST,PUT,PATCH,DELETE,OPTIONS")
            : joinStrings(methods);
        validateCors(out.cors);
    }
    return out;
}

ServerConfig makeServerConfig(double threads, double maxConnections, double idleTimeout,
                              double maxWriteBuffer, double timeout, TlsOptions tls,
                              CorsOptions cors) {
    ServerConfig config;
    config.threads = threads;
    config.maxConnections = maxConnections;
    config.idleTimeout = idleTimeout;
    config.maxWriteBuffer = maxWriteBuffer;
    config.timeout = timeout;
    config.tls = std::move(tls);
    config.cors = std::move(cors);
    return validateServerConfig(config);
}

void requireTlsBackend() {
#ifdef CP3_UWS_SSL
    return;  // uSockets rebuilt with an SSL backend (README §Wiring step 1)
#else
    // third_party uSockets ships LIBUS_NO_SSL (MANIFEST caveat 3): an
    // SSLApp context would fail at handshake time, so fail at config time.
    fail(ServerConfigErrorCode::kBackendUnavailable,
         "TLS backend unavailable: uSockets was built LIBUS_NO_SSL; rebuild with "
         "an SSL backend and define CP3_UWS_SSL (README wiring step 1). "
         "Refusing to serve plaintext instead of the requested TLS.");
#endif
}

SslFiles sslFiles(const ServerConfig& config) {
    const ServerConfig validated = validateServerConfig(config);
    if (!validated.hasTls()) {
        fail(ServerConfigErrorCode::kInvalidOptions, "sslFiles() requires tls to be set");
    }
    SslFiles files;
    files.certFile = validated.tls.cert;
    files.keyFile = validated.tls.key;
    files.caFile = validated.tls.ca;
    files.chainFile = validated.tls.chain;
    return files;
}

network::MiddlewareFn makeCorsMiddleware(const CorsOptions& options) {
    const CorsOptions opts = validateServerConfig(
        [&] { ServerConfig c; c.cors = options; c.cors.present = true; return c; }()).cors;
    return [opts](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
        const std::string requestOrigin = headerGet(req, "origin");
        const std::string requestMethod = headerGet(req, "access-control-request-method");

        // Resolve Access-Control-Allow-Origin (never "*" with credentials).
        std::string allowOrigin;
        if (!requestOrigin.empty()) {
            if (opts.origin == "*") {
                allowOrigin = opts.credentials ? requestOrigin : "*";
            } else {
                for (const std::string& origin : splitList(opts.origin)) {
                    if (origin == "*" || origin == requestOrigin) {
                        allowOrigin = origin == "*" ? requestOrigin : origin;
                        break;
                    }
                }
            }
        }
        if (!allowOrigin.empty()) res.header("Access-Control-Allow-Origin", allowOrigin);
        res.header("Access-Control-Allow-Methods", opts.methods);
        std::string allowHeaders = opts.headers;
        if (allowHeaders.empty()) allowHeaders = headerGet(req, "access-control-request-headers");
        if (!allowHeaders.empty()) res.header("Access-Control-Allow-Headers", allowHeaders);
        if (opts.credentials) res.header("Access-Control-Allow-Credentials", "true");
        if (opts.maxAge > 0.0) {
            res.header("Access-Control-Max-Age",
                       std::to_string(static_cast<long long>(opts.maxAge)));
        }
        res.header("Vary", "Origin, Access-Control-Request-Method, Access-Control-Request-Headers");

        const bool preflight =
            req.method == "OPTIONS" && !requestOrigin.empty() && !requestMethod.empty();
        if (preflight) {
            res.status(204).send();  // answered here; user routes never see it
            return Promise<void>::resolve();
        }
        next();
        return Promise<void>::resolve();
    };
}

void applyServerConfig(network::HttpServer& server, const ServerConfig& config) {
    const ServerConfig validated = validateServerConfig(config);
    if (validated.hasTls()) requireTlsBackend();  // throws -> nothing installed
    if (validated.hasCors()) {
        server.use(makeCorsMiddleware(validated.cors));
    }
}

network::HttpServer createServerFromConfig(const ServerConfig& config) {
    const ServerConfig validated = validateServerConfig(config);
    // Fail closed BEFORE any HttpServer exists: a TLS request must never
    // produce a plaintext server (mirrors tls/README, CP3_TLS_OPENSSL stub).
    if (validated.hasTls()) requireTlsBackend();
    network::HttpServer server = validated.threads == 1.0
        ? network::HttpServer()
        : network::HttpServer(validated.threads);
    if (validated.hasCors()) {
        server.use(makeCorsMiddleware(validated.cors));
    }
    return server;
}

}  // namespace cp3::runtime::network::webserver::config
