# TLS over TcpSocket (`createTlsServer` / `connectTls` / `TlsSocket`)

SOLO subtree: `src/std/network/tls/**` only. No shared file was touched, nothing
was built or run. The integrator (lead) applies the WIRING lines below.

## Library pick: OpenSSL (memory-BIO engine), stub until vendored

**No TLS library is vendored in `third_party/` today.** `third_party/MANIFEST.md`
(caveat 3, "Verified consumer flags") records uSockets built `LIBUS_NO_SSL` and
states that wss/TLS would need the archives rebuilt with an SSL backend. There
is no OpenSSL, mbedTLS or BoringSSL tree, archive or `VERSION.txt` anywhere in
the tree (checked: `third_party/**` carries only libsodium, libuv, uWebSockets,
oneTBB, PCRE2 and the Ledger-F header/compilable libs).

Pick: **OpenSSL** (`libssl`/`libcrypto`, static, same pin-and-build recipe as
libuv/oneTBB, e.g. OpenSSL 3.x LTS from `openssl/openssl`). Rationale:

- It is the only candidate whose memory-BIO API (`BIO_s_mem`, `SSL_set_bio`)
  matches the required architecture exactly: libuv owns the socket, the engine
  never sees an fd, and one pump (`feedCiphertext → handshake/readPlaintext`,
  `writePlaintext → drainCiphertext → uv_write`) serves client and server.
- TLS1.2 floor, SNI, ALPN, `SSL_VERIFY_PEER` + default verify paths and host
  verification (`SSL_set1_host`) are all one-call, version-pinned operations.
- BoringSSL was rejected: it is a TLS-only fork with no stable C ABI across
  releases (vendoring risk) and weaker distro-static story. mbedTLS was
  rejected as second choice: smaller, but its context/PSA API diverges more
  from the `SSL_CTX` shape this design mirrors and its ALPN/SNI story is
  thinner. Either can still be adopted later by implementing the two
  interfaces in `tls_backend.hpp` — the pump does not change.

**Until vendored, the tree ships a fail-closed stub.** Defining
`CP3_TLS_OPENSSL` and linking the vendor archives swaps in the real engine;
without it, `connectTls()` / `createTlsServer()` / `new TlsSocket()` throw a
`TLSError` (`ERR_TLS_BACKEND_UNAVAILABLE`) and no plaintext fallback, silent
downgrade or skip-verification path exists anywhere in the code.

## Files

| File | What |
|---|---|
| `include/cp3/std/network/tls.hpp` | Public C++ surface: `TlsOptions`, `TlsSocket`, `TlsServer`, `createTlsServer`, `connectTls`, `TlsError` (the cp3 `Error` object), `CipherInfo`, `PeerCertificate`. |
| `tls_backend.hpp` | PRIVATE backend boundary: `ITlsContext` / `ITlsEngine` interfaces + the OpenSSL function-by-function mapping contract. Only this header names a TLS library. |
| `runtime_tls.cpp` | Implements the header: libuv pump (read/write/timers/close), handshake timeout, per-server handshake rate limiter, the fail-closed stub backend, and the OpenSSL backend under `#ifdef CP3_TLS_OPENSSL`. |
| `README.md` | This file: export surface, wiring lines, limitations. |

## Export surface (C++)

Namespace `cp3::runtime::network::tls`:

- **Factories**: `TlsServer createTlsServer(TlsOptions = {}, handler = {})`,
  `TlsSocket connectTls(TlsOptions)`, `TlsSocket connectTls(host, port = 443, extra)` —
  throw `TlsError` (an `Error` object, `name == "TLSError"`) on invalid options
  (`ERR_TLS_INVALID_OPTIONS`: `minProtocol < TLSv1.2`, `minProtocol > maxProtocol`,
  `maxSendFragment` outside 256..65536, negative timeouts/limits, server without
  `cert`/`key`, bad ALPN id) or unavailable backend (`ERR_TLS_BACKEND_UNAVAILABLE`).
  Handshake/verify/network failures are **not** thrown: they are delivered as the
  socket `"error"` event with the same `Error` object.
