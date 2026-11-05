// =============================================================================
// src/std/network/tcp/runtime_tcp_server.cpp - `std/network` TcpServer.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). Same selection
// rule as runtime_tcp_socket.cpp.
//
// One uv_tcp_t listener per `threads` slot (runtime_tcp_reuseport.cpp), all
// on the shared loop; uv_listen's connection callback accepts into a fresh
// uv_tcp_t per connection (also on that loop), starts reading, and emits
// "connection" with a TcpSocket handle the server keeps alive until the
// socket retires (the WebSocketServer connections-map pattern).
// maxConnections is enforced at accept time: over-cap sockets are accepted
// and immediately closed without a "connection" event.
// =============================================================================
#include "cp3/std/network/tcp.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <uv.h>

#include "../runtime/runtime_loop.hpp"
#include "runtime_tcp.hpp"

namespace cp3::runtime::network::tcp::detail {
namespace {

// impl -> owning server, so finishSocket's close path can forget the entry
// without the socket knowing its server. Same-loop only, like everything
// else in this module, so a plain map is safe (no locking).
std::unordered_map<TcpSocketImpl*, std::shared_ptr<TcpServerImpl>>& serverOf() {
    static std::unordered_map<TcpSocketImpl*, std::shared_ptr<TcpServerImpl>> map;
    return map;
}

std::shared_ptr<TcpServerImpl> serverOfHandle(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<TcpServerImpl>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

}  // namespace

void tcpServerAccept(uv_stream_t* listener, int /*status*/) {
    const std::shared_ptr<TcpServerImpl> server = serverOfHandle(reinterpret_cast<uv_handle_t*>(listener));
    if (server == nullptr || server->closed) return;
    const double maxConn = server->options.maxConnections;
    if (maxConn > 0.0 && static_cast<double>(server->live.size()) >= maxConn) {
        // Over the cap: accept-and-drop (no handle, no "connection", no
        // "error" - the peer just sees a reset/close). Documented in README.
        // Heap-allocated: uv_close is asynchronous, the handle must outlive
        // this stack frame until the close callback runs.
        auto* dropped = new uv_tcp_t{};
        if (uv_tcp_init(listener->loop, dropped) == 0) {
            (void)uv_accept(listener, reinterpret_cast<uv_stream_t*>(dropped));
            uv_close(reinterpret_cast<uv_handle_t*>(dropped),
                     [](uv_handle_t* h) { delete reinterpret_cast<uv_tcp_t*>(h); });
        } else {
            delete dropped;
        }
        return;
    }
    auto impl = makeSocketImpl(server->options.maxWriteBuffer, server->options.idleTimeout);
    if (impl == nullptr) return;
    // Reuse the accepted handle storage: makeSocketImpl made a fresh tcp,
    // but uv_accept needs the accepted socket as a stream. Accept into it.
    if (uv_accept(listener, reinterpret_cast<uv_stream_t*>(&impl->tcp)) != 0) {
        finishSocket(impl);
        return;
    }
    impl->connected = true;
    refreshAddresses(*impl);
    serverOf()[impl.get()] = server;
    server->live.insert(impl.get());
    TcpSocket socket = TcpSocket::adopt(impl);
    TcpSocket& stored = *server->handles.emplace(impl.get(), std::make_unique<TcpSocket>(socket))
                             .first->second;
    armIdleTimer(impl);
    startReading(impl);
    try {
        if (server->onConnection) server->onConnection(stored);
    } catch (...) {
        // A connection handler threw: close just this socket, keep serving.
        finishSocket(impl);
    }
}

}  // namespace

void TcpServerImpl::emitError(const std::string& code, const std::string& message) {
    if (onError) onError(makeTcpError(code, message));
}

void TcpServerImpl::emitError(const std::string& message) { emitError("", message); }

void TcpServerImpl::forget(TcpSocketImpl* raw) {
    live.erase(raw);
    handles.erase(raw);
}

void tcpServerForget(TcpSocketImpl* raw) {
    auto& map = serverOf();
    const auto it = map.find(raw);
    if (it == map.end()) return;
    it->second->forget(raw);
    map.erase(it);
}

}  // namespace cp3::runtime::network::tcp::detail

