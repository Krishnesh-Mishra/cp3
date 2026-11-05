# webserver server config — `createServer({ tls, cors, ... })`

SOLO subtree: `src/std/network/webserver/config/**` only. No shared file was
touched, nothing was built/run/committed (code only). The integrator (lead)
applies the WIRING lines below.

## Files

| File | Role |
|---|---|
| `include/cp3/std/network/webserver/config.hpp` | Public API: `ServerConfig`, `TlsOptions`, `CorsOptions`, `SslFiles`, `ConnectionGuard`, validation/backend/factory decls, `sema_codegen` text-builder decls |
| `runtime/runtime_server_config.cpp` | Runtime: fail-closed validation (files/PEM/protocol/numbers/CORS), CORS middleware, `applyServerConfig`, `createServerFromConfig`, `requireTlsBackend` |
| `sema/server_config_sema.cpp` | Sema helpers: known-option tables + `checkMinProtocolLiteral` (pure, standalone TU) |
| `codegen/server_config_codegen.cpp` | Codegen helpers: `serverConfigCallText` / `tlsOptionsText` / `corsOptionsText` / `createServerCallText` (pure, standalone TU) |
| `native/server_config_native.hpp` | Native row snippet (paste-only, not applied) |
| `README.md` | This file: schema, export surface, wiring lines, limitations |

## Options schema

```cp3
import { createServer } from "std/network";

const app = createServer({
  // --- TLS (all file paths, validated at createServer() time) ---
  tls: {
    cert: "certs/server.pem",        // required when tls is present
    key:  "certs/server-key.pem",    // required; must differ from cert
    chain: "certs/intermediate.pem", // optional intermediate bundle
    ca:   "certs/ca.pem",            // optional client-cert CA bundle
    minProtocol: "TLSv1.2",          // optional floor; "TLSv1.3" also ok
  },
  // --- CORS (installed as the first middleware; no app.use needed) ---
  cors: {
    origin: "https://app.example.com",   // "*" (default) or origin list
    methods: "GET,POST,PUT,DELETE",     // default GET,POST,PUT,PATCH,DELETE,OPTIONS
    headers: "Content-Type, X-Token",   // default: reflect the request headers
    credentials: true,                  // default false
    maxAge: 86400,                      // seconds; 0 = header omitted (default)
  },
  // --- server limits (numbers) ---
  threads: 4,          // integer 1..4096, default 1 (existing D79 scaling)
  maxConnections: 1000, // integer >= 0, 0 = unlimited (default)
  idleTimeout: 120,     // SECONDS, integer >= 0, 0 = disabled (default)
  maxWriteBuffer: 65536,// BYTES, integer >= 0, 0 = unbounded (default)
  timeout: 30,          // SECONDS, integer >= 0, 0 = disabled (default)
});

app.get("/health", (req, res) => { res.text("ok"); });
app.listen(3000);
```

Fail closed (all synchronous throws at `createServer()`, `[CODE] message`):

| Input | Result |
|---|---|
| `tls` without `cert`/`key`, missing/empty file, file without the expected PEM block, `cert == key`, PEM text instead of a path | `[ERR_TLS_INVALID_OPTIONS]` |
| `minProtocol` other than `TLSv1.2`/`TLSv1.3` (incl. `TLSv1.0`, `TLSv1.1`, `SSLv3`) | `[ERR_TLS_INVALID_OPTIONS]` — a floor, never negotiated below |
| `tls` set but uSockets built `LIBUS_NO_SSL` (no `CP3_UWS_SSL`) | `[ERR_TLS_BACKEND_UNAVAILABLE]` — **before any HttpServer exists; never plaintext** |
| `threads` non-integer / `< 1` / `> 4096`; negative/fractional limits | `[ERR_SERVER_CONFIG_INVALID_OPTIONS]` |
| CORS: empty/spacey `origin`, non-http(s) origin, bad method/header token, `maxAge` outside `0..31536000` | `[ERR_CORS_INVALID_OPTIONS]` |

