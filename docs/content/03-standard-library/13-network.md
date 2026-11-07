---
title: Networking
chapter: Chapter 3 · Standard library
---
# Networking

`std/network` exports `fetch`, `WebSocket`, `WebSocketServer`, `createServer`, `lookup`, `tls`, `Headers`, `Request`, `Response`, `FormData`, `AbortSignal`, `Router`, `static`, `cors`, `bodyLimit`, and `cookieParser` as named imports:

```ts
import { fetch, WebSocket, WebSocketServer, createServer, lookup, tls, Headers, Request, Response, FormData, AbortSignal, Router, static, cors, bodyLimit, cookieParser } from "std/network";
```

Network APIs are asynchronous and depend on DNS, remote hosts, ports, and operating-system resources. Handle rejection and close resources when finished.

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
- **Fail closed**: a TLS misconfiguration (missing/unreadable cert/key, unloadable CA) throws `ERR_TLS_INVALID_OPTIONS` synchronously at server construction; a verification failure during the handshake surfaces as a `TLSError` with `[CODE] message` (e.g. `[CERT_UNTRUSTED] ...`, `[ERR_TLS_HANDSHAKE_TIMEOUT] ...`) as the promise rejection. On platforms without a TLS backend (currently `linux-arm64`), TLS entry points fail with `ERR_TLS_BACKEND_UNAVAILABLE` instead of silently degrading to plaintext.

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

With `tls`, `listen` serves HTTPS through a uWS `SSLApp`; the cert/key are PEM **file paths** consumed synchronously at context creation (an unreadable or non-PEM file fails construction). `tls: true` with no cert/key throws `ERR_TLS_INVALID_OPTIONS` rather than serving plaintext.

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

## Timeouts, errors, and untrusted data

Treat network responses as external input. Check status codes, validate parsed JSON, and set a timeout when an operation must have a bounded duration. Network operations may fail because of DNS, connection, timeout, protocol, or remote-server errors. Catch errors at a layer that can retry safely or report context.

## Resource lifetime

Close WebSocket connections and HTTP servers explicitly. Avoid starting a listener as a side effect of importing a module; create and listen to a server from the program's startup flow so the lifecycle is visible.
