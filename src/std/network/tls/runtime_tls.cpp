// =============================================================================
// src/std/network/tls/runtime_tls.cpp - `std/network` TLS runtime.
//
// OWNERSHIP: the std/network/tls family (src/std/network/tls/**). Same
// selection rule as runtime.cpp (the std/network native package row, D68):
// compiled + linked for a program that imports the TLS names; the marker row
// also links it for fetch programs (runtime_fetch_io.cpp routes https://
// through this engine).
//
// TRANSPORT. TLS over TcpSocket: the engine (tls_backend.hpp) is a pure
// memory-BIO transform, libuv owns the uv_tcp_t and every byte on the wire.
// Nothing here calls SSL_set_fd / uv's fd; the pump is
//   uv_read(ciphertext) -> engine.feedCiphertext -> handshake/readPlaintext
//   engine.writePlaintext -> drainCiphertext -> uv_write(ciphertext)
// so the timing of the event loop can never corrupt the TLS state machine.
//
// BACKEND. OpenSSL when CP3_TLS_OPENSSL is defined (see tls_backend.hpp for
// the function-by-function mapping and README.md for the vendoring recipe).
// Otherwise the stub below is linked and EVERY entry point fails closed with
// a TLSError (ERR_TLS_BACKEND_UNAVAILABLE): no handshake, no plaintext
// fallback, no silent downgrade. That is deliberate: a TLS surface that
// quietly speaks plaintext is worse than one that refuses to run.
//
// POLICY. Verification on by default (SSL_VERIFY_PEER + embedded Mozilla CA
// bundle + hostname check), TLS1.2 floor (options.minProtocol below
// "TLSv1.2" is rejected at construction, and a peer that negotiates lower is
// refused), handshake timeout (uv_timer, default 10s) and a per-server
// handshake rate limiter (token bucket + concurrent cap). All failures are
// cp3 Error objects (TlsError, name "TLSError", `[CODE] message`), delivered
// as the socket "error" event or thrown synchronously from the factories.
// =============================================================================
#include "cp3/std/network/tls.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <uv.h>

#include "../runtime/runtime_loop.hpp"
#include "tls_backend.hpp"

#ifdef CP3_TLS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "cacert.hpp"
#endif

namespace cp3::runtime::network::tls {

namespace detail {

// ---------------------------------------------------------------------------
// Pure helpers (tls_backend.hpp declarations live here for the stub; the
// OpenSSL TU reuses them, so they are defined once, backend-free).
// ---------------------------------------------------------------------------
std::uint16_t tlsProtocolVersion(const std::string& name) {
    // OpenSSL version constants, spelled numerically so this TU stays
    // OpenSSL-free: the pinned OpenSSL backend maps them back to the named
    // constants (TLS1_2_VERSION == 0x0303, TLS1_3_VERSION == 0x0304).
    if (name == "TLSv1.2" || name == "TLS1.2" || name == "tls1.2") return 0x0303;
    if (name == "TLSv1.3" || name == "TLS1.3" || name == "tls1.3") return 0x0304;
    if (name == "TLSv1.1" || name == "TLS1.1") return 0x0302;
    if (name == "TLSv1.0" || name == "TLS1.0") return 0x0301;
    return 0;
}

bool tlsProtocolAtLeast12(const std::string& name) {
    const std::uint16_t v = tlsProtocolVersion(name);
    return v >= 0x0303;
}

std::string alpnWireForm(const std::vector<std::string>& protocols) {
    std::string out;
    for (const auto& p : protocols) {
        if (p.empty() || p.size() > 255) continue;
        out.push_back(static_cast<char>(p.size()));
        out += p;
    }
    return out;
}

std::string cipherListForm(const std::vector<std::string>& ciphers) {
    std::string out;
    for (const auto& c : ciphers) {
        if (c.empty()) continue;
        if (!out.empty()) out += ":";
        out += c;
    }
    return out;
}

void validateTlsOptions(const TlsOptions& options, bool isServer) {
    if (!tlsProtocolAtLeast12(options.minProtocol)) {
        throw TlsError::thrown(
            TlsErrorCode::kInvalidOptions,
            "minProtocol '" + options.minProtocol + "' is below the TLS1.2 floor");
    }
    if (!tlsProtocolAtLeast12(options.maxProtocol)) {
        throw TlsError::thrown(
            TlsErrorCode::kInvalidOptions,
            "maxProtocol '" + options.maxProtocol + "' is below the TLS1.2 floor");
    }
    if (tlsProtocolVersion(options.minProtocol) > tlsProtocolVersion(options.maxProtocol)) {
        throw TlsError::thrown(TlsErrorCode::kInvalidOptions,
                               "minProtocol is above maxProtocol");
    }
    if (options.maxSendFragment < 256 || options.maxSendFragment > 65536) {
        throw TlsError::thrown(TlsErrorCode::kInvalidOptions,
                               "maxSendFragment must be between 256 and 65536");
    }
    if (options.handshakeTimeoutMs < 0 || options.maxHandshakesPerSecond < 0 ||
        options.maxConcurrentHandshakes < 0) {
        throw TlsError::thrown(TlsErrorCode::kInvalidOptions,
                               "handshakeTimeoutMs / rate limits must not be negative");
    }
    if (isServer && (options.cert.empty() || options.key.empty())) {
        throw TlsError::thrown(TlsErrorCode::kInvalidOptions,
                               "createTlsServer requires options.cert and options.key (PEM)");
    }
    for (const auto& p : options.alpnProtocols) {
        if (p.empty() || p.size() > 255) {
            throw TlsError::thrown(TlsErrorCode::kInvalidOptions,
                                   "ALPN protocol ids must be 1..255 bytes");
        }
    }
}

// ---------------------------------------------------------------------------
// Backend: stub (always compiled) and OpenSSL (CP3_TLS_OPENSSL only).
// ---------------------------------------------------------------------------
namespace {

[[noreturn]] void backendUnavailable() {
    throw TlsError::thrown(TlsErrorCode::kBackendUnavailable,
                           "no TLS backend is linked into this program; vendor OpenSSL "
                           "and define CP3_TLS_OPENSSL (see src/std/network/tls/README.md)");
}

class StubContext : public ITlsContext {
public:
    std::string minProtocol() const override { return "TLSv1.2"; }
};

class StubEngine : public ITlsEngine {
public:
    EngineStatus feedCiphertext(const std::uint8_t*, std::size_t) override { return EngineStatus::Failed; }
    EngineStatus handshake() override { return EngineStatus::Failed; }
    EngineStatus readPlaintext(std::uint8_t*, std::size_t, std::size_t& p) override { p = 0; return EngineStatus::Failed; }
    EngineStatus writePlaintext(const std::uint8_t*, std::size_t) override { return EngineStatus::Failed; }
    EngineStatus drainCiphertext(std::uint8_t*, std::size_t, std::size_t& p) override { p = 0; return EngineStatus::Failed; }
    EngineStatus shutdown() override { return EngineStatus::Failed; }
    bool handshakeDone() const override { return false; }
    bool verifyOk() const override { return false; }
    std::string protocol() const override { return ""; }
    std::string alpnProtocol() const override { return ""; }
    std::string servername() const override { return ""; }
    CipherInfo cipher() const override { return {}; }
    PeerCertificate peerCertificate(bool) const override { return {}; }
    std::string lastError() const override { return "tls backend unavailable"; }
};

}  // namespace

#ifdef CP3_TLS_OPENSSL
namespace {

// The CA bundle lookup that a TLS-linked program uses: $SSL_CERT_FILE if
// set (operator override), else the Mozilla bundle embedded in this binary
// (no filesystem dependency, no source-tree reliance). Failing to load the
// chosen source means no trust anchors at all; the caller fails the attempt
// rather than fall back to an unknown-network OS store.
std::string caPathOverride() {
    if (const char* env = std::getenv("SSL_CERT_FILE")) {
        if (*env) return std::string(env);
    }
    return std::string();  // "" means: use the embedded bundle
}

// Walk the embedded Mozilla bundle, stuff each cert into the ctx store.
// Returns how many certs were added (== 0 is a hard error for the caller).
int loadEmbeddedCa(SSL_CTX* ctx) {
    BIO* bio = BIO_new_mem_buf(detail::cacert_hpp_bytes(),
                               static_cast<int>(detail::cacert_hpp_length()));
    if (!bio) return 0;
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    int added = 0;
    for (;;) {
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (!cert) break;
        if (X509_STORE_add_cert(store, cert) == 1) ++added;
        X509_free(cert);
    }
    BIO_free(bio);
    return added;
}

std::string asn1TimeToIso(const ASN1_TIME* t) {
    if (!t) return std::string();
    const unsigned char* p = ASN1_STRING_get0_data(t);
    const long len = ASN1_STRING_length(t);
    if (!p || (len != 13 && len != 15)) return std::string();
    const std::string s(reinterpret_cast<const char*>(p), static_cast<std::size_t>(len));
    if (len == 15) {
        return s.substr(0, 4) + "-" + s.substr(4, 2) + "-" + s.substr(6, 2) + "T" + s.substr(8, 2) +
               ":" + s.substr(10, 2) + ":" + s.substr(12, 2) + "Z";
    }
    const int yy = (s[0] - '0') * 10 + (s[1] - '0');
    const std::string yyyy = std::to_string(yy < 50 ? 2000 + yy : 1900 + yy);
    return yyyy + "-" + s.substr(2, 2) + "-" + s.substr(4, 2) + "T" + s.substr(6, 2) + ":" +
           s.substr(8, 2) + ":" + s.substr(10, 2) + "Z";
}

std::string colonHexUpper(const unsigned char* data, unsigned int len) {
    std::string out;
    for (unsigned int i = 0; i < len; ++i) {
        char tmp[4];
        std::snprintf(tmp, sizeof(tmp), "%02X", static_cast<unsigned int>(data[i]));
        if (i) out += ':';
        out += tmp;
    }
    return out;
}

class OpensslContext : public ITlsContext {
public:
    SSL_CTX* ctx = nullptr;
    std::string minProto;
    ~OpensslContext() override { if (ctx) SSL_CTX_free(ctx); }
    std::string minProtocol() const override { return minProto; }
};

class OpensslEngine : public ITlsEngine {
public:
    SSL* ssl = nullptr;
    BIO* rbio = nullptr;  // ciphertext in  (uv_read side)
    BIO* wbio = nullptr;  // ciphertext out (uv_write side)