## Export surface (C++)

Namespace `cp3::runtime::network::webserver::config`:

- Types: `TlsOptions{enabled,cert,key,chain,ca,minProtocol}`,
  `CorsOptions{present,origin,methods,headers,credentials,maxAge}`,
  `ServerConfig{threads,maxConnections,idleTimeout,maxWriteBuffer,timeout,tls,cors}`,
  `SslFiles{certFile,keyFile,caFile,chainFile}`, `ConnectionGuard`.
- Validation: `validateServerConfig(cfg) -> ServerConfig` (normalised copy),
  `makeServerConfig(threads,maxConnections,idleTimeout,maxWriteBuffer,timeout,tls,cors)`,
  `normalizeMinProtocol`, `tlsMinProtocolVersion` (`0x0303`/`0x0304`),
  `splitList`, `joinStrings`.
- TLS: `requireTlsBackend()`, `sslFiles(cfg)`.
- Wiring: `makeCorsMiddleware(CorsOptions) -> network::MiddlewareFn`,
  `applyServerConfig(HttpServer&, ServerConfig)`,
  `createServerFromConfig(ServerConfig) -> HttpServer`.
- `sema_codegen`: `isKnownCreateServerOption` / `isKnownTlsOption` /
  `isKnownCorsOption`, `checkMinProtocolLiteral`, `serverConfigCallText`,
  `tlsOptionsText`, `corsOptionsText`, `createServerCallText`.

CORS semantics (deliberate divergence from the existing
`makeCorsMiddleware` in `runtime_web.cpp`, which emits `*` unconditionally):
with `credentials: true`, `*` is never emitted — the request `Origin` is
reflected; an explicit origin list only matches listed origins; every CORS
response carries `Vary: Origin, Access-Control-Request-Method,
Access-Control-Request-Headers`; a preflight (`OPTIONS` + `Origin` +
`Access-Control-Request-Method`) is answered `204` at this middleware and
user routes never see it; a non-preflight `OPTIONS` passes through.

## Wiring lines (integrator — NOT applied, shared files untouched)

1. **Vendor/define (TLS only).** Rebuild uSockets with an SSL backend per
   `third_party/MANIFEST.md` caveat 3, then define `CP3_UWS_SSL` in the
   native row. Until then every `tls:` program fails closed with
   `ERR_TLS_BACKEND_UNAVAILABLE` — by design, same as `std/network/tls`
   (`CP3_TLS_OPENSSL` stub).
2. **Native row** (`src/std/network/codegen/native.hpp`, D68): add include
   root `src/std/network/webserver/config/include`, the three TUs and marker
   `webserver::config::` — see `native/server_config_native.hpp`. Add
   `-DCP3_UWS_SSL` to defines only with step 1. The `createServer` lowering
   below contains the marker text, so a config-using program selects the TU.
3. **Sema** (`src/std/network/codegen/sema.cpp`, `checkCreateServerOptions`):
   extend the accepted keys with `sema_codegen::isKnownCreateServerOption`;
   for `tls`/`cors` sub-objects check each key with `isKnownTlsOption` /
   `isKnownCorsOption` (unknown key → type error naming the key); for a
   string-literal `minProtocol` report `checkMinProtocolLiteral`'s message.
   Keep the literal-`threads` checks that are already there. Nothing else in
   the table changes: `createServer` already returns `$network:HttpServer`.
