---
title: Network
---
# Network

`std/network` provides HTTP client and server APIs, WebSockets, raw TCP/UDP/TLS sockets, DNS and middleware. Import named bindings:

```ts
import { fetch, lookup, WebSocket, WebSocketServer, createServer, tls, Router, static, cors, bodyLimit, cookieParser, Headers, Request, Response, FormData, AbortSignal } from "std/network";
```

Raw-socket names (`createTcpServer`, `connectTcp`, `TcpSocket`, `TcpServer`, `createUdpSocket`, `UdpSocket`, `createTlsServer`, `connectTls`, `TlsSocket`, `TlsServer`) are exported since v0.2.1. `std/network/types` publishes these class names for annotation positions only (annotations; no runtime surface).

Numeric widths: a `typescript` project keeps `number`; a `rust`/`cpp` project uses exact widths (`status` and ports are `u16`). Network operations fail for DNS, connection, timeout, protocol and certificate reasons; check status codes and treat bodies as external input.

## `fetch`

| Member | Signature | Notes |
| --- | --- | --- |
| `fetch` | `fetch(url: string, init?): Promise<FetchResponse>` | `http://` and `https://`; https verifies the server certificate (see TLS trust) |
| `init` | `{ method?, headers?: Map<string, string>, body?, timeout?, signal? }` | `timeout` in milliseconds (`0` = no timeout); `init.signal` applies the `AbortSignal` as the request timeout |
| `FetchResponse.ok` | `ok: boolean` | `false` for failed HTTP statuses — the promise still resolves |
| `FetchResponse.status` | `status: u16` | |
| `FetchResponse.statusText` | `statusText: string` | |
| `FetchResponse.headers` | `headers: Map<string, string>` | |
| `FetchResponse.text()` | `text(): Promise<string>` | Body buffered before resolve |
| `FetchResponse.json<T>()` | `json<T>(): Promise<T>` | Scalar or reified interface shapes |
| `FetchResponse.arrayBuffer()` | `arrayBuffer(): Promise<Buffer>` | |

`AbortSignal.timeout(ms: number): AbortSignal` creates a timeout signal; instance member `aborted: boolean`. `Headers` (`get`/`has`/`set`/`append`/`delete`), `FormData` (`append`/`get`/`has`/`set`/`delete`), `Request` (`url`, `method`, `headers`, `signal`, `text()`, `json()`, `arrayBuffer()`) and `Response` (`ok`, `status`, `statusText`, `headers`, `url`, `text()`, `json()`, `arrayBuffer()`) are constructed with `new`; each map get returns the first value.

## TLS trust and certificates

`fetch("https://…")`, `wss://` and TLS servers verify peers by default (embedded Mozilla CA bundle, SNI hostname match, TLS1.2 floor); no filesystem dependency for public CAs.

- `SSL_CERT_FILE=/path/to/bundle.pem` replaces the embedded bundle (corporate/private PKI); if it fails to load, verification falls back to the embedded bundle.
- `createServer({ tls })` / `new WebSocketServer({ port, tls })` take PEM **file paths** only (`cert`, `key`) — inline PEM strings throw `ERR_TLS_INVALID_OPTIONS`. `ca` (mTLS client-cert CA list) and `passphrase` (on an encrypted key) are accepted.
- `minProtocol`: `"TLSv1.2"` (default) or `"TLSv1.3"`; lower floors throw `ERR_TLS_INVALID_OPTIONS`.
- Fail closed: misconfiguration throws `ERR_TLS_INVALID_OPTIONS` synchronously at construction; a handshake verification failure surfaces as `TLSError` with `[CODE] message` (e.g. `[CERT_UNTRUSTED] …`). On a build without a TLS backend, TLS entry points throw `ERR_TLS_BACKEND_UNAVAILABLE`. An ARM64 build with OpenSSL exists but is untested on hardware.

