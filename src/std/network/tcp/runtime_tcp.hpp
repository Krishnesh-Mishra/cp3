// =============================================================================
// src/std/network/tcp/runtime_tcp.hpp - PRIVATE std/network TCP surfaces.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). Shared by
// runtime_tcp_socket.cpp (TcpSocket + emit/flush/idle), runtime_tcp_client.cpp
// (connectTcp) and runtime_tcp_server.cpp (TcpServer + REUSEPORT listeners);
// never installed and never part of the generated-code contract.
//
// One TcpSocketImpl per connection (either side). The libuv handles live
// inside it (uv_tcp_t etc., like FetchOp/WsClient), and every handle's
// data pointer holds a heap shared_ptr<TcpSocketImpl> (the attach/opOf
// pattern of runtime_fetch_io.cpp / runtime_ws_client.cpp) so the impl
// outlives the handle that libuv is calling back into.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <uv.h>

#include "cp3/core/error.hpp"
#include "cp3/std/network/tcp.hpp"

namespace cp3::runtime::network::tcp::detail {

// =============================================================================
// Defaults (pinned here so client, server and socket share one definition)
// =============================================================================
inline constexpr double kDefaultConnectTimeoutMs = 10000.0;
inline constexpr double kDefaultMaxWriteBuffer = 65536.0;
inline constexpr std::size_t kReadBufferSize = 65536;

/// Validates a CP3 `number` option as a non-negative integer count/byte
/// value. Returns the value, or -1 on invalid (caller throws a Thrown).
double validatedCount(double value, double maxValue);
/// Validates a port (integer 0..65535); -1 on invalid.
int validatedPort(double value);
/// A cp3 Error object for this module: Thrown carrying Ref<::cp3::Error>
/// (so a CP3 handler/catch binds a real Error, not a boxed string).
/// `code` (e.g. "ECONNREFUSED") prefixes the message Node-style; the Error
/// has no `.code` field in CP3 (see README limitations).
Thrown makeTcpError(const std::string& code, const std::string& message);
Thrown makeTcpError(const std::string& message);

// =============================================================================
// Event slots (the WsSlots pattern of runtime_ws.hpp)
// =============================================================================
struct TcpSlots {
    std::function<void()> none;
    std::function<void(const std::string&)> text;        // "data" as string
    std::function<void(const buffer::Buffer&)> binary;   // "data" as Buffer
    std::function<void(Thrown)> thr;                     // "error" (Error obj)
};

struct TcpSocketImpl {
    uv_tcp_t tcp{};
    uv_connect_t connect{};
    uv_getaddrinfo_t dns{};
    uv_timer_t idleTimer{};
    uv_timer_t connectTimer{};
    bool tcpReady = false;
    bool connected = false;
    bool finished = false;     // retired: released the runtime loop once
    bool closedEmitted = false;
    bool ended = false;        // end() called: no more writes, FIN after flush
    bool writeEnded = false;   // FIN sent (uv_shutdown completed)
    bool eof = false;          // peer EOF received ("end" emitted)
    bool reading = false;
    bool paused = false;       // user pause()
    bool writeActive = false;  // one uv_write in flight
    bool drainPending = false; // write() returned false; "drain" owed
    bool retained = false;     // retainNetwork() held (balanced in finish)
    bool idleTimerReady = false;
    bool connectTimerReady = false;
    bool shutdownStarted = false;

    double maxWriteBuffer = kDefaultMaxWriteBuffer;  // 0 = unbounded
    double idleTimeoutMs = 0.0;                      // 0 = disabled
    double connectTimeoutMs = kDefaultConnectTimeoutMs;

    std::string outbox;          // queued bytes (single buffer, like WsClient)
    std::size_t outboxOffset = 0;  // bytes already handed to libuv
    std::string remote;          // "ip:port"
    std::string local;           // "ip:port"
    double remotePort = 0.0;
    double localPort = 0.0;

    std::map<std::string, TcpSlots> slots;

    std::size_t pendingBytes() const { return outbox.size() - outboxOffset; }
    std::size_t highWater() const {
        return maxWriteBuffer > 0.0 ? static_cast<std::size_t>(maxWriteBuffer) : 0;
    }
    std::size_t lowWater() const { return highWater() / 2; }

