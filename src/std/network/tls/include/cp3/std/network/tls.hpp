// =============================================================================
// cp3/std/network/tls.hpp - `std/network` TLS surface (TLSSocket + servers).
//
// OWNERSHIP: the std/network/tls family (src/std/network/tls/**). Additive to
// the generated-code ABI (D14): this header is included by generated code only
// when a program uses the TLS surface (codegen sets `needsTls_`); the runtime
// TU is selected by the marker row in src/std/network/codegen/native.hpp (D68),
// so programs that never import the TLS names never compile or link it.
//
// -----------------------------------------------------------------------------
// LIBRARY PICK (see README.md for the full rationale)
// -----------------------------------------------------------------------------
//   OpenSSL (libssl/libcrypto), memory-BIO engine. No TLS library is vendored
//   in vendor/third_party/ today (uSockets is built LIBUS_NO_SSL, MANIFEST caveat 3),
//   so the full surface is implemented against the documented backend boundary
//   in tls_backend.hpp and ships with a stub backend that FAILS CLOSED:
//   connectTls()/createTlsServer() throw a TLSError
//   (ERR_TLS_BACKEND_UNAVAILABLE) and no plaintext fallback is ever attempted.
//   Defining CP3_TLS_OPENSSL and linking the vendored OpenSSL archives swaps
//   the stub for the real engine (runtime_tls.cpp); nothing else changes.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 §network, tls extension)
// -----------------------------------------------------------------------------
//   import { createTlsServer, connectTls, TlsSocket } from "std/network";
//     createTlsServer(options?, handler?) -> TlsServer   (TLS over TCP)
//     connectTls(options | host, port?, options?) -> TlsSocket
//     new TlsSocket(socket, options?)    -> TlsSocket    (wrap a TcpSocket)
//   TlsSocket members mirror TcpSocket exactly (see README §TcpSocket parity);
//   the TLS additions are the `secureConnect` event, `authorized`,
//   `encrypted`, `alpnProtocol`, `servername`, `getCipher()`, `getProtocol()`,
//   `getPeerCertificate()` and `setMaxSendFragment()`.
//   `socket.on("error", (err) => ...)` receives a cp3 Error object (name
//   "TLSError", message carries the machine code as `[CODE] ...`), never a
//   bare string; handshake/verify failures are delivered the same way and
//   double as the rejection of the connectTls() promise path (synchronous
//   throw at construction, see README).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/std/buffer.hpp"