- **`TlsSocket`** (move-only, wraps one `uv_tcp_t`):
  events `on("data", string|Buffer|())`, `on("secureConnect", ())`,
  `on("connect", ())`, `on("end", ())`, `on("error", (Thrown) => void)`,
  `on("close", ())`, `on("timeout", ())`; writes `write(string|Buffer) -> bool`,
  `end()`, `end(data)`, `destroy()`, `destroy(err)`, `close()`, `pause()`,
  `resume()`; `setTimeout(ms, cb?)`, `setNoDelay(enabled)`,
  `setKeepAlive(enabled, delayMs?)`, `connect(port, host?)`;
  properties `connecting()`, `destroyed()`, `readable()`, `readableEnded()`,
  `writable()`, `writableEnded()`, `remoteAddress()`, `remotePort()`,
  `localAddress()`, `localPort()`, `bytesRead()`, `bytesWritten()`;
  TLS additions `authorized()`, `encrypted()`, `alpnProtocol()`, `servername()`,
  `getProtocol()`, `getCipher()`, `getPeerCertificate(raw?)`, `setMaxSendFragment`,
  `getMaxSendFragment()`; `new TlsSocket(void* tcpSocket, options)` adopts a
  `TcpSocket`'s underlying `uv_tcp_t*` (the TcpSocket handle must not be used after).
- **`TlsServer`**: `on("secureConnection", (socket) => void)`, `on("error", (err) => void)`,
  `on("close", ())`; `listen(port, cb?)`, `listen(port, host, cb?)`, `close(cb?)`,
  `addressPort()`.
- **`TlsError`**: `cp3::Error` subclass, `name == "TLSError"`, `code` field with
  the `TlsErrorCode::*` strings (`CERT_UNTRUSTED`, `ERR_TLS_HANDSHAKE_TIMEOUT`,
  `ERR_TLS_HANDSHAKE_RATE_LIMITED`, `ERR_TLS_HANDSHAKE_FAILED`,
  `ERR_TLS_PROTOCOL_TOO_OLD`, `ERR_TLS_NO_PEER_CERTIFICATE`,
  `ERR_TLS_NOT_CONNECTED`, `ERR_TLS_SOCKET_DESTROYED`). Message is `[CODE] text`.
- **`TlsOptions`**: Node `tls` option names — `host`, `port`, `servername` (SNI),
  `ca`/`cert`/`key`/`keyPassphrase` (PEM text or file path), `rejectUnauthorized`
  (default **true**), `checkServerIdentity` (default true), `minProtocol`
  (`"TLSv1.2"` floor, lower is rejected), `maxProtocol` (`"TLSv1.3"`),
  `alpnProtocols`, `ciphers`, `maxSendFragment` (16384), `handshakeTimeoutMs`
  (10000), `maxHandshakesPerSecond` (50), `maxConcurrentHandshakes` (128),
  `allowHalfOpen`, `readable`, `writable`.

### TcpSocket parity

`TlsSocket` deliberately carries the **same member set as `TcpSocket`** (the
planned `net` socket surface): same event names (`data`/`end`/`error`/`close`/
`connect`/`timeout`), same write/close/pause/resume/timeout/keepalive methods,
same property spellings above — so code written against `TcpSocket` runs
unchanged against `TlsSocket`. `TlsSocket` only *adds* the TLS members
(`secureConnect`, `authorized`, `encrypted`, `getCipher()`, `getProtocol()`,
`getPeerCertificate()`, `alpnProtocol()`, `servername()`,
`setMaxSendFragment()`), exactly as Node's `tls.TLSSocket` extends `net.Socket`.

## Behaviour guarantees (when the OpenSSL backend is linked)

