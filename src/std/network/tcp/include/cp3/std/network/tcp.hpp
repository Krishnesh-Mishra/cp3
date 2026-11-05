// =============================================================================
// cp3/std/network/tcp.hpp - `std/network` raw TCP backing surface.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). The header is the
// generated-code contract, so keep it additive like every cp3/std header, D14.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 §network, TCP extension)
// -----------------------------------------------------------------------------
//   createTcpServer(options?)        -> ::cp3::runtime::network::tcp::createTcpServer(TcpServerOptions)
//   connectTcp(options)              -> ::cp3::runtime::network::tcp::connectTcp(TcpConnectOptions)
//   new TcpSocket(host, port, timeout?)
//                                    -> ::cp3::runtime::network::tcp::TcpSocket(host, port, timeout)
//   socket.write(string | Buffer)    -> bool   (back-pressure, see below)
//   socket.on("data", chunk)         -> chunk: string | Buffer (raw bytes, no framing)
//   socket.on("end", () => void)     -> peer sent EOF (FIN)
//   socket.on("close", () => void)   -> socket fully closed (after end/error/close())
//   socket.on("error", (err) => ...) -> err is a cp3 Error object (Thrown carrying
//                                       Ref<::cp3::Error>; err.message / err.name / err.stack)
//   socket.on("drain", () => void)   -> write buffer drained below the high-water mark
//   socket.on("connect", () => void) -> client socket connected (server sockets: already
//                                       connected when "connection" fires, so the event
//                                       is not replayed for them)
//   server.on("connection", (socket) => ...) / server.on("listening" | "close", () => ...)
//   server.on("error", (err) => ...) -> err is a cp3 Error object, as above
//   server.listen(port[, host][, callback])
//
// -----------------------------------------------------------------------------
// EVENT-LOOP MODEL (matches cp3/std/network.hpp)
// -----------------------------------------------------------------------------
//   * One libuv loop per thread, created by std/network (runtime_loop.hpp) and
//     shared with fetch / the HTTP server / WebSockets. Every uv_tcp_t in this
//     module - client, accepted, and listener - lives on that same loop.
//     This module NEVER creates a thread per connection, and the `threads`
//     option does not create threads at all (see below).
//   * A live socket / listening server each retainNetwork() on the runtime
//     loop, so the program cannot exit while that work is pending.
//   * All handlers run on the runtime thread, inside libuv callbacks.
//   * No TLS (D70, TLS off): plaintext TCP only, like beta-one fetch.
//
// -----------------------------------------------------------------------------
// `threads` / SOME_REUSEPORT
// -----------------------------------------------------------------------------
//   `createTcpServer({ threads: n })` with n > 1 creates n listener handles
//   (uv_tcp_t) on the SAME loop, all bound to the same port with SO_REUSEPORT
//   (POSIX). The kernel load-balances accepts across the listeners; every
//   accepted socket still runs on the one shared loop, so handlers are never
//   concurrent and never move loops. On Windows SO_REUSEPORT is unavailable
//   in libuv's model here, so n listeners collapse to one `SO_REUSEADDR`
//   listener (the option is accepted and ignored there - see README).
//   `threads: 1` (the default) is exactly one listener.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/std/buffer.hpp"