namespace cp3::runtime::network::tls {

/// Machine-readable TLS failure codes. Carried in the Error message as
/// `[CODE] human text` and on the C++ TlsError::code field; Node's
/// `err.code` spelling is kept where it exists.
struct TlsErrorCode {
    static constexpr const char* kBackendUnavailable = "ERR_TLS_BACKEND_UNAVAILABLE";
    static constexpr const char* kCertVerifyFailed = "CERT_UNTRUSTED";
    static constexpr const char* kHandshakeTimeout = "ERR_TLS_HANDSHAKE_TIMEOUT";
    static constexpr const char* kHandshakeRateLimited = "ERR_TLS_HANDSHAKE_RATE_LIMITED";
    static constexpr const char* kHandshakeFailed = "ERR_TLS_HANDSHAKE_FAILED";
    static constexpr const char* kProtocolTooOld = "ERR_TLS_PROTOCOL_TOO_OLD";  // < TLS1.2
    static constexpr const char* kNoPeerCertificate = "ERR_TLS_NO_PEER_CERTIFICATE";
    static constexpr const char* kNotConnected = "ERR_TLS_NOT_CONNECTED";
    static constexpr const char* kSocketDestroyed = "ERR_TLS_SOCKET_DESTROYED";
    static constexpr const char* kInvalidOptions = "ERR_TLS_INVALID_OPTIONS";
};

/// The cp3 Error object every TLS failure is delivered as: thrown from the
/// factory functions, passed to "error" listeners, and used as the rejection
/// reason. `name` is "TLSError"; `code` is the TlsErrorCode string.
class TlsError : public cp3::Error {
public:
    std::string code;
    TlsError(std::string code_, std::string message)
        : Error(message), code(std::move(code_)) {
        name = "TLSError";
        refreshStack();
    }
    /// The `[CODE] message` form Node prints as `err.message` with a code.
    std::string toString() const override {
        return name + ": [" + code + "] " + message;
    }
    static Thrown thrown(const std::string& code, const std::string& message) {
        return Thrown::makeError(std::make_shared<TlsError>(code, message));
    }
    static bool is(const Thrown& t) { return t.get<TlsError>() != nullptr; }
    static std::shared_ptr<TlsError> fromThrown(const Thrown& t) {
        if (const auto* p = t.get<TlsError>()) return std::make_shared<TlsError>(*p);
        auto err = t.asError();
        return std::make_shared<TlsError>(TlsErrorCode::kHandshakeFailed,
                                          err ? err->message : std::string("tls error"));
    }
};

/// `TlsConnectOptions` / `TlsServerOptions` (one struct; the server ignores
/// host/port/servername, the client ignores cert/key/ca). Field names are the
/// Node `tls` option names codegen reads from the options object literal.
struct TlsOptions {
    // --- identity / verification (verification is ON by default) ---
    std::string host;                 // client: SNI + hostname verification target
    double port = 443.0;              // client: TCP port (server: listen port)
    std::string servername;           // SNI override; "" = host
    std::string ca;                   // PEM bundle (file path OR PEM text); "" = OS trust store
    std::string cert;                 // PEM certificate (file path or PEM text)
    std::string key;                  // PEM private key matching `cert`
    std::string keyPassphrase;        // passphrase for an encrypted `key`
    bool rejectUnauthorized = true;   // MUST stay true unless the caller opts out explicitly
    bool checkServerIdentity = true;  // hostname check against the peer certificate
    // --- protocol / negotiation ---
    std::string minProtocol = "TLSv1.2";  // floor; TLS1.0/1.1 are refused, never negotiated
    std::string maxProtocol = "TLSv1.3";  // ceiling
    std::vector<std::string> alpnProtocols;  // e.g. {"h2", "http/1.1"}
    std::vector<std::string> ciphers;        // "" entries are ignored
    std::size_t maxSendFragment = 16384;     // must be >= 256, <= 65536
    // --- handshake policy ---
    double handshakeTimeoutMs = 10000.0;  // 0 = server default (10s); never "infinite" by accident
    double maxHandshakesPerSecond = 50.0;  // per-server token bucket; 0 = unlimited (not recommended)
    double maxConcurrentHandshakes = 128.0;  // in-flight cap; excess sockets are destroyed
    // --- socket behaviour (TcpSocket parity) ---
    bool allowHalfOpen = false;
    bool readable = true;
    bool writable = true;
};

/// `socket.getCipher()`: the negotiated cipher suite (Node shape).
struct CipherInfo {
    std::string name;        // e.g. "TLS_AES_256_GCM_SHA384"
    std::string version;     // e.g. "TLSv1.3"
    bool standardName = false;
};

/// `socket.getPeerCertificate()`: the leaf certificate (Node shape, subset).
struct PeerCertificate {
    std::string subject;        // DN, one "C=..., O=..., CN=..." string
    std::string issuer;         // DN of the issuer
    std::string serialNumber;   // hex
    std::string fingerprint;    // SHA-256 hex, upper case, colon separated
    std::string notBefore;      // ISO-8601 UTC
    std::string notAfter;       // ISO-8601 UTC
    bool raw = false;           // true when `pem` carries the raw cert
    std::string pem;            // PEM text ("" when raw=false and not requested)
};

/// Implementation state (runtime_tls.cpp). Namespace scope so the TUs can name
/// it without friendship; generated code never does (same rule as network.hpp).
namespace detail {
struct TlsSocketImpl;
struct TlsServerImpl;
}  // namespace detail

/// One TLS connection. Member-for-member the TcpSocket surface (write/end/
/// destroy/close/pause/resume/setTimeout/setNoDelay/setKeepAlive/connect and
/// the connecting/destroyed/readable/readableEnded/writable/writableEnded/
/// remoteAddress/remotePort/localAddress/localPort/bytesRead/bytesWritten
/// properties) plus the TLS additions listed in the header contract above.
/// Data events fire only after the handshake completes and verification has
/// passed (or `rejectUnauthorized: false` was set explicitly by the caller).
class TlsSocket {
public:
    TlsSocket();
    /// `new TlsSocket(tcpSocket, options?)`: wrap an existing TcpSocket
    /// (the underlying uv_tcp_t is adopted; the TcpSocket handle must not be
    /// used afterwards). `tcpSocket` is the TcpSocket's opaque handle (void*).
    TlsSocket(void* tcpSocket, const TlsOptions& options);
    ~TlsSocket();

    TlsSocket(const TlsSocket&) = delete;
    TlsSocket& operator=(const TlsSocket&) = delete;
    TlsSocket(TlsSocket&& other) noexcept;
    TlsSocket& operator=(TlsSocket&& other) noexcept;

    /// Internal: adopt an accepted server-side connection.
    static TlsSocket adopt(std::shared_ptr<detail::TlsSocketImpl> impl);