    ~OpensslEngine() override {
        if (ssl) { SSL_free(ssl); }
        // BIOs are owned by ssl after SSL_set_bio; do not free them here.
    }
    EngineStatus feedCiphertext(const std::uint8_t* data, std::size_t size) override {
        if (BIO_write(rbio, data, static_cast<int>(size)) != static_cast<int>(size))
            return EngineStatus::Failed;
        return EngineStatus::WantRead;
    }
    EngineStatus handshake() override {
        int rc = SSL_do_handshake(ssl);
        if (rc == 1) return EngineStatus::Ok;
        int err = SSL_get_error(ssl, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            return EngineStatus::WantRead;
        if (SSL_get_verify_result(ssl) != X509_V_OK) return EngineStatus::VerifyFailed;
        return EngineStatus::Failed;
    }
    EngineStatus readPlaintext(std::uint8_t* out, std::size_t cap, std::size_t& produced) override {
        produced = 0;
        if (!handshakeDone()) return EngineStatus::WantRead;
        int n = SSL_read(ssl, out, static_cast<int>(cap));
        if (n > 0) { produced = static_cast<std::size_t>(n); return EngineStatus::Ok; }
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_ZERO_RETURN) return EngineStatus::Done;
        if (err == SSL_ERROR_WANT_READ) return EngineStatus::WantRead;
        return EngineStatus::Failed;
    }
    EngineStatus writePlaintext(const std::uint8_t* data, std::size_t size) override {
        if (!handshakeDone()) return EngineStatus::Failed;
        int n = SSL_write(ssl, data, static_cast<int>(size));
        return n == static_cast<int>(size) ? EngineStatus::Ok : EngineStatus::Failed;
    }
    EngineStatus drainCiphertext(std::uint8_t* out, std::size_t cap, std::size_t& produced) override {
        produced = 0;
        int n = BIO_read(wbio, out, static_cast<int>(cap));
        if (n > 0) { produced = static_cast<std::size_t>(n); return EngineStatus::Ok; }
        return EngineStatus::WantRead;
    }
    EngineStatus shutdown() override {
        if (SSL_get_shutdown(ssl) & SSL_SENT_SHUTDOWN) return EngineStatus::Done;
        SSL_shutdown(ssl);  // sends close_notify into wbio; drainCiphertext flushes it
        return EngineStatus::Done;
    }
    bool handshakeDone() const override { return ssl && SSL_is_init_finished(ssl) != 0; }
    bool verifyOk() const override { return ssl && SSL_get_verify_result(ssl) == X509_V_OK; }
    std::string protocol() const override {
        if (!handshakeDone()) return "";
        const char* v = SSL_get_version(ssl);
        return v ? std::string(v) : std::string();
    }
    std::string alpnProtocol() const override {
        if (!handshakeDone()) return "";
        const unsigned char* data = nullptr;
        unsigned int len = 0;
        SSL_get0_alpn_selected(ssl, &data, &len);
        return data ? std::string(reinterpret_cast<const char*>(data), len) : std::string();
    }
    std::string servername() const override {
        if (!ssl) return "";
        const char* s = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        return s ? std::string(s) : std::string();
    }
    CipherInfo cipher() const override {
        CipherInfo info;
        if (handshakeDone()) {
            const SSL_CIPHER* c = SSL_get_current_cipher(ssl);
            if (c) info.name = SSL_CIPHER_get_name(c);
            info.version = protocol();
        }
        return info;
    }
    PeerCertificate peerCertificate(bool raw) const override {
        PeerCertificate cert;
        X509* peer = handshakeDone() ? SSL_get_peer_certificate(ssl) : nullptr;
        if (!peer) return cert;
        if (X509_NAME* subj = X509_get_subject_name(peer)) {
            if (char* s = X509_NAME_oneline(subj, nullptr, 0)) { cert.subject = s; OPENSSL_free(s); }
        }
        if (X509_NAME* iss = X509_get_issuer_name(peer)) {
            if (char* s = X509_NAME_oneline(iss, nullptr, 0)) { cert.issuer = s; OPENSSL_free(s); }
        }
        if (ASN1_INTEGER* serial = X509_get_serialNumber(peer)) {
            if (BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr)) {
                if (char* hex = BN_bn2hex(bn)) { cert.serialNumber = hex; OPENSSL_free(hex); }
                BN_free(bn);
            }
        }
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int mdLen = 0;
        if (X509_digest(peer, EVP_sha256(), md, &mdLen) == 1) {
            cert.fingerprint = colonHexUpper(md, mdLen);
        }
        if (const ASN1_TIME* nb = X509_get0_notBefore(peer)) cert.notBefore = asn1TimeToIso(nb);
        if (const ASN1_TIME* na = X509_get0_notAfter(peer)) cert.notAfter = asn1TimeToIso(na);
        if (raw) {
            BIO* bio = BIO_new(BIO_s_mem());
            if (bio) {
                if (PEM_write_bio_X509(bio, peer) == 1) {
                    char* pemData = nullptr;
                    const long pemLen = BIO_get_mem_data(bio, &pemData);
                    if (pemData && pemLen > 0) cert.pem.assign(pemData, static_cast<std::size_t>(pemLen));
                }
                BIO_free(bio);
            }
            cert.raw = true;
        }
        X509_free(peer);
        return cert;
    }
    std::string lastError() const override {
        const unsigned long e = ERR_get_error();
        if (!e) return std::string("tls handshake failed");
        char buf[256];
        ERR_error_string_n(e, buf, sizeof(buf));
        return std::string(buf);
    }
};

}  // namespace
#endif  // CP3_TLS_OPENSSL

bool tlsBackendAvailable() {
#ifdef CP3_TLS_OPENSSL
    return true;
#else
    return false;
#endif
}

std::shared_ptr<ITlsContext> createTlsContext(const TlsOptions& options, bool isServer) {
    validateTlsOptions(options, isServer);
#ifdef CP3_TLS_OPENSSL
    auto ctx = std::make_shared<OpensslContext>();
    const SSL_METHOD* method = isServer ? TLS_server_method() : TLS_client_method();
    ctx->ctx = SSL_CTX_new(method);
    if (!ctx->ctx) backendUnavailable();
    ctx->minProto = options.minProtocol;
    SSL_CTX_set_min_proto_version(ctx->ctx, TLS1_2_VERSION);   // the TLS1.2+ floor
    SSL_CTX_set_max_proto_version(ctx->ctx, TLS1_3_VERSION);
    SSL_CTX_set_verify(ctx->ctx, SSL_VERIFY_PEER, nullptr);    // verification ON
    SSL_CTX_set_default_verify_paths(ctx->ctx);                // additive: OS store when present
    // Load the embedded Mozilla bundle second; SSL_CERT_FILE wins for operators.
    const std::string override = caPathOverride();
    bool loaded = false;
    if (!override.empty()) {
        if (SSL_CTX_load_verify_locations(ctx->ctx, override.c_str(), nullptr) == 1) {
            loaded = true;
        } /* else: keep going - the embedded bundle is the fallback */
    }
    if (!loaded) {
        const int n = loadEmbeddedCa(ctx->ctx);
        if (n == 0) {
            backendUnavailable();  // throws ERR_TLS_BACKEND_UNAVAILABLE – no CA anchors at all
        }
    }
    if (!options.ca.empty()) {
        // PEM text or a file path: try as a file first, then as inline PEM.
        if (SSL_CTX_load_verify_locations(ctx->ctx, options.ca.c_str(), nullptr) != 1) {
            BIO* bio = BIO_new_mem_buf(options.ca.data(), static_cast<int>(options.ca.size()));
            if (bio) {
                if (X509_STORE* store = SSL_CTX_get_cert_store(ctx->ctx)) {
                    for (;;) {
                        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
                        if (!cert) break;
                        X509_STORE_add_cert(store, cert);
                        X509_free(cert);
                    }
                }
                BIO_free(bio);
            }
        }
    }
    if (isServer) {
        if (SSL_CTX_use_certificate_chain_file(ctx->ctx, options.cert.c_str()) != 1 ||
            SSL_CTX_use_PrivateKey_file(ctx->ctx, options.key.c_str(), SSL_FILETYPE_PEM) != 1) {
            throw TlsError::thrown(TlsErrorCode::kInvalidOptions,
                                   "createTlsServer could not load options.cert / options.key (PEM)");
        }
    }
    const std::string alpn = alpnWireForm(options.alpnProtocols);
    if (!alpn.empty())
        SSL_CTX_set_alpn_protos(ctx->ctx,
                                reinterpret_cast<const unsigned char*>(alpn.data()),
                                static_cast<unsigned int>(alpn.size()));
    const std::string ciphers = cipherListForm(options.ciphers);
    if (!ciphers.empty()) SSL_CTX_set_cipher_list(ctx->ctx, ciphers.c_str());
    return ctx;
#else
    (void)options; (void)isServer;
    backendUnavailable();
#endif
}

std::shared_ptr<ITlsEngine> createTlsEngine(const std::shared_ptr<ITlsContext>& ctx,
                                             const TlsOptions& options, bool isServer) {
#ifdef CP3_TLS_OPENSSL
    auto engine = std::make_shared<OpensslEngine>();
    auto octx = std::static_pointer_cast<OpensslContext>(ctx);
    // Memory-BIO pair: libuv owns the socket, OpenSSL never sees an fd.
    engine->rbio = BIO_new(BIO_s_mem());
    engine->wbio = BIO_new(BIO_s_mem());
    BIO_set_mem_eof_return(engine->rbio, -1);
    engine->ssl = SSL_new(octx->ctx);
    if (!engine->ssl) backendUnavailable();
    SSL_set_bio(engine->ssl, engine->rbio, engine->wbio);  // ssl owns both BIOs now
    if (isServer) {
        SSL_set_accept_state(engine->ssl);
    } else {
        SSL_set_connect_state(engine->ssl);
        const std::string sni = options.servername.empty() ? options.host : options.servername;
        if (!sni.empty()) SSL_set_tlsext_host_name(engine->ssl, sni.c_str());  // SNI
        if (options.checkServerIdentity && !options.host.empty())
            SSL_set1_host(engine->ssl, options.host.c_str());                 // hostname check
        SSL_set_verify(engine->ssl, SSL_VERIFY_PEER, nullptr);
    }
    SSL_set_max_send_fragment(engine->ssl, static_cast<int>(options.maxSendFragment));
    return engine;
#else
    (void)ctx; (void)options; (void)isServer;
    backendUnavailable();
#endif
}

// ---------------------------------------------------------------------------
// Pump state. One TlsSocketImpl per connection; the server impl owns the
// listener, the rate bucket and the set of live sockets.
// ---------------------------------------------------------------------------
struct TlsSlots {
    std::function<void()> none;
    std::function<void(std::string)> text;
    std::function<void(const buffer::Buffer&)> binary;
    std::function<void(Thrown)> thr;
};

struct TlsSocketImpl : public std::enable_shared_from_this<TlsSocketImpl> {
    uv_tcp_t tcp{};                 // owned by this impl (never by the engine)
    bool tcpInit = false;
    bool tcpClosing = false;
    std::shared_ptr<ITlsEngine> engine;
    std::shared_ptr<ITlsContext> ctx;
    TlsOptions options;
    bool isServerSide = false;
    bool connecting = true;
    bool secure = false;            // handshake done (encrypted())
    bool destroyed = false;
    bool readEnded = false;
    bool writeEnded = false;
    bool paused = false;
    bool writePending = false;
    std::string remoteAddr;
    double remotePort = 0.0;
    std::string localAddr;
    double localPort = 0.0;
    double bytesIn = 0.0;           // ciphertext bytes read from uv
    double bytesOut = 0.0;          // plaintext bytes accepted by write()
    std::string plaintextOut;       // queued plaintext not yet encrypted
    std::string ciphertextOut;      // encrypted bytes waiting for uv_write
    std::map<std::string, TlsSlots> slots;
    uv_timer_t handshakeTimer{};
    bool handshakeTimerOn = false;
    uv_timer_t idleTimer{};
    bool idleTimerOn = false;
    double idleTimeoutMs = 0.0;
    std::function<void()> idleCallback;
    std::function<void()> onServerSocket;  // server: run the connection handler once
    bool serverHandlerRan = false;
    bool retained = false;