namespace cp3::runtime::network::tcp {

namespace detail {
struct TcpSocketImpl;
struct TcpServerImpl;
}  // namespace detail

// =============================================================================
// Options
// =============================================================================

/// `createTcpServer(options?)`. All numeric fields are CP3 `number`s (double).
///   threads:        number of SO_REUSEPORT listener handles on the same loop
///                   (integer >= 1, default 1). No threads are created.
///   maxConnections: cap on live accepted sockets (0 = unlimited, default).
///                   Over-cap accepts are closed immediately without a
///                   "connection" event (see README limitations).
///   idleTimeout:    ms with no data in either direction before the socket is
///                   closed with an ETIMEDOUT Error (0 = disabled, default).
///   maxWriteBuffer: high-water mark in bytes for write() back-pressure
///                   (default 65536; 0 = unbounded, write() always true).
struct TcpServerOptions {
    double threads = 1.0;
    double maxConnections = 0.0;
    double idleTimeout = 0.0;
    double maxWriteBuffer = 65536.0;
};

/// `connectTcp(options)`.   host: IPv4 literal or hostname (DNS via libuv,
///   IPv4 only - the same limit as fetch/WS in this build).
///   port:   integer 1..65535 (required in practice; 0 is rejected).
///   timeout: connect timeout in ms (default 10000). When it elapses before
///            the TCP connect completes, the socket emits an "error" event
///            with an Error ("ETIMEDOUT: connect timeout after N ms") and
///            then "close". 0 disables the connect timer (the socket then
///            relies on the OS TCP timeout, like fetch with timeout 0).
///   idleTimeout / maxWriteBuffer: same meaning as on TcpServerOptions;
///            they configure this one client socket.
struct TcpConnectOptions {
    std::string host = "127.0.0.1";
    double port = 0.0;
    double timeout = 10000.0;
    double idleTimeout = 0.0;
    double maxWriteBuffer = 65536.0;
};

// =============================================================================
// TcpSocket
// =============================================================================

/// One raw TCP connection, either side. The class is a handle: copies share
/// the same underlying connection (Ref semantics, like WebSocket).
///
/// `write(data)` back-pressure (Node `net.Socket` semantics):
///   * The bytes are always queued (unless the socket is closed/ended, in
///     which case write returns false and emits an "error" Error).
///   * It returns `false` when the queued-but-not-yet-written byte count
///     has reached `maxWriteBuffer` (the high-water mark) - the caller
///     should stop writing and wait for the "drain" event.
///   * "drain" fires once, when the queued count falls back below half the
///     high-water mark after having reached it.
///   * With `maxWriteBuffer: 0` (unbounded) write() always returns true and
///     "drain" never fires.
///
/// `pause()` / `resume()` stop / restart the "data" flow (libuv read pause).
/// Buffered kernel bytes are not lost; they are delivered after resume().
class TcpSocket {
public:
    TcpSocket();
    /// `new TcpSocket(host, port, timeout?)`: starts a client connect, exactly
    /// like `connectTcp({ host, port, timeout })`. Invalid host/port values
    /// throw a cp3 Error (Thrown), like `new WebSocket(url)` does.
    TcpSocket(std::string host, double port, double timeoutMs = 10000.0);

    /// Internal: adopts an accepted / started connection. Generated code and
    /// the server/client glue are the only callers.
    static TcpSocket adopt(const std::shared_ptr<detail::TcpSocketImpl>& impl);
    static std::shared_ptr<detail::TcpSocketImpl> sharedImpl(const TcpSocket& socket) {
        return socket.impl_;
    }

