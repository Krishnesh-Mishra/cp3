---
title: Networking
chapter: Chapter 3 · Standard library
---
# Networking

`std/network` exports `fetch`, `WebSocket`, `WebSocketServer`, and `createServer` as named imports:

```ts
import { fetch, WebSocket, WebSocketServer, createServer } from "std/network";
```

Network APIs are asynchronous and depend on DNS, remote hosts, ports, and operating-system resources. Handle rejection and close resources when finished.

## Fetch

`fetch(url, init?)` sends an HTTP request and resolves to a response object. The current runtime accepts `http://` URLs; HTTPS/TLS is not implemented in this version. The optional initialization object supports `method`, `headers` (`Map<string, string>`), `body`, and `timeout` in milliseconds.

```ts
const response = await fetch("http://127.0.0.1:8080/health");
if (!response.ok) {
  throw new Error("health endpoint returned an error");
}
const text = await response.text();
```

`FetchResponse` exposes `ok`, `status`, `statusText`, and `headers`. Its body is buffered before `fetch` resolves. `text()` returns a promise for the text body, `arrayBuffer()` returns a promise for a Buffer, and `json<T>()` parses supported scalar or reified interface shapes. A failed HTTP status still resolves to a response; check `ok` or `status`. DNS, connection, timeout, unsupported-scheme, and protocol failures reject the promise.

A timeout of zero means no timeout. A request that never settles remains pending and keeps event-loop work alive. Use a positive timeout for operations that need a deadline. Headers are represented by a CP3 Map, and the implementation owns framing headers such as host and content length.

## WebSocket client

`new WebSocket(url)` creates a client connection. The socket supports event registration, `send` for string or Buffer payloads, and `close(code?, reason?)`. Event callbacks cover connection open, message, close, and error according to the runtime surface.

```ts
const socket = new WebSocket("ws://127.0.0.1:9000/events");
socket.on("open", (): void => socket.send("hello"));
```

Wait for the `"open"` event before sending application messages. `"message"` receives text frames as strings and binary frames as Buffer values. `"close"` receives a code and reason; `"error"` receives an error value. There is one handler slot per event, so registering a later listener for the same event replaces the previous listener. Close the socket when the exchange is complete; an open socket keeps event-loop work alive.

## WebSocket server

`new WebSocketServer({ port, path? })` accepts a port and an optional route path (default `"*"`). Register a `"connection"` handler that receives a WebSocket, optionally with the HTTP request. A server connection is represented by the same WebSocket handle surface. Call `close()` during shutdown. The current client supports `ws://`; TLS WebSockets are not available.

## HTTP server with `createServer`

`createServer()` constructs an HTTP server with route registration and middleware support. The options object currently supports `threads`, an integer of at least one. Each route handler can be synchronous or return `Promise<void>`. Routes and middleware execute in registration order. Calling `next()` from middleware continues the chain; it returns `Promise<void>`, so async middleware may use `await next()`. A chain that completes without sending a response returns 404.

```ts
const server = createServer();
server.get("/health", (request, response): void => {
  response.status(200).header("content-type", "text/plain");
  response.text("ok");
});
server.listen(8080, (): void => console.log("ready"));
```

The server supports `get`, `post`, `put`, `delete`, `patch`, `head`, `options`, `any`, and `use`. `listen(port, callback?)` begins accepting requests; `close()` shuts down the listener and connections. Request values include method, path, route `params`, query Map, and headers Map. Request bodies are buffered before dispatch; `text()` and `json<T>()` read that body.

Responses support chainable `status(code)` and `header(name, value)`, plus terminal `text`, `json`, and `send` operations. `write(string | Buffer)` and `end()` provide chunked response writes. The first terminal response operation wins; code that tries to send a second response does not start a new response.

Use editor completion for the precise callback types in the compiler version you target. CP3's HTTP API is not a full Express or Node compatibility layer.

## Timeouts, errors, and untrusted data

Treat network responses as external input. Check status codes, validate parsed JSON, and set a timeout when an operation must have a bounded duration. Network operations may fail because of DNS, connection, timeout, protocol, or remote-server errors. Catch errors at a layer that can retry safely or report context.

## Resource lifetime

Close WebSocket connections and HTTP servers explicitly. Avoid starting a listener as a side effect of importing a module; create and listen to a server from the program's startup flow so the lifecycle is visible.