    void startHandshakeTimer(uv_loop_t* loop);
    void stopHandshakeTimer();
    void startIdleTimer(uv_loop_t* loop);
    void stopIdleTimer();
    void emitData(const std::string& plaintext);
    void emitError(Thrown err);
    void emitEvent(const std::string& name);
    void fail(Thrown err);          // emit error (once) + destroy
    void destroy();
    void beginClose();              // close_notify, then uv_close
    void pumpHandshake();
    void pumpReadPlaintext();
    void pumpWrite();
    void flushCiphertext();
};

struct TlsServerImpl {
    uv_tcp_t listener{};
    bool listening = false;
    bool closing = false;
    TlsOptions options;
    std::shared_ptr<ITlsContext> ctx;
    std::map<std::string, std::function<void(TlsSocket&)>> connSlots;
    std::map<std::string, std::function<void(Thrown)>> errSlots;
    std::function<void()> closeSlots;
    std::vector<std::shared_ptr<TlsSocketImpl>> sockets;
    // Handshake rate limiter: token bucket refilled from the libuv loop clock.
    double tokens = 0.0;
    double lastRefillMs = 0.0;
    double activeHandshakes = 0.0;
    bool retained = false;
    std::function<void()> closeCallback;

    bool allowHandshake(uv_loop_t* loop);
    void releaseHandshake();
    void onAccept();
};

// In-flight ciphertext handed to uv_write. Holds the impl alive until the
// write completion callback runs (libuv defers closing handles until their
// pending requests drain).
struct WritePayload {
    std::shared_ptr<TlsSocketImpl> self;
    std::string bytes;
};

void retainOnce(bool& flag) { if (!flag) { flag = true; retainNetwork(); } }
void releaseOnce(bool& flag) { if (flag) { flag = false; releaseNetwork(); } }

void closeTcpWithImpl(uv_tcp_t* tcp) {
    if (!tcp || uv_is_closing(reinterpret_cast<const uv_handle_t*>(tcp))) return;
    auto* impl = static_cast<TlsSocketImpl*>(tcp->data);
    impl->tcpClosing = true;
    impl->stopHandshakeTimer();
    impl->stopIdleTimer();
    uv_close(reinterpret_cast<uv_handle_t*>(tcp), [](uv_handle_t* h) {
        auto* self = static_cast<TlsSocketImpl*>(h->data);
        h->data = nullptr;
        if (self) releaseOnce(self->retained);
        // `self` is owned by the TlsSocket handle / server socket list;
        // dropping the last reference here is safe because uv_close ran.
    });
}

void TlsSocketImpl::startHandshakeTimer(uv_loop_t* loop) {
    const double ms = options.handshakeTimeoutMs > 0 ? options.handshakeTimeoutMs : 10000.0;
    uv_timer_init(loop, &handshakeTimer);
    handshakeTimer.data = this;
    uv_timer_start(&handshakeTimer,
                   [](uv_timer_t* t) {
                       auto* self = static_cast<TlsSocketImpl*>(t->data);
                       self->fail(TlsError::thrown(TlsErrorCode::kHandshakeTimeout,
                                                   "tls handshake timed out"));
                   },
                   static_cast<std::uint64_t>(ms), 0);
    handshakeTimerOn = true;
}

void TlsSocketImpl::stopHandshakeTimer() {
    if (handshakeTimerOn) { uv_timer_stop(&handshakeTimer); handshakeTimerOn = false; }
}

void TlsSocketImpl::startIdleTimer(uv_loop_t* loop) {
    if (idleTimeoutMs <= 0) return;
    uv_timer_init(loop, &idleTimer);
    idleTimer.data = this;
    uv_timer_start(&idleTimer,
                   [](uv_timer_t* t) {
                       auto* self = static_cast<TlsSocketImpl*>(t->data);
                       self->emitEvent("timeout");
                       if (self->idleCallback) self->idleCallback();
                   },
                   static_cast<std::uint64_t>(idleTimeoutMs), 0);
    idleTimerOn = true;
}

void TlsSocketImpl::stopIdleTimer() {
    if (idleTimerOn) { uv_timer_stop(&idleTimer); idleTimerOn = false; }
}

void TlsSocketImpl::emitData(const std::string& plaintext) {
    if (destroyed || paused) return;
    const auto it = slots.find("data");
    if (it == slots.end()) return;
    if (it->second.binary) { it->second.binary(buffer::from(plaintext)); return; }
    if (it->second.text) { it->second.text(plaintext); return; }
    if (it->second.none) it->second.none();
}

void TlsSocketImpl::emitError(Thrown err) {
    const auto it = slots.find("error");
    if (it == slots.end()) return;
    if (it->second.thr) it->second.thr(err);
    else if (it->second.none) it->second.none();
}

void TlsSocketImpl::emitEvent(const std::string& name) {
    const auto it = slots.find(name);
    if (it != slots.end() && it->second.none) it->second.none();
}

void TlsSocketImpl::fail(Thrown err) {
    if (destroyed) return;
    emitError(err);
    destroy();
}

void TlsSocketImpl::destroy() {
    if (destroyed) return;
    destroyed = true;
    connecting = false;
    stopHandshakeTimer();
    stopIdleTimer();
    if (tcpInit && !tcpClosing) closeTcpWithImpl(&tcp);
    emitEvent("close");
}

void TlsSocketImpl::beginClose() {
    if (writeEnded) return;
    writeEnded = true;
    if (engine && secure) engine->shutdown();  // close_notify into the out-BIO
    flushCiphertext();
    if (tcpInit && !tcpClosing) closeTcpWithImpl(&tcp);
}

void TlsSocketImpl::flushCiphertext() {
    if (destroyed || writePending || ciphertextOut.empty() || !engine) { writePending = false; return; }
    std::uint8_t buf[16384];
    std::size_t produced = 0;
    while (engine->drainCiphertext(buf, sizeof(buf), produced) == EngineStatus::Ok && produced > 0) {
        ciphertextOut.append(reinterpret_cast<char*>(buf), produced);
        produced = 0;
    }
    if (ciphertextOut.empty()) return;
    // uv_write borrows the buffer until the callback; hand the bytes to the loop.
    auto* payload = new WritePayload{shared_from_this(), std::move(ciphertextOut)};
    ciphertextOut.clear();
    uv_buf_t uvbuf = uv_buf_init(payload->bytes.data(), static_cast<unsigned int>(payload->bytes.size()));
    writePending = true;
    auto* req = new uv_write_t;
    req->data = payload;
    uv_write(req, reinterpret_cast<uv_stream_t*>(&tcp), &uvbuf, 1,
             [](uv_write_t* req, int status) {
                 std::unique_ptr<WritePayload> payload(static_cast<WritePayload*>(req->data));
                 delete req;
                 if (status < 0) {
                     payload->self->writePending = false;
                     payload->self->fail(TlsError::thrown(TlsErrorCode::kHandshakeFailed,
                                                          "tls transport write failed"));
                     return;
                 }
                 payload->self->writePending = false;
                 payload->self->flushCiphertext();
             });
}

void TlsSocketImpl::pumpWrite() {
    if (!secure || plaintextOut.empty() || !engine) return;
    const auto chunk = plaintextOut;
    plaintextOut.clear();
    std::size_t off = 0;
    std::uint8_t buf[16384];
    while (off < chunk.size()) {
        const std::size_t n = chunk.size() - off > sizeof(buf) ? sizeof(buf) : chunk.size() - off;
        if (engine->writePlaintext(reinterpret_cast<const std::uint8_t*>(chunk.data()) + off, n) != EngineStatus::Ok) {
            plaintextOut = chunk.substr(off) + plaintextOut;  // requeue, fail closed next pump
            break;
        }
        off += n;
        bytesOut += static_cast<double>(n);
    }
    flushCiphertext();
}

void TlsSocketImpl::pumpHandshake() {
    if (!engine || secure) return;
    EngineStatus st = EngineStatus::WantRead;
    // Feed-then-handshake loop bounded so one read burst cannot starve the loop.
    for (int i = 0; i < 16; ++i) {
        st = engine->handshake();
        if (st != EngineStatus::WantRead) break;
        // WantRead: wait for more ciphertext (already fed by the read pump).
        break;
    }
    flushCiphertext();
    if (st == EngineStatus::Ok) {
        if (!engine->verifyOk()) {
            fail(TlsError::thrown(TlsErrorCode::kCertVerifyFailed,
                                  "certificate verification failed: " + engine->lastError()));
            return;
        }
        secure = true;
        connecting = false;
        stopHandshakeTimer();
        emitEvent("connect");         // TcpSocket parity
        emitEvent("secureConnect");   // TLS event
        if (onServerSocket && !serverHandlerRan) { serverHandlerRan = true; onServerSocket(); }
        pumpWrite();
        pumpReadPlaintext();
        return;
    }
    if (st == EngineStatus::VerifyFailed) {
        fail(TlsError::thrown(TlsErrorCode::kCertVerifyFailed,
                              "certificate verification failed: " + engine->lastError()));
    } else if (st == EngineStatus::ProtocolTooOld) {
        fail(TlsError::thrown(TlsErrorCode::kProtocolTooOld,
                              "peer negotiated a protocol below TLS1.2"));
    } else if (st == EngineStatus::Failed) {
        fail(TlsError::thrown(TlsErrorCode::kHandshakeFailed,
                              "tls handshake failed: " + engine->lastError()));
    }
}

void TlsSocketImpl::pumpReadPlaintext() {
    if (!secure || !engine) return;
    std::uint8_t buf[16384];
    for (int i = 0; i < 64; ++i) {
        std::size_t produced = 0;
        EngineStatus st = engine->readPlaintext(buf, sizeof(buf), produced);
        if (st == EngineStatus::Ok && produced > 0) {
            emitData(std::string(reinterpret_cast<char*>(buf), produced));
            continue;
        }
        if (st == EngineStatus::Done) {
            readEnded = true;
            emitEvent("end");
            beginClose();
            return;
        }
        break;
    }
}

bool TlsServerImpl::allowHandshake(uv_loop_t* loop) {
    const double nowMs = static_cast<double>(uv_hrtime()) / 1e6;
    if (lastRefillMs == 0) { lastRefillMs = nowMs; tokens = options.maxHandshakesPerSecond > 0 ? options.maxHandshakesPerSecond : 1e9; }
    if (options.maxHandshakesPerSecond > 0) {
        tokens += (nowMs - lastRefillMs) * options.maxHandshakesPerSecond / 1000.0;
        if (tokens > options.maxHandshakesPerSecond) tokens = options.maxHandshakesPerSecond;
    }
    lastRefillMs = nowMs;
    (void)loop;
    if (options.maxHandshakesPerSecond > 0 && tokens < 1.0) return false;
    if (options.maxConcurrentHandshakes > 0 && activeHandshakes >= options.maxConcurrentHandshakes) return false;
    if (options.maxHandshakesPerSecond > 0) tokens -= 1.0;
    activeHandshakes += 1.0;
    return true;
}

void TlsServerImpl::releaseHandshake() { if (activeHandshakes > 0) activeHandshakes -= 1.0; }

void TlsServerImpl::onAccept() {
    auto impl = std::make_shared<TlsSocketImpl>();
    impl->options = options;
    impl->ctx = ctx;
    impl->isServerSide = true;
    try {
        impl->engine = createTlsEngine(ctx, options, true);
    } catch (const Thrown& err) {
        impl->fail(err);
        return;
    }
    impl->tcp.data = impl.get();
    uv_tcp_init(reinterpret_cast<uv_loop_t*>(loopHandle()), &impl->tcp);
    impl->tcpInit = true;
    retainOnce(impl->retained);
    if (uv_accept(reinterpret_cast<uv_stream_t*>(&listener),
                  reinterpret_cast<uv_stream_t*>(&impl->tcp)) != 0) {
        impl->destroy();
        return;
    }
    uv_tcp_nodelay(&impl->tcp, 1);
    if (!allowHandshake(reinterpret_cast<uv_loop_t*>(loopHandle()))) {
        impl->fail(TlsError::thrown(TlsErrorCode::kHandshakeRateLimited,
                                    "tls handshake rate limit exceeded"));
        return;
    }
    impl->onServerSocket = [this, impl] {
        releaseHandshake();
        TlsSocket socket = TlsSocket::adopt(impl);
        for (auto& [name, handler] : connSlots) {
            if (name == "secureConnection" && handler) handler(socket);
        }
    };
    impl->startHandshakeTimer(reinterpret_cast<uv_loop_t*>(loopHandle()));
    uv_read_start(reinterpret_cast<uv_stream_t*>(&impl->tcp),
                  [](uv_handle_t*, std::size_t suggested, uv_buf_t* buf) {
                      buf->base = new char[suggested]; buf->len = static_cast<unsigned int>(suggested);
                  },
                  [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                      auto* self = static_cast<TlsSocketImpl*>(stream->data);
                      std::unique_ptr<char[]> owned(buf->base);
                      if (nread < 0) {
                          if (nread != UV_EOF)
                              self->fail(TlsError::thrown(TlsErrorCode::kHandshakeFailed,
                                                          std::string("tcp read failed: ") + uv_strerror(static_cast<int>(nread))));
                          else { self->readEnded = true; self->emitEvent("end"); self->beginClose(); }
                          return;
                      }
                      if (nread == 0) return;
                      self->bytesIn += static_cast<double>(nread);
                      self->engine->feedCiphertext(reinterpret_cast<const std::uint8_t*>(buf->base),
                                                   static_cast<std::size_t>(nread));
                      self->pumpHandshake();
                      self->pumpReadPlaintext();
                  });
    sockets.push_back(impl);
}

}  // namespace detail