- **Verification on by default**: `SSL_VERIFY_PEER` + OS default verify paths
  (plus `ca` when given) + hostname check (`SSL_set1_host`) unless `checkServerIdentity`
  is explicitly false. A failed verify destroys the socket with `CERT_UNTRUSTED`
  even if the handshake itself completed — no partial trust.
- **TLS1.2+ only**: `SSL_CTX_set_min_proto_version(TLS1_2_VERSION)`; a floor
  below that in options is rejected at construction; a peer negotiating lower
  fails the handshake (`ERR_TLS_PROTOCOL_TOO_OLD`/failed).
- **SNI**: `SSL_set_tlsext_host_name` from `servername` (default `host`).
  **ALPN**: `SSL_CTX_set_alpn_protos` from `alpnProtocols`; negotiated id via
  `SSL_get0_alpn_selected`.
- **Memory-buffer engine, libuv owns the socket**: OpenSSL only ever sees
  `BIO_s_mem` pairs; every `uv_*` call lives in `runtime_tls.cpp`. The engine
  is never handed an fd (`SSL_set_fd` is never called).
- **Handshake timeout**: `uv_timer` started on connect/accept, stopped on
  `secureConnect`; expiry destroys the socket with `ERR_TLS_HANDSHAKE_TIMEOUT`.
- **Handshake rate limit**: per-`TlsServer` token bucket
  (`maxHandshakesPerSecond`) + concurrent cap (`maxConcurrentHandshakes`);
  excess sockets are destroyed with `ERR_TLS_HANDSHAKE_RATE_LIMITED`.
- **Data only after `secureConnect`**: `write()` before the handshake emits
  `ERR_TLS_NOT_CONNECTED` and returns false; `"data"` never fires for
  unverified or pre-handshake bytes.
- **Errors are `Error` objects everywhere**: thrown from factories, passed to
  `"error"` listeners, and rendered `[CODE] message` with `name "TLSError"`.

## WIRING (apply in order; every line is additive)

### 1. Vendor OpenSSL (third_party, follow MANIFEST.md recipe)

Pin OpenSSL (record repo/tag/SHA in `third_party/MANIFEST.md` + `VERSION.txt`),
build static `libssl.a`/`libcrypto.a` per triple (`windows-x64`, `linux-x64`,
`linux-arm64`), copy `include/` + `lib/<triple>/` + `LICENSE` into
`third_party/openssl/`. Nothing in this subtree changes when that lands.

### 2. `src/std/network/codegen/native.hpp` (the runtime TU selection row)

```cpp
// include roots: append "src/std/network/tls/include" next to
// "src/std/network/include" (the header lives at cp3/std/network/tls.hpp).
// sources: append "src/std/network/tls/runtime_tls.cpp" to runtimeSources.
// markers: append "createTlsServer(|connectTls(|TlsSocket|TlsServer|tls::" to
//          the marker list (same length as sources, positional).
// defines: append "-DCP3_TLS_OPENSSL" ONLY once OpenSSL is vendored
//          (before that, the stub compiles and fails closed at runtime).
// link (per triple, next to the libuv libs): add
//   windows-x64: {"third_party/openssl/lib/windows-x64"}, {"-lssl", "-lcrypto", "-lws2_32", ...}
//   linux-x64/arm64: {"third_party/openssl/lib/linux-x64"}, {"-lssl", "-lcrypto", "-ldl", "-pthread"}
```

### 3. `src/modules/src/support/std_registry.cpp` (import surface)

Add the named imports to the existing `std/network` row (no new module):
`createTlsServer`, `connectTls`, `TlsSocket`, `TlsServer`.

### 4. `src/std/network/codegen/sema.cpp` (typing)

