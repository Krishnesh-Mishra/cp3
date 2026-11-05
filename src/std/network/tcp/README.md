# TCP (`src/std/network/tcp/**`)

Solo subagent deliverable. Nothing outside this directory was created/edited.
No build/test/commit was run.

## Files

| File | Role |
|---|---|
| `include/cp3/std/network/tcp.hpp` | Generated-code contract: `TcpServerOptions`, `TcpConnectOptions`, `TcpSocket`, `TcpServer`, `createTcpServer`, `connectTcp`. |
| `runtime_tcp.hpp` | Private `TcpSocketImpl`/`TcpServerImpl`, event slots, lifecycle + REUSEPORT helpers shared by the three TUs. |
| `runtime_tcp_socket.cpp` | `TcpSocket`: `write` back-pressure/`drain`, `pause`/`resume`, `end`/`close`, `data`/`end`/`close`/`error`/`drain`/`connect` emits, idle timer, Error-object errors. |
| `runtime_tcp_client.cpp` | `connectTcp`: IPv4 fast path, `uv_getaddrinfo` hostnames, default connect timeout (`timeout`, default 10000 ms), DNS/refused/timeout → `error` Error. |
| `runtime_tcp_server.cpp` | `TcpServer`: `uv_tcp_t` listeners + accept, `maxConnections` accept-and-drop, `connection`/`listening`/`close`/`error`, `addressPort`/`connectionCount`. |
| `runtime_tcp_reuseport.cpp` | `threads` → N SO_REUSEPORT listener handles on the **same** loop (no threads created). Windows: collapses to one listener. |
| `sema_tcp.inc` | Paste fragment for `src/std/network/codegen/sema.cpp` (not compiled). |
| `codegen_tcp.inc` | Paste fragments for `codegen.cpp`/`codegen_methods.cpp` (not compiled). |
| `native_tcp.hpp` | Splice fragment for `src/std/network/codegen/native.hpp` (not compiled). |
| `README.md` | This file. |

## Export surface (CP3)

```cp3
import { createTcpServer, connectTcp } from "std/network";

const server = createTcpServer({ threads: 4, maxConnections: 1000, idleTimeout: 30000, maxWriteBuffer: 65536 });
server.on("connection", (socket) => {
  socket.on("data", (chunk) => {           // chunk: string | Buffer (raw bytes)
    if (!socket.write(chunk)) {            // false = buffer >= maxWriteBuffer
      socket.on("drain", () => { /* resume writing */ });
    }
  });
  socket.on("end", () => socket.end());    // peer FIN
  socket.on("error", (err) => console.log(err.message));  // err: Error object
  socket.pause();                          // stop "data" ...
  socket.resume();                         // ... and restart it
});
server.on("error", (err) => console.log(err.message));
server.listen(8080, () => console.log("listening"));

const client = connectTcp({ host: "127.0.0.1", port: 8080, timeout: 5000 }); // default 10000
client.on("connect", () => client.write("hello"));
client.on("data", (chunk) => console.log(chunk));
client.on("error", (err) => console.log(err.message));  // e.g. "ECONNREFUSED: connect ECONNREFUSED ...", "ETIMEDOUT: connect timeout after 5000 ms"
client.on("close", () => {});
// also: new TcpSocket(host, port, timeout?), socket.end([data]), socket.close(),
// socket.isPaused()/isClosed()/isConnected(), socket.bufferedBytes(),
// socket.remoteAddress()/localAddress()/remotePort()/localPort(),
// server.close(), server.listening(), server.addressPort(), server.connectionCount()
```

## Exact wiring for lead (shared files — you own these edits)

1. **Native row** — `src/std/network/codegen/native.hpp` (`kNetworkNativePackage`): see `native_tcp.hpp` for the exact snippet (include dir `src/std/network/tcp/include`, 4 runtime sources, 4 parallel markers). No link/define/CMake change.
2. **Sema** — paste `sema_tcp.inc` blocks 1–5 into `src/std/network/codegen/sema.cpp`, plus `{"TcpSocket", "std/network"}` / `{"TcpServer", "std/network"}` in `src/sema/src/checks/type/std_check.cpp` next to the WebSocket rows.
3. **Codegen** — paste `codegen_tcp.inc` blocks A–E into `src/std/network/codegen/codegen.cpp` + `codegen_methods.cpp` (markers, `createTcpServer`/`connectTcp`/`new TcpSocket` lowering, event-param contexts, `needsTcp_` include of `cp3/std/network/tcp.hpp`).
4. **Tests** — no test files added (code-only task). Suggested cases for the lead: loopback echo (server + `connectTcp`), `write` returns false at a tiny `maxWriteBuffer` then `drain` fires, `pause`/`resume` halts/restarts `data`, connect to a closed port yields an `error` Error with `ECONNREFUSED`, `timeout: 1` yields `ETIMEDOUT`, `maxConnections: 1` drops the second client.

## Limitations / deliberate deviations

- **`threads` creates no threads.** It is the number of SO_REUSEPORT listener handles on the one shared loop (task's SOME_REUSEPORT). This is the opposite of `HttpServer({ threads })` (D79, one loop per thread); a TCP connection never moves loops and handlers never run concurrently.
- **Windows:** no SO_REUSEPORT fan-out — `threads > 1` collapses to a single `SO_REUSEADDR`-equivalent listener (slot 0 only). The option is accepted, not an error.
- **IPv4 only** for bind and connect (hostnames resolve with `AF_INET`), matching fetch/WS in this build. IPv6 literals are rejected (`listen` throws; `connectTcp` fails DNS with `ENOTFOUND`).
- **Error objects:** `error` handlers receive a real cp3 `Error` (`Thrown` carrying `Ref<::cp3::Error>`, so `err.message`/`err.name`/`err.stack` work), not a boxed string like `WebSocket`'s. CP3 `Error` has no `.code` field, so the Node-style code (`ECONNREFUSED`, `ETIMEDOUT`, `EPIPE`, `ENOTFOUND`) is a message prefix, not a property.
- **`write()` never drops bytes** when it returns `false` (Node semantics): the data is queued, the return value says "stop and wait for `drain`". Only writes after `end()`/`close()` return `false` without queueing (plus an `ERR_STREAM_WRITE_AFTER_END` Error).
- **`connectTcp` is not a Promise** (like `net.createConnection`): it returns the socket immediately; connect/DNS/timeout outcomes arrive via `connect`/`error`/`close`. `timeout: 0` disables the connect timer (OS TCP timeout applies).
- **`maxConnections` over-cap = silent accept-and-drop** (no `connection`, no `error` event); the peer sees a close/reset. A per-server `error` for rejects would need a lead decision.
- **`idleTimeout` is inactivity in both directions** (re-armed on any read or write), one-shot per idle window; on expiry the socket emits an `ETIMEDOUT` Error and closes. `0` disables it.
- **`end()` is a half-close** (FIN after the outbox flushes); the socket stays readable until the peer's EOF, then `end` + `close` fire. `close()` discards unflushed queued bytes.
- **`data` chunks are raw TCP segments** (libuv read sizes, up to 64 KiB) with no framing/message boundaries — the app must frame (length-prefix, delimiters, …) itself, exactly like Node `net`.
- **No TLS, no UDP, no `net.Server` listen hostnames** beyond IPv4 literals (D70 TLS-off; this is TCP only).
- **Server `close()`** closes listeners and every live connection immediately (queued writes are discarded); there is no graceful-drain / `server.close()`-then-wait-for-connections mode.
- **`addressPort()`** resolves `listen(0)` from slot 0's `getsockname`; with `threads > 1` all slots bind the same (resolved) port via REUSEPORT.