// =============================================================================
// TlsSocket
// =============================================================================
TlsSocket::TlsSocket()
    : impl_(std::make_shared<detail::TlsSocketImpl>()) {}

TlsSocket::TlsSocket(void* tcpSocket, const TlsOptions& options)
    : impl_(std::make_shared<detail::TlsSocketImpl>()) {
    if (!tcpSocket) throw TlsError::thrown(TlsErrorCode::kInvalidOptions, "TlsSocket requires a TcpSocket");
    detail::validateTlsOptions(options, false);
    auto impl = impl_;
    impl->options = options;
    impl->ctx = detail::createTlsContext(options, false);  // throws when the backend is unavailable
    impl->engine = detail::createTlsEngine(impl->ctx, options, false);
    auto* adopted = static_cast<uv_tcp_t*>(tcpSocket);
    impl->tcp = *adopted;  // adopt the underlying handle (libuv still owns the fd)
    impl->tcp.data = impl.get();
    impl->tcpInit = true;
    detail::retainOnce(impl->retained);
    impl->connecting = true;
    impl->startHandshakeTimer(reinterpret_cast<uv_loop_t*>(loopHandle()));
}

TlsSocket::~TlsSocket() {
    if (impl_) impl_->destroy();
}

TlsSocket::TlsSocket(TlsSocket&& other) noexcept : impl_(std::move(other.impl_)) {}
TlsSocket& TlsSocket::operator=(TlsSocket&& other) noexcept {
    if (this != &other) { if (impl_) impl_->destroy(); impl_ = std::move(other.impl_); }
    return *this;
}