`tls(options?)` builds a reusable options object for `createServer({ tls })` / `new WebSocketServer({ port, tls })`. The keys it produces (typed): `cert`, `key`, `ca`, `passphrase`, `minProtocol`, `servername`, `rejectUnauthorized`, `checkServerIdentity`. The same object can be inlined; `tls: true` means "TLS with default options" and requires `cert`/`key` at runtime.

## WebSocket client

`new WebSocket(url: string): WebSocket` — `ws://` or `wss://`; the server certificate is verified exactly like `https://`. Members: `on(event: string, listener): void`; `send(data: string | Buffer): void`; `close(code?: number, reason?: string): void`. Events: `"open"` (wait for it before sending), `"message"` (text frames as strings, binary as `Buffer`), `"close"` (code and reason), `"error"`. One handler slot per event — re-registering replaces the previous listener. An open socket keeps the event loop alive; close it when done.

## WebSocket server

`new WebSocketServer(options: { port: number, path?, tls? }): WebSocketServer` — `path` defaults to `"*"`; `tls` enables TLS (inline options object or `tls({...})`; PEM **file paths**). Members: `on(event: string, listener): void` with `"connection"` (a WebSocket handle, optionally with the HTTP request); `close(): void`. Server connections use the same `WebSocket` handle surface.

## HTTP server — `createServer`

`createServer(options?): HttpServer` — options object, all optional: `threads` (integer ≥ 1), `tls` (`true`, an inline options object, or a `tls({...})` value — file paths only), `cors` (`{ origin?, methods?, headers?, credentials?, maxAge? }`), `static` (directory path), `bodyLimit` (maximum request-body bytes), `trustProxy` (honors `X-Forwarded-Proto`), `views` (template directory for `res.render`), `onError` (`(err, req, res) => void`). An empty object is valid. Route handlers are synchronous or `Promise<undefined>`; routes and middleware run in registration order; an unhandled chain returns 404; bodies over `bodyLimit` are rejected with 413; CORS preflights are answered by the cors middleware alone.

| Member | Signature | Notes |
| --- | --- | --- |
| `get` / `post` / `put` / `delete` / `patch` / `head` / `options` / `any` | `(path: string, handler: (req: HttpRequest, res: HttpResponse) => void) => void` | Route registration |
| `all` | `(path: string, handler) => void` | All methods |
| `use` | `use(middleware)` / `use(prefix: string, middleware)` / `use(router: Router)` / `use(prefix: string, router: Router)` | Mount middleware or a router |
| `enable` / `disable` | `(name: string) => HttpServer` | App settings |
| `enabled` / `disabled` | `(name: string) => boolean` | |
| `set` | `set(name: string, value) => HttpServer` | Chainable app settings (e.g. `"secret"` for signed cookies) |
| `route` | `route(path: string): RouteChain` | `.get/.post/...` chains, handler or handler array, returning the chain |
| `param` | `param(name: string, (req, res, next, value) => void): void` | Route-param hook |
| `onError` | `onError((err, req, res) => void): void` | Error handler |
| `listen` | `listen(port: number, callback?: () => void): void` | Ports are `u16` in Rust/C++, `number` in TypeScript |
| `close` | `close(): void` | Stops the listener and connections |

### Request (`HttpRequest`)

| Member | Type | Notes |
| --- | --- | --- |
| `method` / `path` | `string` | |
| `params` / `query` / `headers` / `cookies` / `signedCookies` | `Map<string, string>` | Route params; query string; headers; parsed `Cookie` values; verified signed cookies (`s:` values against the `secret`) — tampered ones are omitted |
| `baseUrl` / `originalUrl` | `string` | Mount-path reads (router) |
| `ip` / `protocol` | `string` | `protocol` is `http` unless `trustProxy` sees `X-Forwarded-Proto` |
| `body` | buffered body | Bodies are buffered before dispatch |
| `text()` | `Promise<string>` | |
| `json<T>()` | `Promise<T>` | |

### Response (`HttpResponse`)

