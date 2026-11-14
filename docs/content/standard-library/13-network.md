---
title: Networking
---
# Networking

`std/network` exports `fetch`, `WebSocket`, `WebSocketServer`, `createServer`, `lookup`, `tls`, `Headers`, `Request`, `Response`, `FormData`, `AbortSignal`, `Router`, `static`, `cors`, `bodyLimit`, and `cookieParser` as named imports:

```ts
import { fetch, WebSocket, WebSocketServer, createServer, lookup, tls, Headers, Request, Response, FormData, AbortSignal, Router, static, cors, bodyLimit, cookieParser } from "std/network";
```

Network APIs are asynchronous and depend on DNS, remote hosts, ports, and operating-system resources. Handle rejection and close resources when finished.

Raw socket bindings (`createTcpServer`, `connectTcp`, `TcpSocket`, `TcpServer`, `createUdpSocket`, `UdpSocket`, `connectTls`, `createTlsServer`, `TlsSocket`, `TlsServer`) are exported since v0.2.1 - see [Raw sockets (TCP, UDP, TLS)](#raw-sockets-tcp-udp-tls) below.

## Fetch

`fetch(url, init?)` sends an HTTP request and resolves to a response object. Both `http://` and `https://` URLs are supported; HTTPS uses the same TLS engine as `connectTls` / `createTlsServer`, with certificate verification on by default (see **TLS trust and certificates** below). The optional initialization object supports `method`, `headers` (`Map<string, string>`), `body`, and `timeout` in milliseconds.

```ts
const response = await fetch("https://127.0.0.1:8080/health");
if (!response.ok) {
  throw new Error("health endpoint returned an error");
}
const text = await response.text();
```

`FetchResponse` exposes `ok`, `status`, `statusText`, and `headers`. Its body is buffered before `fetch` resolves. `text()` returns a promise for the text body, `arrayBuffer()` returns a promise for a Buffer, and `json<T>()` parses supported scalar or reified interface shapes. A failed HTTP status still resolves to a response; check `ok` or `status`. DNS, connection, timeout, unsupported-scheme, certificate-verification, and protocol failures reject the promise.

## TLS trust and certificates

`fetch` over `https://`, `new WebSocket("wss://...")`, and TLS servers all verify peers by default: the server certificate chain is checked against the Mozilla CA bundle embedded in the binary, the hostname is matched against SNI (the URL host), and the protocol floor is TLS1.2. There is no runtime filesystem dependency for public CAs.

- **Operator override**: set `SSL_CERT_FILE=/path/to/bundle.pem` before launching the process to replace the embedded bundle with your own (for a corporate or private PKI). The override file must exist and parse; if it does not load, verification falls back to the embedded bundle.
- **Server options**: `createServer({ tls })` and `new WebSocketServer({ port, path?, tls })` take PEM **file paths** only (`cert` and `key`) — inline PEM strings are rejected with `ERR_TLS_INVALID_OPTIONS`. `ca` (a client-certificate CA list for mTLS) and file passphrases are accepted as `passphrase` on an encrypted `key`.
- **`minProtocol`**: `"TLSv1.2"` (default) or `"TLSv1.3"`. Lower floors are refused with `ERR_TLS_INVALID_OPTIONS`.
- **Fail closed**: a TLS misconfiguration (missing/unreadable cert/key, unloadable CA) throws `ERR_TLS_INVALID_OPTIONS` synchronously at server construction; a verification failure during the handshake surfaces as a `TLSError` with `[CODE] message` (e.g. `[CERT_UNTRUSTED] ...`, `[ERR_TLS_HANDSHAKE_TIMEOUT] ...`) as the promise rejection. On a build without a TLS backend, TLS entry points fail with `ERR_TLS_BACKEND_UNAVAILABLE` instead of silently degrading to plaintext. The ARM64 packages are built with OpenSSL but have not been run on ARM64 hardware.

## `tls()` — reusable TLS options

`tls` is a named import usable as a factory: `tls({ cert, key, ... })` evaluates to a reusable options object, which can be passed to `createServer({ tls })` or `new WebSocketServer({ port, tls })`:

```ts
import { tls, createServer } from "std/network";

const creds = tls({ cert: "cert.pem", key: "key.pem", minProtocol: "TLSv1.2" });
const app = createServer({ tls: creds, bodyLimit: 1 << 20 });

app.get("/health", (req, res): undefined => res.text("ok"));
app.listen(443, () => console.log("listening on https://0.0.0.0:443"));
```

The same object can also be inlined directly in the options literal: `createServer({ tls: { cert: "cert.pem", key: "key.pem" } })`. `tls: true` still means "TLS enabled with default options" and requires `cert`/`key` at runtime.

Numeric network values follow the project vocabulary. A `typescript` project keeps `number`; a `rust` or `cpp` project uses the exact standard-library width: `status` and ports are `u16` (`uint16_t` in C++), so `res.status` and `listen(port)` never pass through a floating-point value.

A timeout of zero means no timeout. A request that never settles remains pending and keeps event-loop work alive. Use a positive timeout for operations that need a deadline. Headers are represented by a CP3 Map, and the implementation owns framing headers such as host and content length.

## WebSocket client

`new WebSocket(url)` creates a client connection over `ws://` or `wss://`. The socket supports event registration, `send` for string or Buffer payloads, and `close(code?, reason?)`. Event callbacks cover connection open, message, close, and error according to the runtime surface. Over `wss://`, the server certificate is verified exactly like `https://` (embedded Mozilla CA, SNI, TLS1.2 floor, `SSL_CERT_FILE` override).

```ts
const socket = new WebSocket("wss://127.0.0.1:9000/events");
socket.on("open", (): undefined => socket.send("hello"));
```

Wait for the `"open"` event before sending application messages. `"message"` receives text frames as strings and binary frames as Buffer values. `"close"` receives a code and reason; `"error"` receives an error value. There is one handler slot per event, so registering a later listener for the same event replaces the previous listener. Close the socket when the exchange is complete; an open socket keeps event-loop work alive.

## WebSocket server

`new WebSocketServer({ port, path?, tls? })` accepts a port, an optional route path (default `"*"`), and an optional `tls` key. Passing `tls` enables TLS for accepted connections — the value is either an inline options object or a value from `tls({...})`; `cert` and `key` must be PEM **file paths** (see **TLS trust and certificates**). Without `tls`, the server is plain `ws://`.

```ts
const wss = new WebSocketServer({ port: 9000, tls: { cert: "cert.pem", key: "key.pem" } });
wss.on("connection", (ws): undefined => { ws.on("message", (msg) => ws.send("echo")); });
```

Register a `"connection"` handler that receives a WebSocket, optionally with the HTTP request. A server connection is represented by the same WebSocket handle surface. Call `close()` during shutdown.

## HTTP server with `createServer`

`createServer(options?)` constructs an HTTP server with route registration and middleware support. The options object supports `threads`, an integer of at least one, and the full config surface: `tls` — either a boolean (`true` enables TLS with default options and requires `cert`/`key` at runtime), an inline options object, or a value from the `tls({...})` factory — `cors` (`{ origin?, methods?, headers?, credentials?, maxAge? }`), `static` (a directory path), `bodyLimit` (maximum request-body bytes), `trustProxy` (honors `X-Forwarded-Proto`), `views` (a template directory for `res.render`), and `onError` (`(err, req, res) => ...`). An empty options object is valid. Each route handler can be synchronous or return `Promise<undefined>`. Routes and middleware execute in registration order. Calling `next()` from middleware continues the chain; it returns `Promise<undefined>`, so async middleware may use `await next()`. A chain that completes without sending a response returns 404. Requests over `bodyLimit` are rejected with 413 before the handler runs; CORS preflights are answered by the cors middleware alone.

With `tls`, `listen` serves HTTPS; the cert/key are PEM **file paths** read when the server is constructed (an unreadable or non-PEM file fails construction). `tls: true` with no cert/key throws `ERR_TLS_INVALID_OPTIONS` rather than serving plaintext.

```ts
const server = createServer();
server.get("/health", (request, response): undefined => {
  response.status(200).header("content-type", "text/plain");
  response.text("ok");
});
server.listen(8080, (): undefined => console.log("ready"));
```

The server supports `get`, `post`, `put`, `delete`, `patch`, `head`, `options`, `any`, and `use`. `listen(port, callback?)` begins accepting requests; `close()` shuts down the listener and connections. Request values include method, path, route `params`, query Map, headers Map, `cookies` Map, `ip`, and `protocol` (`http` unless `trustProxy` sees `X-Forwarded-Proto`). Request bodies are buffered before dispatch; `text()` and `json<T>()` read that body. The `port` and status-code parameters are `number` in a `typescript` project and `u16` in a `rust`/`cpp` project, matching the values returned by `res.status`.

Responses support chainable `status(code)` and `header(name, value)`, `cookie(name, value, { maxAge?, path?, domain?, secure?, httpOnly?, signed?, sameSite? }?)` (`maxAge` is in seconds), `redirect(url, status?)`, `sendFile(path)`, and `render(view)` (serves from the configured `views` directory), plus terminal `text`, `json`, and `send` operations. `write(string | Buffer)` and `end()` provide chunked response writes. The first terminal response operation wins; code that tries to send a second response does not start a new response.

## Router and middleware helpers

`new Router(options?)` builds a mountable route set with the same `get`/`post`/`put`/`delete`/`patch`/`head`/`options`/`all` methods, plus `use`/middleware, `route(path)` chains, and nested routers. Mount with `app.use("/prefix", router)`; params, cases where `strict` or `caseSensitive` constrain matching, and custom path patterns (`:name`, `*`) follow Express-style semantics. `static(dir)` serves files, `cors(options)` answers `OPTIONS` preflights and stamps CORS headers, `bodyLimit(n)` rejects oversize bodies with 413, and `cookieParser()` fills `req.cookies`.

Use editor completion for the precise callback types in the compiler version you target. CP3's HTTP API is not a full Express or Node compatibility layer.

## DNS (`lookup`)

`import { lookup } from "std/network"` resolves a hostname to the **first** address it finds:

```ts
const address = await lookup("example.com");   // e.g. "93.184.216.34"
```

- Signature: `lookup(hostname: string): Promise<string>`. Takes exactly one argument; extra arguments are a compile-time error.
- Resolution goes through libuv `getaddrinfo` (the same path `fetch` uses). The first result is returned; there is no `lookupAll`, no options object, and no callback form.
- Rejects with an `Error` whose message names the hostname when resolution fails.
- The namespace spellings `network.lookup(hostname)` and `dns.lookup(hostname)` (via the default import `import network from "std/network"` / `import dns from "std/network"`) lower to the same runtime, but the named import `import { lookup } from "std/network"` is the documented form.

## Raw sockets (TCP, UDP, TLS)

Available since v0.2.1. The `std/network` registry exports the names in this section; programs that never use them link nothing extra.

### TCP — `createTcpServer` / `connectTcp` / `TcpSocket`

Named import: `import { createTcpServer, connectTcp, TcpSocket, TcpServer } from "std/network"`.

```ts
import { createTcpServer, connectTcp } from "std/network";

const server = createTcpServer({ threads: 4, maxConnections: 1000, idleTimeout: 30000, maxWriteBuffer: 65536 });
server.on("connection", (socket) => {
  socket.on("data", (chunk) => {              // chunk: string | Buffer (raw bytes, no framing)
    if (!socket.write(chunk)) {               // false = queued bytes reached maxWriteBuffer
      socket.on("drain", () => { /* resume writing */ });
    }
  });
  socket.on("end", () => socket.end());       // peer sent EOF (FIN)
  socket.on("error", (err) => console.log(err.message));
  socket.pause();                             // stop "data" delivery ...
  socket.resume();                            // ... and restart it
});
server.on("error", (err) => console.log(err.message));
server.listen(8080, () => console.log("listening"));

const client = connectTcp({ host: "127.0.0.1", port: 8080, timeout: 5000 });  // default 10000 ms
client.on("connect", () => client.write("hello"));
client.on("data", (chunk) => console.log(chunk));
client.on("error", (err) => console.log(err.message));   // e.g. "ECONNREFUSED: ...", "ETIMEDOUT: ..."
client.on("close", () => {});
```

**`createTcpServer(options?)`** — options (all optional; `number`s, integers):

| Option | Default | Meaning |
| --- | --- | --- |
| `threads` | `1` | Number of `SO_REUSEPORT` listener handles on the one shared loop. Does **not** create threads; every accepted socket runs on the same loop, so handlers are never concurrent. `1..64`; other values throw at construction. On Windows the fan-out collapses to one listener (the option is accepted, not an error). |
| `maxConnections` | `0` (unlimited) | Cap on live accepted sockets. Over-cap accepts are closed immediately — no `"connection"` event, no error; the peer sees a close/reset. |
| `idleTimeout` | `0` (disabled) | Milliseconds with no data in either direction before the socket is closed with an `ETIMEDOUT` error. Inactivity in both directions rearms the timer. |
| `maxWriteBuffer` | `65536` | High-water mark in bytes for `write()` back-pressure. `0` = unbounded (`write()` always returns true, `"drain"` never fires). |

**`TcpServer`**: `listen(port, callback?)` binds `0.0.0.0`; the host form binds an IPv4 literal — `listen(port, host, callback?)`. A bind failure (bad port, port in use, bad host) throws synchronously. Events: `"connection"` (a `TcpSocket`), `"listening"`, `"close"`, `"error"`. Also `close()` (closes listeners **and every live connection** immediately — there is no graceful drain), `listening()`, `addressPort()` (resolves `listen(0)`), `connectionCount()`. One handler per event; re-registering replaces.

**`TcpSocket`** (either side of the connection): `new TcpSocket(host, port, timeout?)` starts a client connect exactly like `connectTcp`. Events: `"data"` (chunk as `string` or `Buffer`), `"end"` (peer FIN), `"close"`, `"drain"`, `"connect"`, `"error"` (an `Error` object — `name` is `"Error"`, the Node-style machine code is a **message prefix**, not a property: `err.message` like `"ECONNREFUSED: connect ECONNREFUSED ..."`, `"ETIMEDOUT: connect timeout after N ms"`, `"EPIPE: ..."`; CP3 `Error` has no `.code`). Methods: `write(string | Buffer) -> boolean` (Node-style back-pressure: bytes are always queued unless the socket is closed/ended — then it returns `false` without queueing — and `false` means "queued bytes reached `maxWriteBuffer`, wait for `\"drain\"`"), `end([data])` (queue `data` if any, flush, then half-close with a FIN — the socket stays readable until the peer's EOF), `close()` (destroy immediately, queued bytes discarded), `pause()` / `resume()`, `isPaused()`, `isClosed()`, `isConnected()`, `bufferedBytes()`. Addresses: `remoteAddress()` / `localAddress()` (`"ip:port"` text, `""` when not connected), `remotePort()` / `localPort()`. Writes before `"connect"` are queued and flushed on connect, like Node.

**`connectTcp(options)`** — `connectTcp({ host, port, timeout?, idleTimeout?, maxWriteBuffer? })`, or the convenience `connectTcp(host, port, timeout?)`. **It is not a Promise**: the socket is returned immediately and outcomes arrive via `"connect"` / `"error"` / `"close"` (like Node's `net.createConnection`). `host` may be an IPv4 literal or a hostname (DNS via libuv, IPv4 only — IPv6 literals are rejected). `port` is required in practice; `0` is rejected. `timeout` defaults to `10000` ms; on expiry the socket emits `"error"` (`ETIMEDOUT`) then `"close"`. `timeout: 0` disables the connect timer (the OS TCP timeout applies).

Deliberate limits: IPv4 only for bind and connect; `"data"` chunks are raw TCP segments (libuv read sizes, up to 64 KiB) with **no framing** — the app must frame itself (length prefix, delimiter, ...); no TLS and no UDP on this path; server `close()` discards live connections and unflushed bytes.

### UDP — `createUdpSocket` / `UdpSocket`

Named import: `import { createUdpSocket } from "std/network"`.

```ts
import { createUdpSocket } from "std/network";

const socket = createUdpSocket("udp4");        // "udp4" | "udp6"
socket.on("message", (msg, from) => {          // msg: Buffer, from: { address, port, family }
  console.log("datagram from", from.address, from.port);
});
socket.bind(48244);                            // or socket.bind(port, "127.0.0.1")
socket.send("ping", 48244, "127.0.0.1");       // string (UTF-8) or Buffer
socket.close();
```

- `createUdpSocket(type)` — `type` must be `"udp4"` or `"udp6"`; anything else throws.
- `socket.bind(port)` binds the wildcard address of the family (`0.0.0.0` / `::`); `bind(port, address)` binds a **numeric IP literal** (no hostnames — there is no DNS on the UDP path); `bind(port, callback)` runs the callback once the port is bound (`"listening"` fires too). Port `0` requests an ephemeral port. Failure (bad port, address in use, already bound) throws synchronously.
- `socket.send(data, port, address)` sends one datagram to a numeric IP literal; the socket need not be bound first. Fire-and-forget: a **send-time** failure is reported via the `"error"` event, not thrown — but a bad port, unparseable address, or closed socket throws synchronously.
- `"message"` fires with the datagram as an owning `Buffer` and a `from = { address, port, family }` struct (`family` is `"IPv4"` | `"IPv6"`; `port` is `u16`, widened to `number` in a `typescript` project).
- Chunks larger than 64 KiB are truncated at the per-thread read buffer; there is no reassembly. One `thread_local` 64 KiB read buffer per thread receives every datagram (the delivered `Buffer` is the single copy).
- One handler per event (re-registration replaces); all callbacks run on the runtime (libuv) thread — not safe to call from arbitrary OS threads; `address()` returns the bound `{ address, port, family }` and throws when not bound; `close()` is idempotent and fires `"close"` once.
- Not supported: multicast / broadcast options, `setMulticastTTL`, dual-stack, `connect()` / `disconnect()`, packet-info. Depending on the family for `bind`/`send` address forms.

### TLS over raw sockets — `connectTls` / `createTlsServer` / `TlsSocket`

Named import: `import { connectTls, createTlsServer, TlsSocket } from "std/network"`. These ride the same vendored TLS engine (OpenSSL) that `fetch("https://…")`, `wss://`, and `createServer({ tls })` use.

```ts
import { connectTls, createTlsServer } from "std/network";

const server = createTlsServer({ cert: "cert.pem", key: "key.pem" }, (socket) => {
  socket.on("data", (chunk) => socket.write(chunk));
});
server.listen(443, () => console.log("listening"));

const socket = connectTls({ host: "example.com", port: 443 });   // or connectTls("example.com", 443)
socket.on("secureConnect", () => socket.write("hello"));
socket.on("error", (err) => console.log(err.message));           // "TLSError: [CERT_UNTRUSTED] ..."
```

**`connectTls(options | host, port?, options?)`** — returns the `TlsSocket` immediately (like `connectTcp`, not a Promise). Throws a `TLSError` synchronously for invalid options or an unavailable backend; handshake/verify failures are delivered on the socket's `"error"` event.

**`createTlsServer(options?, handler?)`** — `handler(socket)` is the `"secureConnection"` listener (also reachable via `on("secureConnection", …)`). Configuration errors (missing/unreadable cert/key, `minProtocol` below TLSv1.2, `maxSendFragment` outside 256..65536, negative limits, server without `cert`/`key`) throw a `TLSError` synchronously; handshake failures arrive as socket `"error"` events.

**`TlsOptions`** (Node-style names): `host`, `port` (default `443`), `servername` (SNI override; `""` = `host`), `ca` / `cert` / `key` / `keyPassphrase` (PEM file paths or inline PEM text), `rejectUnauthorized` (default `true`), `checkServerIdentity` (default `true`), `minProtocol` (default `"TLSv1.2"`; TLS1.0/1.1 floors are refused), `maxProtocol` (default `"TLSv1.3"`), `alpnProtocols`, `ciphers`, `maxSendFragment` (default `16384`), `handshakeTimeoutMs` (default `10000`; `0` = server default, never infinite), `maxHandshakesPerSecond` (default `50`, per-server token bucket), `maxConcurrentHandshakes` (default `128`), `allowHalfOpen`, `readable`, `writable`. Note: `createServer({ tls })` / `new WebSocketServer({ port, tls })` take **file paths only** — these sockets' inline-PEM freedom does not apply there.

**`TlsSocket`** mirrors `TcpSocket` member for member — `write` / `end` / `close` / `destroy` / `pause` / `resume` / `setTimeout` / `setNoDelay` / `setKeepAlive`, `connect`, the events `"data"`, `"end"`, `"close"`, `"connect"`, `"error"`, `"timeout"`, and the properties `connecting` / `destroyed` / `readable` / `readableEnded` / `writable` / `writableEnded` / `remoteAddress` / `remotePort` / `localAddress` / `localPort` / `bytesRead` / `bytesWritten` — plus the TLS additions: event `"secureConnect"` (fires once the handshake completes; `"connect"` fires with it), `authorized()`, `encrypted()`, `alpnProtocol()`, `servername()`, `getProtocol()`, `getCipher()` → `{ name, version, standardName }`, `getPeerCertificate(raw?)` → `{ subject, issuer, serialNumber, fingerprint, notBefore, notAfter, pem? }`, `setMaxSendFragment(n)` / `getMaxSendFragment()`. Deliberate differences from `TcpSocket`: `write()` before `"secureConnect"` is **dropped** (Node buffers it) and emits `"error"` (`ERR_TLS_NOT_CONNECTED`) instead; `bytesRead()` counts **ciphertext** bytes, `bytesWritten()` plaintext — they are not on the same side of the engine.

**`TlsServer`**: events `"secureConnection"` (a `TlsSocket`), `"error"`, `"close"`; `listen(port, callback?)` / `listen(port, host, callback?)` (IP literal host), `close(callback?)` (stop accepting, destroy in-flight handshakes; established sockets are the caller's job), `addressPort()`.

**`TLSError`** — the `Error` object every TLS failure is delivered as: thrown from the factories, passed to `"error"` listeners. `name` is `"TLSError"`; the machine code travels in the message as `"[CODE] ..."`, e.g. `"[CERT_UNTRUSTED] ..."`. Codes: `CERT_UNTRUSTED`, `ERR_TLS_HANDSHAKE_TIMEOUT`, `ERR_TLS_HANDSHAKE_RATE_LIMITED`, `ERR_TLS_HANDSHAKE_FAILED`, `ERR_TLS_PROTOCOL_TOO_OLD`, `ERR_TLS_NO_PEER_CERTIFICATE`, `ERR_TLS_NOT_CONNECTED`, `ERR_TLS_SOCKET_DESTROYED`, `ERR_TLS_INVALID_OPTIONS`, `ERR_TLS_BACKEND_UNAVAILABLE` (the fail-closed stub backend — builds without the OpenSSL archives installed never silently downgrade to plaintext).

Deliberate limits: one certificate/key per `TlsServer` (no per-SNI callback), no session resumption or tickets, no client-cert request, no OCSP stapling; `setMaxSendFragment` after the handshake is stored but only applied to the next connection; the rate limiter is per-server and in-process.

### What to use when

- HTTP(S) client — `fetch` (see **Fetch**); HTTPS is verified against the embedded CA bundle by default.
- WebSocket client/server — `WebSocket` / `WebSocketServer`, including TLS servers (see the WebSocket sections).
- HTTPS server — `createServer({ tls })` or reusable `tls({ ... })` options (see **TLS trust and certificates**), including `new WebSocketServer({ port, tls })`.
- Raw TCP/UDP/TLS sockets — `createTcpServer` / `connectTcp`, `createUdpSocket`, `connectTls` / `createTlsServer` (see **Raw sockets**). When a native library already has a socket and a non-blocking step API, `std/io` exposes the reactor primitives instead (see [I/O readiness](./18-io.md)).
- Hostname resolution — `lookup`.

Note that `createServer({ tls })` / `new WebSocketServer({ port, tls })` take PEM **file paths** only — the raw TLS sockets are the path for inline PEM material (see **TLS over raw sockets**).

Treat network responses as external input. Check status codes, validate parsed JSON, and set a timeout when an operation must have a bounded duration. Network operations may fail because of DNS, connection, timeout, protocol, or remote-server errors. Catch errors at a layer that can retry safely or report context.

## Resource lifetime

Close WebSocket connections and HTTP servers explicitly. Avoid starting a listener as a side effect of importing a module; create and listen to a server from the program's startup flow so the lifecycle is visible.

## Requirements and platforms

Networking is built on the compiler's bundled libuv and OpenSSL (TLS 1.2 floor), and HTTPS verification trusts an embedded Mozilla CA bundle with an `SSL_CERT_FILE` override. The supported targets are Windows x64 and Linux x64; Windows/Linux ARM64 packages exist but are untested.

## Related pages

- [I/O readiness](./18-io.md) — reactor primitives for native socket libraries
- [JSON](./09-json.md) — `response.json<T>()` parsing
- [URL extensions](./24-url-extensions.md) — URL handling behind fetch and servers
- [Cryptography](./10-crypto.md) — key material and hashes