TlsSocket TlsSocket::adopt(std::shared_ptr<detail::TlsSocketImpl> impl) {
    TlsSocket socket;
    socket.impl_ = std::move(impl);
    return socket;
}

void TlsSocket::on(const std::string& event, std::function<void()> handler) const {
    impl_->slots[event].none = std::move(handler);
}
void TlsSocket::on(const std::string& event, std::function<void(std::string)> handler) const {
    impl_->slots[event].text = std::move(handler);
}
void TlsSocket::on(const std::string& event, std::function<void(const buffer::Buffer&)> handler) const {
    impl_->slots[event].binary = std::move(handler);
}
void TlsSocket::on(const std::string& event, std::function<void(Thrown)> handler) const {
    impl_->slots[event].thr = std::move(handler);
}

bool TlsSocket::write(const std::string& data) const {
    if (impl_->destroyed || impl_->writeEnded) return false;
    if (!impl_->secure) {
        impl_->emitError(TlsError::thrown(TlsErrorCode::kNotConnected,
                                          "write before secureConnect"));
        return false;
    }
    impl_->plaintextOut += data;
    impl_->pumpWrite();
    return true;
}
bool TlsSocket::write(const buffer::Buffer& data) const {
    return write(data.bytes());
}

void TlsSocket::end() const { impl_->beginClose(); }
void TlsSocket::end(const std::string& data) const { write(data); impl_->beginClose(); }
void TlsSocket::end(const buffer::Buffer& data) const { write(data); impl_->beginClose(); }
void TlsSocket::destroy() const { impl_->destroy(); }
void TlsSocket::destroy(Thrown err) const { impl_->emitError(err); impl_->destroy(); }
void TlsSocket::close() const { impl_->beginClose(); }