| Member | Signature | Notes |
| --- | --- | --- |
| `status` | `status(code: number): HttpResponse` | Chainable; chainable with `header` |
| `header` | `header(name: string, value: string): HttpResponse` | Chainable |
| `cookie` | `cookie(name: string, value: string, options?): HttpResponse` | Options: `maxAge?` (seconds), `path?`, `domain?`, `secure?`, `httpOnly?`, `signed?`, `sameSite?` |
| `clearCookie` | `clearCookie(name: string, options?): HttpResponse` | |
| `redirect` | `redirect(url: string, status?: number): void` | Terminal |
| `text` | `text(text: string, status?: number): void` | Terminal |
| `json` | `json(payload?, status?: number): void` | Terminal; object/array literals are lowered natively |
| `send` | `send(status?: number): void` | Terminal |
| `write` | `write(chunk: string \| Buffer): void` | Chunked write |
| `end` | `end(): void` | Terminates chunked writes |
| `sendFile` | `sendFile(path: string): void` | |
| `set` / `append` / `get` / `type` | `set(field, value?)`, `append(name: string, value: string)`, `get(name: string): string`, `type(mime: string)` | Chainable/typed header helpers |
| `render` | `render(view: string, options?): void` | Serves from the configured `views` directory |
| `format` / `download` / `attach` / `links` / `location` / `vary` | `format(obj): void`; `download(path, filename?): void`; `attach(path): HttpResponse`; `links(obj): HttpResponse`; `location(url): HttpResponse`; `vary(field): HttpResponse` | Rendering and download helpers |
| `headersSent` | `headersSent: boolean` | |

The first terminal response operation wins; later attempts do not start a new response.

## `Router` and middleware

`new Router(options?): Router` — a mountable route set with the same verbs (`get`/`post`/`put`/`delete`/`patch`/`head`/`options`/`all`), `use`/`use(prefix, …)`/`param`/`route(path)` chains, plus `handle`/`handler`/`mountMiddleware`. Mount with `app.use("/prefix", router)`; `:name` params, `*` patterns and `strict`/`caseSensitive` follow Express-style semantics. Middleware factories: `static(dir)` (serve files), `cors(options)` (answers `OPTIONS` preflights and stamps CORS headers), `bodyLimit(n)` (413 over the limit), `cookieParser()` (fills `req.cookies`; `cookieParser(secret)` verifies signed cookies).

## DNS — `lookup`

`lookup(hostname: string): Promise<string>` — resolves to the **first** address via `getaddrinfo`; rejects with an `Error` naming the hostname. No options object, no callback form, no `lookupAll`.

## Raw sockets (since v0.2.1)

`createTcpServer(options?): TcpServer` — options: `threads` (`SO_REUSEPORT` handles on one shared loop, **not** threads; `1..64`, other values throw), `maxConnections` (`0` = unlimited; over-cap accepts are closed without events), `idleTimeout` (ms; `0` = disabled), `maxWriteBuffer` (bytes; `0` = unbounded). `TcpServer`: `listen(port, callback?)` / `listen(port, host, callback?)` (`0.0.0.0` / IPv4 literal; bind failures throw synchronously); `close()` (listeners **and** every live connection); `listening(): boolean`; `addressPort(): u16` (resolves `listen(0)`); `connectionCount(): number`; events `"connection"` (a `TcpSocket`), `"listening"`, `"close"`, `"error"`.

`TcpSocket` (via `new TcpSocket(host, port, timeout?)` or `connectTcp`): events `"data"` (`string` or `Buffer`, raw segments, no framing), `"end"`, `"close"`, `"drain"`, `"connect"`, `"error"` (an `Error` whose Node-style code is a message prefix). Methods: `write(chunk: string | Buffer): boolean` (`false` = queued bytes reached `maxWriteBuffer`; bytes are always queued unless closed/ended — then `false` without queueing); `end([data])`; `close()`; `pause()`/`resume()`; `isPaused()`/`isClosed()`/`isConnected()`; `bufferedBytes(): number`; `remoteAddress()`/`localAddress()` (`"ip:port"`, `""` when not connected); `remotePort()`/`localPort()`.