    /// `socket.on(event, handler)`. One handler per event (a later
    /// registration replaces the earlier one), like WebSocket::on.
    ///   "data"    -> (chunk: string) or (chunk: Buffer): the string slot
    ///                receives the UTF-8 bytes as a string, the Buffer slot
    ///                the raw bytes; when only one slot is registered it
    ///                receives every chunk (the WS conversion rule).
    ///   "end"     -> () => void            peer EOF (FIN) received
    ///   "close"   -> () => void            socket closed
    ///   "drain"   -> () => void            write buffer drained (see above)
    ///   "connect" -> () => void            client connect completed
    ///   "error"   -> (err) => void         err is a cp3 Error object:
    ///                a Thrown carrying Ref<::cp3::Error>; in CP3 the handler
    ///                parameter binds the Error (err.message, err.name,
    ///                err.stack). Its message is prefixed with a Node-style
    ///                code, e.g. "ECONNREFUSED: connect ECONNREFUSED ...".
    void on(const std::string& event, std::function<void()> handler) const;
    void on(const std::string& event, std::function<void(const std::string&)> handler) const;
    void on(const std::string& event, std::function<void(const buffer::Buffer&)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;

    /// `socket.write(string | Buffer) -> boolean`. See the back-pressure
    /// contract on the class. Safe to call before "connect" (client): the
    /// bytes are queued and flushed once connected, like Node.
    bool write(const std::string& data) const;
    bool write(const buffer::Buffer& data) const;

    /// `socket.end([data])`: queue `data` (if any), flush, then half-close
    /// (FIN). The socket stays readable until the peer's EOF / close.
    void end() const;
    void end(const std::string& data) const;
    void end(const buffer::Buffer& data) const;

    /// `socket.close()`: destroy the socket immediately (queued writes that
    /// have not been handed to libuv are discarded). "close" fires after.
    void close() const;

    /// `socket.pause()` / `socket.resume()`: stop / restart "data" events.
    void pause() const;
    void resume() const;
    bool isPaused() const;

    bool isClosed() const;
    bool isConnected() const;
    /// Queued-but-not-yet-written bytes right now (the back-pressure meter).
    std::size_t bufferedBytes() const;

    /// "ip:port" text forms ("" when not connected yet / unknown).
    std::string remoteAddress() const;
    std::string localAddress() const;
    double remotePort() const;
    double localPort() const;

private:
    std::shared_ptr<detail::TcpSocketImpl> impl_;
};

// =============================================================================
// TcpServer
// =============================================================================

/// `createTcpServer(options?)`. Listens with `listen(port[, host][, cb])`;
/// accepted sockets arrive on the "connection" event as TcpSocket handles
/// that stay valid for the life of the connection (the server keeps them
/// alive, like WebSocketServer's connections map).
class TcpServer {
public:
    explicit TcpServer(const TcpServerOptions& options = TcpServerOptions());

    /// `server.on(event, handler)`:
    ///   "connection" -> (socket: TcpSocket) => void
    ///   "listening"  -> () => void        (also fires the listen callback)
    ///   "close"      -> () => void
    ///   "error"      -> (err) => void     err is a cp3 Error object (Thrown
    ///                   carrying Ref<::cp3::Error>), as on TcpSocket.
    void on(const std::string& event, std::function<void()> handler) const;
    void on(const std::string& event, std::function<void(TcpSocket&)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;

    /// `server.listen(port, callback?)` binds 0.0.0.0; the host form binds
    /// the given IPv4 literal. A bind failure (bad port, port in use, bad
    /// host) throws a cp3 Error (Thrown) synchronously, like HttpServer.
    void listen(double port, std::function<void()> callback = {}) const;
    void listen(double port, const std::string& host, std::function<void()> callback = {}) const;

    /// `server.close()`: stop listening and close every live connection.
    void close() const;

    bool listening() const;
    /// The bound port (resolves the ephemeral port when listen(0)). 0 when
    /// not listening.
    double addressPort() const;
    std::size_t connectionCount() const;

private:
    std::shared_ptr<detail::TcpServerImpl> impl_;
};

// =============================================================================
// Factories (the documented entry points)
// =============================================================================

/// `createTcpServer(options?)`.
TcpServer createTcpServer(const TcpServerOptions& options = TcpServerOptions());

/// `connectTcp({ host, port, timeout? })`: returns the socket immediately;
/// the connect is asynchronous and reports through "connect" / "error" /
/// "close" events (there is no Promise - like Node's net.createConnection).
/// Hostname resolution and the default connect timeout are handled inside.
TcpSocket connectTcp(const TcpConnectOptions& options);
/// `connectTcp(host, port, timeout?)` convenience spelling.
TcpSocket connectTcp(const std::string& host, double port, double timeoutMs = 10000.0);

}  // namespace cp3::runtime::network::tcp