void TlsSocket::pause() const { impl_->paused = true; }
void TlsSocket::resume() const { impl_->paused = false; }

void TlsSocket::setTimeout(double ms, std::function<void()> callback) const {
    impl_->idleTimeoutMs = ms;
    impl_->idleCallback = std::move(callback);
    impl_->stopIdleTimer();
    impl_->startIdleTimer(reinterpret_cast<uv_loop_t*>(loopHandle()));
}
void TlsSocket::setNoDelay(bool enabled) const {
    if (impl_->tcpInit) uv_tcp_nodelay(&impl_->tcp, enabled ? 1 : 0);
}
void TlsSocket::setKeepAlive(bool enabled, double delayMs) const {
    if (impl_->tcpInit) uv_tcp_keepalive(&impl_->tcp, enabled ? 1 : 0, static_cast<unsigned int>(delayMs / 1000.0));
}
void TlsSocket::connect(double port, const std::string& host) {
    // Re-connect of a detached socket: build fresh options from the stored
    // ones so verification/SNI/ALPN are identical to the first connect.
    TlsOptions options = impl_->options;
    options.host = host.empty() ? options.host : host;
    options.port = port;
    TlsSocket fresh = connectTls(options);
    *this = std::move(fresh);
}

bool TlsSocket::connecting() const { return impl_->connecting; }
bool TlsSocket::destroyed() const { return impl_->destroyed; }
bool TlsSocket::readable() const { return impl_->options.readable && !impl_->readEnded && !impl_->destroyed; }
bool TlsSocket::readableEnded() const { return impl_->readEnded; }
bool TlsSocket::writable() const { return impl_->options.writable && impl_->secure && !impl_->writeEnded && !impl_->destroyed; }
bool TlsSocket::writableEnded() const { return impl_->writeEnded; }
std::string TlsSocket::remoteAddress() const { return impl_->remoteAddr; }
double TlsSocket::remotePort() const { return impl_->remotePort; }
std::string TlsSocket::localAddress() const { return impl_->localAddr; }
double TlsSocket::localPort() const { return impl_->localPort; }
double TlsSocket::bytesRead() const { return impl_->bytesIn; }
double TlsSocket::bytesWritten() const { return impl_->bytesOut; }