    // --- TcpSocket parity: events ---
    /// `socket.on("data", (chunk: string | Buffer) => void)`; a handler that
    /// takes a Buffer receives the raw bytes, one that takes a string the
    /// UTF-8 text, `() => void` ignores the payload (same slot rule as
    /// WebSocket in network.hpp). "secureConnect" fires once, after the
    /// handshake; "connect" fires (TcpSocket parity) at the same moment.
    void on(const std::string& event, std::function<void()> handler) const;
    void on(const std::string& event, std::function<void(std::string)> handler) const;
    void on(const std::string& event, std::function<void(const buffer::Buffer&)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;

    // --- TcpSocket parity: writes / lifecycle ---
    /// `socket.write(data)` (string | Buffer). Returns false when the socket
    /// is not writable yet (before secureConnect) or is destroyed; the chunk
    /// is dropped and an "error" (ERR_TLS_NOT_CONNECTED) is emitted instead
    /// of throwing, matching Node's async error delivery. NOT buffered for
    /// replay after the handshake (documented divergence, README).
    bool write(const std::string& data) const;
    bool write(const buffer::Buffer& data) const;
    /// `socket.end(data?)`: flush, send close_notify, FIN. Optional data is
    /// written first (string form only; Buffer overload takes the chunk).
    void end() const;
    void end(const std::string& data) const;
    void end(const buffer::Buffer& data) const;
    /// `socket.destroy(err?)`: abortive close (no close_notify flush needed).
    void destroy() const;
    void destroy(Thrown err) const;
    /// `socket.close()`: graceful close request (same as end() here).
    void close() const;
    /// `socket.pause()` / `socket.resume()`: stop/start "data" delivery.
    void pause() const;
    void resume() const;
    /// `socket.setTimeout(ms, callback?)`: idle timeout; ticks "timeout"
    /// (the socket is NOT auto-closed, Node semantics).
    void setTimeout(double ms, std::function<void()> callback = {}) const;
    /// `socket.setNoDelay(enabled)` / `socket.setKeepAlive(enabled, delay?)`:
    /// TCP_NODELAY / SO_KEEPALIVE on the underlying uv_tcp_t.
    void setNoDelay(bool enabled) const;
    void setKeepAlive(bool enabled, double delayMs = 0.0) const;
    /// `socket.connect(port, host?)`: re-connect a detached socket. Mutates
    /// the handle, so it is not const (TcpSocket has no reconnect parity here).
    void connect(double port, const std::string& host = "");

    // --- TCP properties (readable state) ---
    bool connecting() const;
    bool destroyed() const;
    bool readable() const;
    bool readableEnded() const;
    bool writable() const;
    bool writableEnded() const;
    std::string remoteAddress() const;
    double remotePort() const;
    std::string localAddress() const;
    double localPort() const;
    double bytesRead() const;
    double bytesWritten() const;

    // --- TLS additions ---
    bool authorized() const;                 // peer verified AND checkServerIdentity passed
    bool encrypted() const;                  // handshake completed (true after secureConnect)
    std::string alpnProtocol() const;        // negotiated ALPN id, "" when none
    std::string servername() const;          // SNI actually sent / selected
    std::string getProtocol() const;         // "TLSv1.2" | "TLSv1.3" | ""
    CipherInfo getCipher() const;
    PeerCertificate getPeerCertificate(bool raw = false) const;
    void setMaxSendFragment(std::size_t size) const;
    std::size_t getMaxSendFragment() const;

private:
    std::shared_ptr<detail::TlsSocketImpl> impl_;
};

/// `createTlsServer(options?, handler?)`: a TLS server. `handler(socket)` is
/// the "secureConnection" listener (also reachable via on("secureConnection")).
class TlsServer {
public:
    explicit TlsServer(TlsOptions options = TlsOptions());
    ~TlsServer();

    TlsServer(const TlsServer&) = delete;
    TlsServer& operator=(const TlsServer&) = delete;
    /// Move operations (the impl is shared_ptr-held; moves never throw).
    TlsServer(TlsServer&& other) noexcept;
    TlsServer& operator=(TlsServer&& other) noexcept;

    /// `server.on("secureConnection", (socket) => void)` / "error" / "close".
    void on(const std::string& event, std::function<void(TlsSocket&)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;
    void on(const std::string& event, std::function<void()> handler) const;

    /// `server.listen(port, host?, callback?)`; emits "error" (TLSError) on
    /// failure instead of throwing, mirroring Node's server "error" event.
    void listen(double port, std::function<void()> callback = {}) const;
    void listen(double port, const std::string& host, std::function<void()> callback = {}) const;
    /// `server.close(callback?)`: stop accepting, close listeners, destroy
    /// in-flight handshakes (established sockets are left to the caller).
    void close(std::function<void()> callback = {}) const;
    double addressPort() const;  // 0 when not listening

private:
    std::shared_ptr<detail::TlsServerImpl> impl_;
};

/// `createTlsServer(options?, handler?)`. Throws a TLSError (Error object)
/// when the backend is unavailable or the options are invalid (bad PEM,
/// minProtocol < TLSv1.2, bad maxSendFragment): configuration errors are
/// synchronous, handshake errors are "error" events on the socket.
TlsServer createTlsServer(const TlsOptions& options = TlsOptions(),
                          std::function<void(TlsSocket&)> handler = {});

/// `connectTls(options | host, port?, options?) -> TlsSocket`. Throws a
/// TLSError synchronously for invalid options / unavailable backend; the
/// TCP connect + handshake run on the same libuv loop as the rest of
/// std/network and failure is delivered as the socket "error" event
/// (ERR_TLS_*) so TcpSocket-style handlers work unchanged.
TlsSocket connectTls(const TlsOptions& options);
TlsSocket connectTls(const std::string& host, double port = 443.0,
                     const TlsOptions& extra = TlsOptions());

}  // namespace cp3::runtime::network::tls
