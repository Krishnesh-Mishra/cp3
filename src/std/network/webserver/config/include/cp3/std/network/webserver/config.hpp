// =============================================================================
// src/std/network/webserver/config/include/cp3/std/network/webserver/config.hpp
// `createServer(options)` config: TLS + CORS + server limits, no `app.use`.
// SOLO subtree: src/std/network/webserver/config/** only. No shared file is
// modified or required for this header; the integrator applies the paste
// lines in README.md (native row, sema `createServer` arm, codegen arm).
//
// CP3 SURFACE (after wiring, README.md):
//   createServer({
//     tls:  { cert, key, chain?, ca?, minProtocol? },   // file paths, see below
//     cors: { origin?, methods?, headers?, credentials?, maxAge? },
//     threads?, maxConnections?, idleTimeout?, maxWriteBuffer?, timeout?,
//   }) -> HttpServer   (CORS installed as the first middleware automatically)
//
// FAIL CLOSED (no plaintext downgrade, no silent CORS widening):
//   * tls present but cert/key missing, unreadable, empty, PEM-text instead
//     of a file path, or without the expected PEM block -> throw
//     [ERR_TLS_INVALID_OPTIONS] at createServer() time (before listen).
//   * minProtocol other than TLSv1.2/TLSv1.3 (incl. TLSv1.0/1.1/SSLv3) ->
//     [ERR_TLS_INVALID_OPTIONS]; it is a floor, never a "prefer this" hint.
//   * tls present but the uWS SSL backend is not built (CP3_UWS_SSL not
//     defined; third_party uSockets is built LIBUS_NO_SSL, MANIFEST caveat 3)
//     -> [ERR_TLS_BACKEND_UNAVAILABLE]. The server is never created as plain
//     HTTP when the program asked for TLS.
//   * bad numeric limits / bad CORS values -> [ERR_SERVER_CONFIG_INVALID_OPTIONS]
//     / [ERR_CORS_INVALID_OPTIONS] at createServer() time.
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::webserver::config {

/// Machine-readable config failure codes (TLS codes match std/network/tls).
struct ServerConfigErrorCode {
    static constexpr const char* kInvalidOptions = "ERR_TLS_INVALID_OPTIONS";
    static constexpr const char* kBackendUnavailable = "ERR_TLS_BACKEND_UNAVAILABLE";
    static constexpr const char* kProtocolTooOld = "ERR_TLS_PROTOCOL_TOO_OLD";
    static constexpr const char* kInvalidConfig = "ERR_SERVER_CONFIG_INVALID_OPTIONS";
    static constexpr const char* kInvalidCors = "ERR_CORS_INVALID_OPTIONS";
};

/// `tls: { cert, key, chain?, ca?, minProtocol? }`.
/// cert/key/chain/ca are FILE PATHS (uWS SSL `SocketContextOptions` takes
/// file names only, like `us_socket_context_options_t`); a PEM block passed
/// as the value itself is rejected instead of being written somewhere
/// silently. `chain` is an optional extra intermediate bundle, appended by
/// the integrator to the cert file (uWS has no separate chain slot).
/// `ca` is the optional CA bundle for client-certificate verification
/// (`ca_file_name`). `minProtocol` is "" (default TLSv1.2), "TLSv1.2" or
/// "TLSv1.3" ("1.2"/"1.3" accepted and normalised); anything older fails.
struct TlsOptions {
    bool enabled = false;  // true when a `tls: {...}` object was present
    std::string cert;
    std::string key;
    std::string chain;
    std::string ca;
    std::string minProtocol = "TLSv1.2";
};

/// `cors: { origin?, methods?, headers?, credentials?, maxAge? }`.
/// origin: "*" (default) or one/comma-separated http(s) origin(s).
///   With credentials:true, "*" is never emitted (browsers reject it):
///   the request Origin is reflected instead (see README, divergence from
///   the existing makeCorsMiddleware, which emits "*" unconditionally).
/// methods: comma list, default "GET,POST,PUT,PATCH,DELETE,OPTIONS".
/// headers: comma list; "" = reflect Access-Control-Request-Headers.
/// maxAge: seconds, 0 = header omitted. All validated (no CRLF/tokens).
struct CorsOptions {
    bool present = false;  // true when a `cors: {...}` object was present
    std::string origin = "*";
    std::string methods = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
    std::string headers;
    bool credentials = false;
    double maxAge = 0.0;
};

/// The whole `createServer(options)` object. Numeric fields are CP3
/// `number`s (double), matching HttpServer(double threads) / TcpServerOptions.
/// Units: idleTimeout/timeout are SECONDS (uWS socket timeout granularity),
/// maxWriteBuffer is BYTES. 0 means "disabled/unlimited" for every limit
/// except threads (1..4096). Configure before listen(); the scaled server
/// (threads > 1) shares one config across loop threads.
struct ServerConfig {
    double threads = 1.0;
    double maxConnections = 0.0;   // 0 = unlimited; enforced at dispatch (README)
    double idleTimeout = 0.0;      // seconds, 0 = disabled
    double maxWriteBuffer = 0.0;   // bytes, 0 = unbounded
    double timeout = 0.0;          // seconds, 0 = disabled
    TlsOptions tls;
    CorsOptions cors;

    bool hasTls() const { return tls.enabled; }
    bool hasCors() const { return cors.present; }
};

/// File names for the integrator's SSLApp construction (uWS
/// `SocketContextOptions::key_file_name/cert_file_name/ca_file_name`).
/// The strings are owned by the ServerConfig - keep the config alive for
/// the process lifetime (codegen emits it as `static`, README §Wiring).
struct SslFiles {
    std::string certFile;
    std::string keyFile;
    std::string caFile;     // "" when no ca
    std::string chainFile;  // "" when no chain
};

/// Pure helpers (also used by the sema/codegen fragments).
std::string normalizeMinProtocol(const std::string& value);  // throws Thrown
int tlsMinProtocolVersion(const std::string& value);          // 0x0303/0x0304, throws
std::string joinStrings(const std::vector<std::string>& parts);  // ", "-join for codegen string[]
std::vector<std::string> splitList(const std::string& value);    // comma-split + trim

/// Validate + normalise (minProtocol spelling, CORS lists). Returns the
/// normalised copy; throws Thrown ([CODE] message) fail-closed. Reads the
/// TLS files from disk (existence + PEM block presence) when tls enabled.
ServerConfig validateServerConfig(const ServerConfig& config);

/// Assemble a ServerConfig from codegen-emitted field expressions and
/// validate it. Field order matches the CP3 options object.
ServerConfig makeServerConfig(double threads, double maxConnections, double idleTimeout,
                              double maxWriteBuffer, double timeout, TlsOptions tls,
                              CorsOptions cors);

/// Throws [ERR_TLS_BACKEND_UNAVAILABLE] unless built with CP3_UWS_SSL.
/// Call this (or createServerFromConfig) before constructing any plain
/// HttpServer when tls is requested - never fall back to plaintext.
void requireTlsBackend();

/// sslFiles(): the owned file names for `uWS::SSLApp(options)` (integrator
/// SSL path, README §Wiring). Does NOT check the backend; call
/// requireTlsBackend() first. Throws on invalid options like validate.
SslFiles sslFiles(const ServerConfig& config);

/// The CORS middleware installed by applyServerConfig (also usable
/// directly via app.use). Preflight is answered 204 without next().
network::MiddlewareFn makeCorsMiddleware(const CorsOptions& options);

/// Install the config's CORS middleware as the FIRST middleware of an
/// already-constructed server (call before any user app.use). Validates
/// first; with tls it throws [ERR_TLS_BACKEND_UNAVAILABLE] (fail closed)
/// when the SSL backend is not built - it never installs a plain server
/// as a substitute for a TLS one.
void applyServerConfig(network::HttpServer& server, const ServerConfig& config);

/// `createServer(options)`: validate -> (tls: requireTlsBackend) ->
/// HttpServer(threads) -> applyServerConfig. With tls and no SSL backend
/// this throws before any HttpServer exists (no downgrade).
network::HttpServer createServerFromConfig(const ServerConfig& config);

/// In-flight dispatch counter for maxConnections. Integrator line (README):
/// one guard per server, enter() at the top of the request glue, leave()
/// when the response finishes/aborts. enter() returns false over the cap.
class ConnectionGuard {
public:
    explicit ConnectionGuard(double maxConnections = 0.0)
        : max_(maxConnections > 0.0 ? static_cast<std::size_t>(maxConnections) : 0) {}
    bool enter() {
        const std::size_t cur = active_.fetch_add(1, std::memory_order_relaxed);
        if (max_ != 0 && cur >= max_) { leave(); return false; }
        return true;
    }
    void leave() { active_.fetch_sub(1, std::memory_order_relaxed); }
    std::size_t active() const { return active_.load(std::memory_order_relaxed); }
    std::size_t max() const { return max_; }

private:
    std::size_t max_ = 0;  // 0 = unlimited
    std::atomic<std::size_t> active_{0};
};

/// Sema/codegen helpers: option-key tables + C++ text builders. Pure (no
/// Analyzer/Generator dependency) so their TUs compile standalone; the
/// shared sema/codegen calls them with one line each (README §Wiring).
namespace sema_codegen {
bool isKnownCreateServerOption(const std::string& key);
bool isKnownTlsOption(const std::string& key);
bool isKnownCorsOption(const std::string& key);
/// "" when `value` is an acceptable minProtocol literal, else a diagnostic.
std::string checkMinProtocolLiteral(const std::string& value);

/// `webserver::config::makeServerConfig(<threads>, <maxConnections>,
///  <idleTimeout>, <maxWriteBuffer>, <timeout>, <tls>, <cors>)`.
/// Empty exprs fall back to the documented defaults.
std::string serverConfigCallText(const std::string& threadsExpr,
                                 const std::string& maxConnectionsExpr,
                                 const std::string& idleTimeoutExpr,
                                 const std::string& maxWriteBufferExpr,
                                 const std::string& timeoutExpr,
                                 const std::string& tlsExpr,
                                 const std::string& corsExpr);
/// `webserver::config::TlsOptions{true, <cert>, <key>, <chain>, <ca>, <proto>}`
/// (empty exprs -> std::string() / the TLSv1.2 default).
std::string tlsOptionsText(const std::string& certExpr, const std::string& keyExpr,
                           const std::string& chainExpr, const std::string& caExpr,
                           const std::string& minProtocolExpr);
/// `webserver::config::CorsOptions{true, <origin>, <methods>, <headers>,
///  <credentials>, <maxAge>}` (empty exprs -> the documented defaults).
std::string corsOptionsText(const std::string& originExpr, const std::string& methodsExpr,
                            const std::string& headersExpr, const std::string& credentialsExpr,
                            const std::string& maxAgeExpr);
/// `webserver::config::createServerFromConfig(<configExpr>)`.
std::string createServerCallText(const std::string& configExpr);
}  // namespace sema_codegen
}  // namespace cp3::runtime::network::webserver::config