namespace cp3::runtime::network::tcp {

TcpServer::TcpServer(const TcpServerOptions& options)
    : impl_(std::make_shared<detail::TcpServerImpl>()) {
    impl_->options = options;
    if (detail::validatedTcpThreads(options.threads) < 0) {
        throw Thrown::make(
            "std/network: createTcpServer threads must be an integer in 1..64");
    }
    if (detail::validatedCount(options.maxConnections, 1099511627776.0) < 0.0) {
        throw Thrown::make(
            "std/network: createTcpServer maxConnections must be a non-negative integer");
    }
    if (detail::validatedCount(options.idleTimeout, 1099511627776.0) < 0.0) {
        throw Thrown::make(
            "std/network: createTcpServer idleTimeout must be a non-negative integer");
    }
    if (detail::validatedCount(options.maxWriteBuffer, 1099511627776.0) < 0.0) {
        throw Thrown::make(
            "std/network: createTcpServer maxWriteBuffer must be a non-negative integer");
    }
    detail::ensureLoop();
}

void TcpServer::on(const std::string& event, std::function<void()> handler) const {
    if (!impl_) return;
    if (event == "listening") impl_->listening0 = std::move(handler);
    else if (event == "close") impl_->close0 = std::move(handler);
}

void TcpServer::on(const std::string& event, std::function<void(TcpSocket&)> handler) const {
    if (!impl_ || event != "connection") return;
    impl_->onConnection = std::move(handler);
}

void TcpServer::on(const std::string& event, std::function<void(Thrown)> handler) const {
    if (!impl_ || event != "error") return;
    impl_->onError = std::move(handler);
}

void TcpServer::listen(double port, std::function<void()> callback) const {
    listen(port, std::string(), std::move(callback));
}

void TcpServer::listen(double port, const std::string& host,
                       std::function<void()> callback) const {
    using namespace detail;
    if (!impl_ || impl_->listening || impl_->closed) return;
    const int p = validatedPort(port);
    if (p < 0) throw Thrown::make("std/network: listen port must be an integer in 0..65535");
    struct sockaddr_in addr{};
    const std::string bindHost = host.empty() ? "0.0.0.0" : host;
    if (uv_ip4_addr(bindHost.c_str(), p, &addr) != 0) {
        throw Thrown::make("std/network: listen host must be an IPv4 address: " + host);
    }
    const int threadCount = validatedTcpThreads(impl_->options.threads);
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) throw Thrown::make("std/network: could not create the libuv loop");

    impl_->listening = true;
    impl_->boundHost = bindHost;
    retainNetwork();
    impl_->retained = true;