bool TlsSocket::authorized() const { return impl_->secure && impl_->engine && impl_->engine->verifyOk(); }
bool TlsSocket::encrypted() const { return impl_->secure; }
std::string TlsSocket::alpnProtocol() const { return impl_->engine ? impl_->engine->alpnProtocol() : std::string(); }
std::string TlsSocket::servername() const { return impl_->engine ? impl_->engine->servername() : std::string(); }
std::string TlsSocket::getProtocol() const { return impl_->engine ? impl_->engine->protocol() : std::string(); }
CipherInfo TlsSocket::getCipher() const { return impl_->engine ? impl_->engine->cipher() : CipherInfo{}; }
PeerCertificate TlsSocket::getPeerCertificate(bool raw) const {
    if (!impl_->secure) throw TlsError::thrown(TlsErrorCode::kNoPeerCertificate, "no peer certificate before secureConnect");
    return impl_->engine->peerCertificate(raw);
}
void TlsSocket::setMaxSendFragment(std::size_t size) const { impl_->options.maxSendFragment = size; }
std::size_t TlsSocket::getMaxSendFragment() const { return impl_->options.maxSendFragment; }

// =============================================================================
// TlsServer
// =============================================================================
TlsServer::TlsServer(TlsOptions options) : impl_(std::make_shared<detail::TlsServerImpl>()) {
    detail::validateTlsOptions(options, true);
    impl_->options = options;
    impl_->ctx = detail::createTlsContext(options, true);  // throws when the backend is unavailable
}

TlsServer::~TlsServer() { close(); }