`connectTcp(options)` / `connectTcp(host, port, timeout?)` — **not a Promise**; the socket returns immediately and outcomes arrive via `"connect"`/`"error"`/`"close"`. `timeout` defaults to `10000` (`0` = OS TCP timeout); DNS is IPv4-only and rejects IPv6 literals; port `0` is rejected.

`createUdpSocket(type: "udp4" | "udp6"): UdpSocket` — anything else throws. Members: `on("message", (msg: Buffer, from: { address, port, family }) => void)`; `bind(port)` / `bind(port, address)` (numeric IP literal) / `bind(port, callback?)`; `send(data: string | Buffer, port, address)` (fire-and-forget; send-time failures go to `"error"`); `address(): { address, port, family }`; `close()`; `bound(): boolean`. No multicast, no dual-stack, no `connect()`.

TLS over raw sockets (same vendored OpenSSL as `fetch("https://…")`): `connectTls(options | host, port?, options?): TlsSocket` (not a Promise; throws `TLSError` synchronously on invalid options/unavailable backend; handshake failures via `"error"`); `createTlsServer(options?, handler?): TlsServer` (`handler(socket)` is the `"secureConnection"` listener; configuration errors throw synchronously). `TlsOptions` keys: `host`, `port` (default `443`), `servername`, `ca`/`cert`/`key`/`keyPassphrase` (PEM paths **or** inline PEM), `rejectUnauthorized` (default `true`), `checkServerIdentity`, `minProtocol` (default `"TLSv1.2"`), `maxProtocol` (default `"TLSv1.3"`), `alpnProtocols`, `ciphers`, `maxSendFragment`, `handshakeTimeoutMs` (default `10000`), `maxHandshakesPerSecond` (default `50`), `maxConcurrentHandshakes` (default `128`), `allowHalfOpen`, `readable`, `writable`. Note: `createServer({ tls })` takes file paths only — inline PEM belongs to these sockets.

`TlsSocket` mirrors `TcpSocket` — `write`/`end`/`close`/`destroy`/`pause`/`resume`/`setTimeout`/`setNoDelay`/`setKeepAlive`, `connect`, events `"data"`, `"end"`, `"close"`, `"connect"`, `"error"`, `"timeout"`, properties `connecting`/`destroyed`/`readable`/`readableEnded`/`writable`/`writableEnded`/`remoteAddress`/`remotePort`/`localAddress`/`localPort`/`bytesRead`/`bytesWritten` — plus `"secureConnect"`, `authorized()`, `encrypted()`, `alpnProtocol()`, `servername()`, `getProtocol()`, `getCipher(): { name, version, standardName }`, `getPeerCertificate(raw?): { subject, issuer, serialNumber, fingerprint, notBefore, notAfter, pem? }`, `setMaxSendFragment(n)` / `getMaxSendFragment()`. Differences from `TcpSocket`: `write()` before `"secureConnect"` is dropped (emits `"error"` `ERR_TLS_NOT_CONNECTED`); `bytesRead()` counts ciphertext, `bytesWritten()` plaintext.

`TlsServer`: events `"secureConnection"` (a `TlsSocket`), `"error"`, `"close"`; `listen(port, callback?)` / `listen(port, host, callback?)`; `close(callback?)`; `addressPort()`. `TLSError` — every TLS failure: `name` is `"TLSError"`, the code travels in the message (`[CERT_UNTRUSTED] …`, `[ERR_TLS_HANDSHAKE_TIMEOUT] …`, `[ERR_TLS_INVALID_OPTIONS] …`, `[ERR_TLS_NOT_CONNECTED] …`, …). One certificate/key per server; no SNI callback, no session resumption, no client-cert request, no OCSP.

Behavior details: [guide](../../standard-library/13-network.md)