4. **Codegen** (`src/std/network/codegen/codegen.cpp`, the `createServer`
   arm of `emitNetworkIdentifier`): walk the options object literal; per
   known key collect `expr(value)` (`tls`/`cors`: walk one level deeper the
   same way). For `origin`/`methods`/`headers`, a string literal lowers to
   `expr(value)` and a string-array literal to
   `webserver::config::joinStrings({expr(e1), expr(e2), ...})`; `credentials`
   stays a bool expr. Then emit
   `sema_codegen::createServerCallText(sema_codegen::serverConfigCallText(...))`
   (prefix the returned text with `kRuntimeNs`, i.e. `cp3::runtime::network::`)
   with `tlsOptionsText(...)` / `corsOptionsText(...)` for the nested exprs
   (empty expr for an absent key → the builder's documented default). Set
   `needsNetwork_` as today. Keep the config alive for the process lifetime
   by emitting it as `static`:
   `static ServerConfig cfg = <serverConfigCallText>;` then
   `createServerFromConfig(cfg)` — `SslFiles` strings point into it.
5. **Include** (`module_writer.cpp` + `decl/program.cpp`, next to the
   `needsNetwork_` line): `if (needsNetwork_) out += "#include \"cp3/std/network/webserver/config.hpp\"\n";`
   (only needed if the config marker selects the TU without other network
   symbols; reusing the existing `network.hpp` include also works).
6. **TLS transport (integrator, `runtime_server*.cpp`)**: with
   `CP3_UWS_SSL`, construct `uWS::SSLApp` from
   `sslFiles(cfg)` (`cert_file_name`/`key_file_name`/`ca_file_name`; append
   `chainFile` contents to the cert file — uWS has no chain slot) instead
   of `uWS::App`, in both `HttpServer()` and the scaled child loops.
   `tlsMinProtocolVersion(cfg.tls.minProtocol)` is the floor the SSL
   context must enforce (0x0303/0x0304). Until this step, `tls:` throws in
   `requireTlsBackend()` and step 6 is unreachable — that is the point.
7. **Limits enforcement (integrator, accept/dispatch)**: `idleTimeout` /
   `timeout` → `us_socket_timeout(ssl, socket, seconds)` per connection;
   `maxWriteBuffer` → check `res` buffered/write amount before flush
   (same high-water rule as `TcpSocket::write`); `maxConnections` →
   `ConnectionGuard guard(cfg.maxConnections);` with `guard.enter()` at the
   top of the request glue (`onRequest`) and `guard.leave()` on
   finish/abort (over-cap → `503`, no handler). These are recorded and
   validated here; the hooks above are the only shared-file lines.

## Limitations

- **TLS is fail-closed until steps 1+6 land.** There is no working TLS
  transport in this subtree: with the current `LIBUS_NO_SSL` build any
  `tls:` config throws and produces no server — never a silent plaintext
  downgrade. `createTlsServer` (`std/network/tls`) is a separate surface and
  is not reused for HTTP.
- **Cert/key by file path only.** `tls.cert`/`key`/`chain`/`ca` are paths,
  not PEM text (uWS SSL takes file names); PEM-text values are rejected
  with a diagnostic rather than written to a temp file. No cert/key pair
  matching is done (no OpenSSL here): a mismatched pair surfaces as a TLS
  handshake failure, not a config error. One cert per server (no SNI
  multi-tenancy); fetch/`https://` client use is out of scope.
- **CORS is middleware-shaped.** It runs as the first `app.use` layer (so
  user middleware ordering after it is unchanged), but it is still subject
  to the dispatcher: it runs after the request body is buffered, and a
  thrown user error later still yields the existing 500 path. `origin` is
  an exact string/list match (no wildcard subdomains, no regex, no
  `origin: true` dynamic function).
- **Limits are validated + recorded, enforcement is dispatch-level.**
  `maxConnections` counts in-flight handler dispatches (via
  `ConnectionGuard` at step 7), not raw TCP accepts; `idleTimeout`/`timeout`
  use uWS's 4-second-granularity timers; `maxWriteBuffer` is a high-water
  check, not a hard write abort. Units are seconds/bytes as documented
  above (unlike `TcpServerOptions::idleTimeout`, which is milliseconds).
- **`threads` stays the existing D79 scaling** (`HttpServer(double)`); this
  config adds no new threading. Options must be object literals at the
  `createServer` call (a computed options value would need the same
  `ambient()` escape hatch the current sema table uses for `threads`).
- Not built/tested (per task: code only). The runtime TU depends only on
  `cp3/std/network.hpp` + `std::filesystem`, following the sibling subtrees.