TlsServer::TlsServer(TlsServer&& other) noexcept : impl_(std::move(other.impl_)) {}
TlsServer& TlsServer::operator=(TlsServer&& other) noexcept {
    if (this != &other) {
        if (impl_) close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void TlsServer::on(const std::string& event, std::function<void(TlsSocket&)> handler) const {
    impl_->connSlots[event] = std::move(handler);
}
void TlsServer::on(const std::string& event, std::function<void(Thrown)> handler) const {
    impl_->errSlots[event] = std::move(handler);
}
void TlsServer::on(const std::string& event, std::function<void()> handler) const {
    if (event == "close") impl_->closeSlots = std::move(handler);
}

void TlsServer::listen(double port, std::function<void()> callback) const {
    listen(port, "", std::move(callback));
}

void TlsServer::listen(double port, const std::string& host, std::function<void()> callback) const {
    ensureLoop();
    auto* loop = reinterpret_cast<uv_loop_t*>(loopHandle());
    uv_tcp_init(loop, &impl_->listener);
    impl_->listener.data = impl_.get();
    detail::retainOnce(impl_->retained);
    sockaddr_storage addr{};
    int family = AF_INET;
    if (!host.empty() && host.find(':') != std::string::npos) family = AF_INET6;
    if (family == AF_INET) {
        auto* a = reinterpret_cast<sockaddr_in*>(&addr);
        a->sin_family = AF_INET;
        a->sin_port = htons(static_cast<std::uint16_t>(port));
        if (host.empty()) a->sin_addr.s_addr = htonl(INADDR_ANY);
        else if (uv_inet_pton(AF_INET, host.c_str(), &a->sin_addr) != 0) {
            for (auto& [name, h] : impl_->errSlots)
                if (name == "error" && h)
                    h(TlsError::thrown(TlsErrorCode::kInvalidOptions,
                                       "listen host must be an IP literal in v1 (see README limitations)"));
            return;
        }
    } else {
        auto* a = reinterpret_cast<sockaddr_in6*>(&addr);
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(static_cast<std::uint16_t>(port));
        if (!host.empty() && uv_inet_pton(AF_INET6, host.c_str(), &a->sin6_addr) != 0) {
            for (auto& [name, h] : impl_->errSlots)
                if (name == "error" && h)
                    h(TlsError::thrown(TlsErrorCode::kInvalidOptions, "listen host must be an IP literal in v1"));
            return;
        }
    }
    if (uv_tcp_bind(&impl_->listener, reinterpret_cast<const sockaddr*>(&addr),
                    family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in)) != 0 ||
        uv_listen(reinterpret_cast<uv_stream_t*>(&impl_->listener), 128,
                  [](uv_stream_t* stream, int /*status*/) {
                      auto* self = static_cast<detail::TlsServerImpl*>(stream->data);
                      self->onAccept();
                  }) != 0) {
        for (auto& [name, h] : impl_->errSlots)
            if (name == "error" && h) h(TlsError::thrown(TlsErrorCode::kHandshakeFailed, "could not listen (port already in use?)"));
        return;
    }
    impl_->listening = true;
    if (callback) callback();
}

void TlsServer::close(std::function<void()> callback) const {
    if (!impl_->listening) { if (callback) callback(); return; }
    impl_->listening = false;
    impl_->closeCallback = std::move(callback);
    for (auto& socket : impl_->sockets) socket->destroy();  // in-flight handshakes die here
    uv_close(reinterpret_cast<uv_handle_t*>(&impl_->listener), [](uv_handle_t* h) {
        auto* self = static_cast<detail::TlsServerImpl*>(h->data);
        detail::releaseOnce(self->retained);
        if (self->closeSlots) self->closeSlots();
        if (self->closeCallback) self->closeCallback();
    });
}

double TlsServer::addressPort() const {
    if (!impl_->listening) return 0.0;
    sockaddr_storage addr{};
    int len = sizeof(addr);
    if (uv_tcp_getsockname(&impl_->listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0.0;
    if (addr.ss_family == AF_INET6) return static_cast<double>(ntohs(reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port));
    return static_cast<double>(ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port));
}

TlsServer createTlsServer(const TlsOptions& options, std::function<void(TlsSocket&)> handler) {
    TlsServer server(options);
    if (handler) server.on("secureConnection", std::move(handler));
    return std::move(server);
}

// =============================================================================
// connectTls
// =============================================================================
namespace {

struct ConnectState {
    std::shared_ptr<detail::TlsSocketImpl> impl;
    uv_getaddrinfo_t req{};
};

void connectWithAddr(ConnectState* state, struct sockaddr* addr, int addrLen) {
    (void)addrLen;  // family comes from the sockaddr itself; libuv reads it
    auto* loop = reinterpret_cast<uv_loop_t*>(loopHandle());
    auto* req = new uv_connect_t{};
    req->data = state;
    uv_tcp_connect(req, &state->impl->tcp, addr, [](uv_connect_t* req, int status) {
        auto* state = static_cast<ConnectState*>(req->data);
        auto holder = std::move(state->impl);  // keep impl alive across delete
        detail::TlsSocketImpl* self = holder.get();
        delete req;
        delete state;
        if (status != 0) {
            if (holder) holder->fail(TlsError::thrown(TlsErrorCode::kHandshakeFailed,
                                                      std::string("tcp connect failed: ") + uv_strerror(status)));
            return;
        }
        if (!holder) return;
        holder->pumpHandshake();  // client speaks first
        uv_read_start(reinterpret_cast<uv_stream_t*>(&holder->tcp),
                      [](uv_handle_t*, std::size_t suggested, uv_buf_t* buf) {
                          buf->base = new char[suggested]; buf->len = static_cast<unsigned int>(suggested);
                      },
                      [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                          auto* self = static_cast<detail::TlsSocketImpl*>(stream->data);
                          std::unique_ptr<char[]> owned(buf->base);
                          if (nread < 0) {
                              if (nread != UV_EOF)
                                  self->fail(TlsError::thrown(TlsErrorCode::kHandshakeFailed,
                                                              std::string("tcp read failed: ") + uv_strerror(static_cast<int>(nread))));
                              else { self->readEnded = true; self->emitEvent("end"); self->beginClose(); }
                              return;
                          }
                          if (nread == 0) return;
                          self->bytesIn += static_cast<double>(nread);
                          self->engine->feedCiphertext(reinterpret_cast<const std::uint8_t*>(buf->base),
                                                       static_cast<std::size_t>(nread));
                          self->pumpHandshake();
                          self->pumpReadPlaintext();
                      });
    });
}

}  // namespace

TlsSocket connectTls(const TlsOptions& options) {
    detail::validateTlsOptions(options, false);
    if (options.host.empty())
        throw TlsError::thrown(TlsErrorCode::kInvalidOptions, "connectTls requires options.host");
    ensureLoop();
    auto* loop = reinterpret_cast<uv_loop_t*>(loopHandle());

    // Build the connection state FIRST: everything fallible throws here and
    // leaves nothing on the loop, so the caller's synchronous catch (or the
    // stub backend) never strands a half-open pump.
    auto impl = std::make_shared<detail::TlsSocketImpl>();
    impl->options = options;
    impl->ctx = detail::createTlsContext(options, false);   // throws when the backend is unavailable
    impl->engine = detail::createTlsEngine(impl->ctx, options, false);

    auto state = new ConnectState();
    state->impl = std::move(impl);
    state->impl->tcp.data = state->impl.get();
    uv_tcp_init(loop, &state->impl->tcp);
    state->impl->tcpInit = true;
    detail::retainOnce(state->impl->retained);
    state->impl->connecting = true;
    state->impl->startHandshakeTimer(loop);
    TlsSocket socket = TlsSocket::adopt(state->impl);

    // DNS: uv_getaddrinfo (async, off the calling thread's stack) — never a
    // blocking getaddrinfo on the event loop.
    state->req.data = state;
    const std::string portText = std::to_string(static_cast<int>(options.port));
    uv_getaddrinfo(loop, &state->req,
                   [](uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
                       auto* state = static_cast<ConnectState*>(req->data);
                       if (status != 0 || !res) {
                           if (res) uv_freeaddrinfo(res);
                           auto* self = state->impl.get();
                           self->fail(TlsError::thrown(TlsErrorCode::kHandshakeFailed,
                                                       std::string("dns lookup failed for '") + self->options.host + "'"));
                           state->impl.reset();  // socket owns it; drop the ConnectState ref
                           delete state;
                           return;
                       }
                       connectWithAddr(state, res->ai_addr, res->ai_addrlen);
                       uv_freeaddrinfo(res);
                   },
                   options.host.c_str(), portText.c_str(), nullptr);
    return std::move(socket);
}

TlsSocket connectTls(const std::string& host, double port, const TlsOptions& extra) {
    TlsOptions options = extra;
    options.host = host;
    options.port = port;
    return connectTls(options);
}

}  // namespace cp3::runtime::network::tls
