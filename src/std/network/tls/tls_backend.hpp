// =============================================================================
// src/std/network/tls/tls_backend.hpp - PRIVATE TLS backend boundary.
//
// OWNERSHIP: the std/network/tls family (src/std/network/tls/**). Never
// installed, never part of the generated-code contract. This header is the
// ONLY place that names a TLS library: runtime_tls.cpp implements it against
// OpenSSL when CP3_TLS_OPENSSL is defined, and against a fail-closed stub
// otherwise. Swapping libraries (mbedTLS, BoringSSL) means writing one new
// implementation of the two interfaces below; the engine pump, timers, rate
// limiter and event slots in runtime_tls.cpp do not change.
//
// -----------------------------------------------------------------------------
// ENGINE MODEL (the part that must not change across backends)
// -----------------------------------------------------------------------------
// libuv owns the socket. The TLS engine NEVER sees a file descriptor: it is
// fed ciphertext through a memory BIO pair and hands ciphertext back through
// the second BIO, exactly the OpenSSL BIO_s_mem pattern. The pump is:
//
//   uv read (ciphertext) ──► rbio ──► SSL_do_handshake / SSL_read (plaintext)
//   SSL_write (plaintext) ──► wbio ──► uv_write (ciphertext)
//
// That keeps every uv_* call in runtime_tls.cpp, makes the backend reusable
// for the uWS server transport later, and avoids the classic bug of letting
// OpenSSL drive a busy libuv loop (or vice versa).
//
// -----------------------------------------------------------------------------
// OPENSSL MAPPING (the contract the OpenSSL implementation must honour)
// -----------------------------------------------------------------------------
//   context create (client): SSL_CTX_new(TLS_client_method());
//       SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);   // TLS1.2+ floor
//       SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);   // or per options
//       SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
//       SSL_CTX_set_default_verify_paths(ctx);                // OS trust store
//       SSL_CTX_load_verify_locations(ctx, caFile, nullptr);  // when ca given
//       SSL_CTX_use_certificate_chain_file / _PrivateKey_file // server + mTLS
//       SSL_CTX_set_alpn_protos(ctx, len-prefixed list);
//       SSL_CTX_set_cipher_list(ctx, join(ciphers, ":"));     // TLS<=1.2 only
//   context create (server): SSL_CTX_new(TLS_server_method()); same version
//       floor, cert/key required. SNI dispatch: SSL_CTX_set_tlsext_servername_callback
//       is OUT of scope for v1 (single context per TlsServer); documented.
//   per-connection: SSL_new(ctx); pair of BIO_s_mem; SSL_set_bio(ssl, rbio,
//       wbio); SSL_set_connect_state / SSL_set_accept_state;
//       client: SSL_set_tlsext_host_name(ssl, servername)  (SNI)
//       client: SSL_set1_host(ssl, hostname) when checkServerIdentity
//       client: SSL_set_verify(ssl, SSL_VERIFY_PEER) (again, belt and braces)
//       server: SSL_set_alpn_selected not needed; SSL_get0_alpn_selected
//       reads the negotiated id; SSL_set_max_send_fragment(ssl, size).
//   handshake pump: SSL_do_handshake until SSL_get_error == SSL_ERROR_NONE;
//       SSL_ERROR_WANT_READ is the normal "need more ciphertext" stop, never
//       an error (the BIO is non-blocking by construction).
//   verify result: SSL_get_verify_result(ssl) != X509_V_OK  =>  CERT_UNTRUSTED,
//       even when the handshake itself succeeded (fail closed, no partial trust).
//   renegotiation: SSL_renegotiate is never called; TLS1.3 has none.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cp3/std/network/tls.hpp"