    void emitNone(const std::string& event);
    void emitData(const std::string& data);
    void emitError(const std::string& code, const std::string& message);
    void emitError(const std::string& message);
    /// Emits "end" once (peer EOF), then finishes the socket.
    void emitEndThenFinish();
    /// Emits "close" once; called from the uv_close callback.
    void emitClose();
};

// Socket lifecycle shared by the client and server TUs.
void armIdleTimer(const std::shared_ptr<TcpSocketImpl>& impl);
void stopIdleTimer(TcpSocketImpl& impl);
void stopConnectTimer(TcpSocketImpl& impl);
/// Flushes outbox with one uv_write at a time; emits "drain" when owed.
void flushWrites(const std::shared_ptr<TcpSocketImpl>& impl);
/// Queues bytes; returns the back-pressure answer (pending < high-water).
bool queueBytes(const std::shared_ptr<TcpSocketImpl>& impl, const std::string& data);
/// Starts reading (uv_read_start) unless paused/finished/eof.
void startReading(const std::shared_ptr<TcpSocketImpl>& impl);
/// Retires the socket once: stops timers/read, closes the handle,
/// releases the runtime loop. "close" fires from the close callback.
void finishSocket(const std::shared_ptr<TcpSocketImpl>& impl);
void failSocket(const std::shared_ptr<TcpSocketImpl>& impl, const std::string& code,
                const std::string& message);
/// Half-close (FIN) after the outbox is empty. Caller guarantees !finished.
void shutdownAfterFlush(const std::shared_ptr<TcpSocketImpl>& impl);
/// Fills remote/local text + ports from a connected handle.
void refreshAddresses(TcpSocketImpl& impl);
/// Creates the impl, its tcp handle and idle timer; retainNetwork() once.
/// Returns nullptr when the loop cannot be created (impl already failed).
std::shared_ptr<TcpSocketImpl> makeSocketImpl(double maxWriteBuffer, double idleTimeoutMs);

// Client entry point (runtime_tcp_client.cpp).
void tcpClientStart(const std::shared_ptr<TcpSocketImpl>& impl, const std::string& host,
                    int port, double timeoutMs);

// =============================================================================
// Server
// =============================================================================
struct TcpServerImpl {
    struct Listener {
        uv_tcp_t tcp{};
        bool ready = false;
    };

    TcpServerOptions options;
    std::vector<Listener> listeners;
    bool listening = false;
    bool closed = false;
    double boundPort = 0.0;
    std::string boundHost;
    bool retained = false;  // one retainNetwork() for the whole server

    std::function<void()> listening0;   // "listening" / "close"
    std::function<void()> close0;
    std::function<void(TcpSocket&)> onConnection;
    std::function<void(Thrown)> onError;

    // Live accepted sockets, keyed by impl so connection handlers that
    // capture the TcpSocket handle stay valid for the connection's life
    // (the WebSocketServer connections-map pattern).
    std::unordered_set<TcpSocketImpl*> live;
    std::map<TcpSocketImpl*, std::unique_ptr<TcpSocket>> handles;

    void emitError(const std::string& code, const std::string& message);
    void emitError(const std::string& message);
    /// Removes a retired socket from live/handles (called by finishSocket's
    /// close path through tcpServerForget below). Inline so the socket TU
    /// always links it.
    inline void forget(TcpSocketImpl* raw) {
        live.erase(raw);
        handles.erase(raw);
    }
};

/// Shared registry of accepted impls -> owning server, so finishSocket's
/// close path can forget the entry without the socket knowing its server.
/// Same-loop only, like everything else in this module, so a plain map is
/// safe (no locking). Inline and header-resident so the socket TU always
/// links tcpServerForget, even when the server TU is not selected.
inline std::unordered_map<TcpSocketImpl*, std::shared_ptr<TcpServerImpl>>&
serverSocketRegistry() {
    static std::unordered_map<TcpSocketImpl*, std::shared_ptr<TcpServerImpl>> map;
    return map;
}

/// Drops `impl` from its owning server's live set (no-op for client sockets,
/// which have no server). Inline so a pure-client program always links.
inline void tcpServerForget(TcpSocketImpl* raw) {
    auto& map = serverSocketRegistry();
    const auto it = map.find(raw);
    if (it == map.end()) return;
    it->second->forget(raw);
    map.erase(it);
}
/// uv_listen connection callback for every REUSEPORT listener.
void tcpServerAccept(uv_stream_t* listener, int status);

// REUSEPORT helpers (runtime_tcp_reuseport.cpp). `threads` becomes this many
// listener handles on the same loop; no OS threads are created anywhere.
int validatedTcpThreads(double threads);
/// Sets SO_REUSEPORT (POSIX) on a not-yet-bound tcp handle. Returns false
/// on Windows / when unsupported (caller falls back to one listener).
bool applyReusePort(uv_tcp_t* tcp);

}  // namespace cp3::runtime::network::tcp::detail