    // One listener per `threads` slot, all on this loop (SOME_REUSEPORT).
    // Windows / no REUSEPORT: only the first listener is kept (see
    // runtime_tcp_reuseport.cpp); the rest are skipped, not failed.
    bool boundAny = false;
    // Reserve first: a uv_tcp_t must never move after uv_tcp_init (the
    // handle's address is its identity to libuv), so the vector must not
    // reallocate while listeners are live.
    impl_->listeners.reserve(static_cast<std::size_t>(threadCount));
    for (int i = 0; i < threadCount; ++i) {
        auto& listener = impl_->listeners.emplace_back();
        uv_tcp_init(loop, &listener.tcp);
        if (i > 0) {
            if (!applyReusePort(&listener.tcp)) {
                // No REUSEPORT (Windows): skip this slot. The handle stays
                // in the vector (never moved, never used) until close().
                listener.ready = false;
                continue;
            }
        }
        // Even the first listener gets SO_REUSEPORT when threads > 1, so
        // every listener can coexist in the kernel's balance set.
        if (i == 0 && threadCount > 1) (void)applyReusePort(&listener.tcp);
        listener.tcp.data = nullptr;  // replaced below with the server holder
        listener.ready = true;
        // Attach the server to the listener: uv_listen's callback needs it.
        // The holder is leaked with the listener storage (process lifetime
        // of the server, like the uWS App in runtime_server.cpp) - close()
        // deletes it.
        listener.tcp.data = new std::shared_ptr<TcpServerImpl>(impl_);
        int rc = uv_tcp_bind(&listener.tcp,
                             reinterpret_cast<const struct sockaddr*>(&addr), 0);
        if (rc != 0) {
            // First listener failing is fatal; a later REUSEPORT listener
            // failing is skipped (the port is already bound by slot 0).
            if (!boundAny) {
                impl_->listening = false;
                listener.ready = false;
                delete static_cast<std::shared_ptr<TcpServerImpl>*>(listener.tcp.data);
                listener.tcp.data = nullptr;
                if (impl_->retained) { impl_->retained = false; releaseNetwork(); }
                throw Thrown::make(std::string("std/network: could not listen: ") +
                                   uv_strerror(rc));
            }
            listener.ready = false;
            delete static_cast<std::shared_ptr<TcpServerImpl>*>(listener.tcp.data);
            listener.tcp.data = nullptr;
            continue;
        }
        rc = uv_listen(reinterpret_cast<uv_stream_t*>(&listener.tcp), 128,
                       &detail::tcpServerAccept);
        if (rc != 0) {
            if (!boundAny) {
                impl_->listening = false;
                listener.ready = false;
                delete static_cast<std::shared_ptr<TcpServerImpl>*>(listener.tcp.data);
                listener.tcp.data = nullptr;
                if (impl_->retained) { impl_->retained = false; releaseNetwork(); }
                throw Thrown::make(std::string("std/network: could not listen: ") +
                                   uv_strerror(rc));
            }
            listener.ready = false;
            delete static_cast<std::shared_ptr<TcpServerImpl>*>(listener.tcp.data);
            listener.tcp.data = nullptr;
            continue;
        }
        boundAny = true;
        if (impl_->boundPort == 0.0) {
            // Resolve an ephemeral port from slot 0's sockaddr.
            struct sockaddr_storage storage{};
            int len = static_cast<int>(sizeof(storage));
            if (uv_tcp_getsockname(&listener.tcp, reinterpret_cast<struct sockaddr*>(&storage),
                                   &len) == 0 &&
                storage.ss_family == AF_INET) {
                impl_->boundPort = static_cast<double>(
                    ntohs(reinterpret_cast<struct sockaddr_in*>(&storage)->sin_port));
            }
        }
    }
    if (!boundAny) {
        impl_->listening = false;
        if (impl_->retained) { impl_->retained = false; releaseNetwork(); }
        throw Thrown::make("std/network: could not listen (port already in use?)");
    }
    if (impl_->boundPort == 0.0) impl_->boundPort = static_cast<double>(p);
    if (impl_->listening0) impl_->listening0();
    if (callback) callback();
}

void TcpServer::close() const {
    if (!impl_ || impl_->closed) return;
    impl_->closed = true;
    impl_->listening = false;
    // Close every live connection first (handles erase themselves through
    // tcpServerForget when their close callbacks run; copy the set because
    // finishSocket mutates it on the same loop).
    const std::unordered_set<detail::TcpSocketImpl*> live = impl_->live;
    for (auto* raw : live) {
        // The impl is reachable through the stored handle.
        const auto it = impl_->handles.find(raw);
        if (it != impl_->handles.end() && it->second != nullptr) it->second->close();
    }
    for (auto& listener : impl_->listeners) {
        if (!listener.ready) continue;
        listener.ready = false;
        uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&listener.tcp);
        if (!uv_is_closing(handle)) {
            uv_close(handle, [](uv_handle_t* h) {
                auto* holder = static_cast<std::shared_ptr<detail::TcpServerImpl>*>(h->data);
                h->data = nullptr;
                delete holder;
            });
        }
    }
    // The listeners vector is deliberately not cleared: uv_close is
    // asynchronous and the handles must not move or die before their close
    // callbacks run (the server holder in each handle's data keeps the
    // impl, and therefore the vector, alive until then).
    if (impl_->retained) {
        impl_->retained = false;
        releaseNetwork();
    }
    if (impl_->close0) impl_->close0();
}

bool TcpServer::listening() const { return impl_ != nullptr && impl_->listening; }
double TcpServer::addressPort() const { return impl_ ? impl_->boundPort : 0.0; }
std::size_t TcpServer::connectionCount() const { return impl_ ? impl_->live.size() : 0; }

TcpServer createTcpServer(const TcpServerOptions& options) { return TcpServer(options); }

}  // namespace cp3::runtime::network::tcp