```cpp
// Markers: const char* const kTlsSocket = "$network:TlsSocket";
//          const char* const kTlsServer = "$network:TlsServer";
// tlsSocketFields(): data/end/error/close/connect/secureConnect/timeout
//   listeners, write(string|Buffer)->boolean, end/destroy/close/pause/resume,
//   setTimeout(number, cb?)/setNoDelay(boolean)/setKeepAlive(boolean, number?),
//   connecting/destroyed/readable/readableEnded/writable/writableEnded: boolean,
//   remoteAddress/localAddress: string, remotePort/localPort/bytesRead/
//   bytesWritten: number, authorized/encrypted: boolean, alpnProtocol/
//   servername/getProtocol: string, getCipher()->object, getPeerCertificate()
//   ->object, setMaxSendFragment(number)/getMaxSendFragment()->number.
// tlsServerFields(): on(string, fn), listen(port, cb?)/listen(port, host, cb?),
//   close(cb?), addressPort()->number.
// networkMember(): "createTlsServer" -> fn(ambient, fn)->TlsServer,
//   "connectTls" -> fn(ambient)->TlsSocket, "TlsSocket" -> fn(ambient, ambient)
//   ->TlsSocket.
// tryInferNetworkCall(): "createTlsServer"/"connectTls" -> TlsServer/TlsSocket
//   types; "TlsSocket" construction -> TlsSocket type.
```

### 5. `src/std/network/codegen/codegen.cpp` (lowering)

```cpp
// emitNetworkIdentifier(): "createTlsServer" -> ::cp3::runtime::network::tls::
//   createTlsServer(<options>, <handler>); set needsTls_.
// "connectTls" -> tls::connectTls(<options>) / tls::connectTls(host, port, extra).
// "new TlsSocket(tcp, options)" -> tls::TlsSocket(<tcp>, <options>).
// needsTls_ -> emit #include "cp3/std/network/tls.hpp" (module_writer.cpp +
//   decl/program.cpp next to the needsNetwork_ include).
// socket.on/write/end/...: same lowering as the TcpSocket arms once the
//   TcpSocket agent lands; TLS-only members lower to the tls::TlsSocket
//   member spellings above.
```

### 6. Optional: `fetch` over TLS

`fetch("https://...")` should reuse `connectTls` + `TlsSocket` for the
transport (today it is http-only, D70). That is a `runtime_fetch.cpp` change
and is **not** part of this subtree.

## Limitations (deliberate)

1. **Stub until vendored**: with no `CP3_TLS_OPENSSL`, every TLS entry point
   throws `ERR_TLS_BACKEND_UNAVAILABLE`. There is deliberately no plaintext
   fallback and no "verification off because the backend is missing" path.
2. **`write()` before `secureConnect` is dropped** (Node buffers it). It emits
   `ERR_TLS_NOT_CONNECTED` and returns false so the caller can retry on
   `secureConnect`; no silent replay queue is kept.
3. **SNI dispatch (multi-tenant `servername` callback) is out of scope v1**:
   one context per `TlsServer` (single cert/key). `SSL_CTX_set_tlsext_servername_callback`
   is the documented extension point if per-SNI certs are later needed.
4. **Server `listen` host must be an IP literal** in v1 (same as client DNS is
   async via `uv_getaddrinfo`, but the listen path does not resolve names);
   `"error"` (`ERR_TLS_INVALID_OPTIONS`) is emitted otherwise.
5. **Session resumption / tickets, client certs (`requestCert`), OCSP
   stapling, TLS 1.3 sentinels and `getTLSToken` are not exposed**; the
   `PeerCertificate` shape is the Node subset (DN/serial/fingerprint/dates)
   and full extraction is the OpenSSL vendoring follow-up (the engine method
   is the single place to fill it in).
6. **`bytesRead()` counts ciphertext bytes** (what the wire carried) and
   `bytesWritten()` counts plaintext bytes accepted by `write()` — the two
   are on different sides of the engine by design; document before comparing.
7. **Rate limiter is per-`TlsServer` and in-process** (token bucket refilled
   from the libuv clock); there is no cross-process or per-IP policy.
8. **`setMaxSendFragment` after the handshake is stored but not re-applied**
   until the next connection (OpenSSL only honours it at `SSL_new` time in
   this wiring).