namespace cp3::runtime::network::tls::detail {

/// Outcome of one non-blocking engine step. `WantRead` is the normal stop
/// (more ciphertext needed); everything else except `Ok` is terminal.
enum class EngineStatus {
    Ok,               // progress made, plaintext available / consumed
    WantRead,         // needs more ciphertext from the peer
    WantWrite,        // RHSO: memory BIOs never block; kept for interface parity
    Done,             // close_notify seen / shutdown complete
    VerifyFailed,     // certificate chain or hostname check failed (terminal)
    ProtocolTooOld,   // peer negotiated below the TLS1.2 floor (terminal)
    Failed,           // any other backend failure (terminal)
};

/// One TLS context (the SSL_CTX analogue): trust store, cert/key, protocol
/// floor/ceiling, ALPN list, cipher list. Shared by all connections of one
/// TlsServer / one connectTls call. `createContext` throws TlsError.
class ITlsContext {
public:
    virtual ~ITlsContext() = default;
    virtual std::string minProtocol() const = 0;  // "TLSv1.2" etc. (for diagnostics)
};

/// One connection's engine (the SSL analogue). All methods are non-blocking;
/// the pump in runtime_tls.cpp owns the libuv side. `feedCiphertext` copies
/// the bytes (the caller may reuse its buffer immediately).
class ITlsEngine {
public:
    virtual ~ITlsEngine() = default;
    /// Push ciphertext received from uv. Returns WantRead on success.
    virtual EngineStatus feedCiphertext(const std::uint8_t* data, std::size_t size) = 0;
    /// Drive the handshake one step. Ok == handshake complete.
    virtual EngineStatus handshake() = 0;
    /// Read decrypted plaintext (up to `capacity` bytes). WantRead = none yet.
    virtual EngineStatus readPlaintext(std::uint8_t* out, std::size_t capacity,
                                       std::size_t& produced) = 0;
    /// Encrypt plaintext into the out-BIO. WantWrite only if the out-BIO is full.
    virtual EngineStatus writePlaintext(const std::uint8_t* data, std::size_t size) = 0;
    /// Drain ciphertext produced by writePlaintext/handshake (up to capacity).
    virtual EngineStatus drainCiphertext(std::uint8_t* out, std::size_t capacity,
                                         std::size_t& produced) = 0;
    /// Send close_notify and report Done when the out-BIO is drained.
    virtual EngineStatus shutdown() = 0;
    /// True once the handshake completed (authorized() source of truth when
    /// combined with verifyOk()).
    virtual bool handshakeDone() const = 0;
    virtual bool verifyOk() const = 0;
    virtual std::string protocol() const = 0;       // "TLSv1.2" / "TLSv1.3" / ""
    virtual std::string alpnProtocol() const = 0;   // negotiated ALPN id / ""
    virtual std::string servername() const = 0;     // SNI sent (client) / received (server)
    virtual CipherInfo cipher() const = 0;
    virtual PeerCertificate peerCertificate(bool raw) const = 0;
    virtual std::string lastError() const = 0;      // backend error text for TlsError
};

/// Factory. The stub implementation (always available) fails closed: every
/// create* throws ERR_TLS_BACKEND_UNAVAILABLE. The OpenSSL implementation is
/// compiled only under CP3_TLS_OPENSSL (see runtime_tls.cpp).
std::shared_ptr<ITlsContext> createTlsContext(const TlsOptions& options, bool isServer);
std::shared_ptr<ITlsEngine> createTlsEngine(const std::shared_ptr<ITlsContext>& ctx,
                                             const TlsOptions& options, bool isServer);
/// True when a real backend is linked (OpenSSL). Sema/codegen may use this to
/// decide whether to warn at codegen time; runtime never branches on it.
bool tlsBackendAvailable();

/// Pure helpers shared by both backends (and unit-testable without a backend).
/// "TLSv1.2" -> 0x0303, "TLSv1.3" -> 0x0304; unknown spellings return 0.
std::uint16_t tlsProtocolVersion(const std::string& name);
/// True when `name` is at or above the TLS1.2 floor ("TLSv1.2"/"TLSv1.3").
bool tlsProtocolAtLeast12(const std::string& name);
/// Length-prefixed ALPN wire form ("h2" -> "\x02h2"); "" entries skipped.
std::string alpnWireForm(const std::vector<std::string>& protocols);
/// "a:b:c" join used for SSL_CTX_set_cipher_list; "" when empty.
std::string cipherListForm(const std::vector<std::string>& ciphers);
/// Validates the options that must fail before any socket exists. Throws
/// TlsError(ERR_TLS_INVALID_OPTIONS) on the first problem found.
void validateTlsOptions(const TlsOptions& options, bool isServer);

}  // namespace cp3::runtime::network::tls::detail
