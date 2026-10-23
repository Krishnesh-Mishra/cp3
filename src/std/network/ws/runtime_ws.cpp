// =============================================================================
// src/std/network/runtime_ws.cpp - `std/network` WebSocket API surface.
//
// OWNERSHIP: the std/network family (src/std/network/**). Same selection rule as
// runtime.cpp (the std/network native package row, D68).
//
// This TU defines the public WebSocket class (event slots + send/close) and
// WebSocketServer (one uWS App with a single `ws<WsUserData>` route at the
// configured path). Accepted connections become ordinary WebSocket handles:
// their native pointer is stored in the shared impl, and live connections are
// tracked per server so close events can retire them. The client transport
// (TCP, framing, handshake) is runtime_ws_client.cpp.
//
// Missing on purpose (wave report): the `req` argument of the connection
// listener is empty (uWS hands the request to the upgrade callback, not to
// `open`), and `upgrade`-time customization (headers, protocols) is not
// exposed.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "App.h"
#include "Loop.h"

#include "../runtime/runtime_loop.hpp"
#include "runtime_ws.hpp"

namespace cp3::runtime::network {
namespace {

/// Per-connection user data for the uWS websocket context. uWS requires the
/// type to fit its socket extension; the actual handle state lives in the
/// `WsImpl` map below, keyed by the native pointer.
struct WsUserData {
    std::uint32_t reserved = 0;
};

using NativeWs = uWS::WebSocket<false, true, WsUserData>;

}  // namespace

// =============================================================================
// WsImpl event emission (shared by both directions)
// =============================================================================
namespace detail {

void WsImpl::emitOpen() {
    if (closed) return;
    opened = true;
    const auto it = slots.find("open");
    if (it != slots.end() && it->second.none) it->second.none();
}

void WsImpl::emitMessage(const std::string& data, bool isBinary) {
    if (closed) return;
    const auto it = slots.find("message");
    if (it == slots.end()) return;
    // The documented payload is `string | Buffer`: the frame kind picks the
    // matching slot when the handler registered one, otherwise the payload
    // converts so a single handler still sees every frame (the pre-buffer
    // behavior for text handlers).
    if (isBinary) {
        if (it->second.binary) {
            it->second.binary(buffer::from(data));
        } else if (it->second.text) {
            it->second.text(data);
        } else if (it->second.none) {
            it->second.none();
        }
        return;
    }
    if (it->second.text) {
        it->second.text(data);
    } else if (it->second.binary) {
        it->second.binary(buffer::from(data));
    } else if (it->second.none) {
        it->second.none();
    }
}

void WsImpl::emitClose(double code, const std::string& reason) {
    if (closed) return;
    closed = true;
    const auto it = slots.find("close");
    if (it == slots.end()) return;
    if (it->second.pair) {
        it->second.pair(code, reason);
    } else if (it->second.none) {
        it->second.none();
    }
}

void WsImpl::emitError(const std::string& message) {
    if (closed) return;
    const auto it = slots.find("error");
    if (it == slots.end()) return;
    if (it->second.thr) {
        it->second.thr(Thrown::make(message));
    } else if (it->second.none) {
        it->second.none();
    }
}

}  // namespace detail

// =============================================================================
// WebSocket
// =============================================================================
WebSocket::WebSocket(const std::string& url) : impl_(std::make_shared<detail::WsImpl>()) {
    impl_->kind = detail::WsImpl::Kind::Client;
    std::string host;
    int port = 80;
    std::string target;
    const std::string error = detail::parseWsUrl(url, host, port, target);
    if (!error.empty()) {
        throw Thrown::make(error);
    }
    ensureLoop();
    detail::wsClientStart(impl_, host, port, target);
}

WebSocket WebSocket::adopt(void* native) {
    WebSocket socket;
    socket.impl_ = std::make_shared<detail::WsImpl>();
    socket.impl_->kind = detail::WsImpl::Kind::Server;
    socket.impl_->opened = true;
    socket.impl_->native = native;
    return socket;
}

void WebSocket::on(const std::string& event, std::function<void()> handler) const {
    if (impl_) impl_->slots[event].none = std::move(handler);
}

void WebSocket::on(const std::string& event, std::function<void(std::string)> handler) const {
    if (impl_) impl_->slots[event].text = std::move(handler);
}

void WebSocket::on(const std::string& event,
                   std::function<void(const buffer::Buffer&)> handler) const {
    if (impl_) impl_->slots[event].binary = std::move(handler);
}

void WebSocket::on(const std::string& event,
                   std::function<void(double, std::string)> handler) const {
    if (impl_) impl_->slots[event].pair = std::move(handler);
}

void WebSocket::on(const std::string& event, std::function<void(Thrown)> handler) const {
    if (impl_) impl_->slots[event].thr = std::move(handler);
}

void WebSocket::send(const std::string& data) const {
    if (!impl_ || impl_->closed) return;
    if (impl_->kind == detail::WsImpl::Kind::Server) {
        if (impl_->native != nullptr) {
            static_cast<NativeWs*>(impl_->native)->send(data, uWS::OpCode::TEXT);
        }
        return;
    }
    detail::wsClientSend(impl_, data);
}

void WebSocket::send(const buffer::Buffer& data) const {
    if (!impl_ || impl_->closed) return;
    if (impl_->kind == detail::WsImpl::Kind::Server) {
        if (impl_->native != nullptr) {
            static_cast<NativeWs*>(impl_->native)->send(data.bytes(), uWS::OpCode::BINARY);
        }
        return;
    }
    detail::wsClientSendBinary(impl_, data.bytes());
}

void WebSocket::close(double code, const std::string& reason) const {
    if (!impl_ || impl_->closed) return;
    if (impl_->kind == detail::WsImpl::Kind::Server) {
        if (impl_->native != nullptr) {
            static_cast<NativeWs*>(impl_->native)->end(static_cast<int>(code), reason);
        }
        return;
    }
    detail::wsClientClose(impl_, code, reason);
}

// =============================================================================
// WebSocketServer
// =============================================================================
namespace detail {

/// State behind one `new WebSocketServer({port, path})`. The uWS App is leaked
/// on purpose (process lifetime); live connections are tracked so the close
/// callback can retire the right handle.
struct WssImpl {
    uWS::App* app = nullptr;
    bool listening = false;
    bool closed = false;
    std::function<void(WebSocket&)> connection0;              // (ws)
    std::function<void(WebSocket&, HttpRequest)> connection1; // (ws, req)
    std::map<void*, std::unique_ptr<WebSocket>> connections;
};

}  // namespace detail

namespace {

using WssImpl = detail::WssImpl;

void onServerOpen(const std::shared_ptr<WssImpl>& impl, NativeWs* native) {
    const WebSocket handle = WebSocket::adopt(native);
    if (WebSocket::sharedImpl(handle) == nullptr) return;
    // The stored handle is what the connection listener receives: a `[&]`
    // capture of it in a nested handler stays valid for the life of the
    // connection (std::map nodes are stable), which is what generated closures
    // rely on. The listener runs with the entry already in place.
    WebSocket& stored = *impl->connections.emplace(native, std::make_unique<WebSocket>(handle))
                             .first->second;
    try {
        if (impl->connection1) {
            impl->connection1(stored, HttpRequest());
        } else if (impl->connection0) {
            impl->connection0(stored);
        }
    } catch (...) {
        // A connection handler threw: close just this socket.
        native->end(1011, "handler error");
    }
}

void onServerMessage(const std::shared_ptr<WssImpl>& impl, NativeWs* native,
                     std::string_view message, uWS::OpCode opCode) {
    if (opCode != uWS::OpCode::TEXT && opCode != uWS::OpCode::BINARY) return;
    const auto it = impl->connections.find(native);
    if (it == impl->connections.end() || it->second == nullptr) return;
    const std::shared_ptr<detail::WsImpl> connection = WebSocket::sharedImpl(*it->second);
    if (connection != nullptr) {
        connection->emitMessage(std::string(message), opCode == uWS::OpCode::BINARY);
    }
}

void onServerClose(const std::shared_ptr<WssImpl>& impl, NativeWs* native, int code,
                   std::string_view message) {
    const auto it = impl->connections.find(native);
    if (it == impl->connections.end() || it->second == nullptr) return;
    // Emit while the stored handle is still alive (`emitClose` runs the close
    // listeners), then retire the entry that nested handlers referenced.
    const std::shared_ptr<detail::WsImpl> connection = WebSocket::sharedImpl(*it->second);
    if (connection != nullptr) {
        connection->native = nullptr;
        connection->emitClose(static_cast<double>(code), std::string(message));
    }
    impl->connections.erase(it);
}

}  // namespace

WebSocketServer::WebSocketServer(double port, std::string path)
    : impl_(std::make_shared<detail::WssImpl>()) {
    ensureLoop();
    if (loopHandle() == nullptr) return;
    impl_->app = new uWS::App();  // leaked on purpose (process lifetime)
    const std::shared_ptr<detail::WssImpl> impl = impl_;
    uWS::App::WebSocketBehavior<WsUserData> behavior;
    behavior.compression = uWS::DISABLED;
    behavior.open = [impl](NativeWs* native) { onServerOpen(impl, native); };
    behavior.message = [impl](NativeWs* native, std::string_view message, uWS::OpCode opCode) {
        onServerMessage(impl, native, message, opCode);
    };
    behavior.close = [impl](NativeWs* native, int code, std::string_view message) {
        onServerClose(impl, native, code, message);
    };
    impl_->app->ws<WsUserData>(path, std::move(behavior));
    impl_->app->listen(static_cast<int>(port), [](us_listen_socket_t* socket) {
        if (socket == nullptr) {
            throw Thrown::make("std/network: could not listen (port already in use?)");
        }
    });
    impl_->listening = true;
    retainNetwork();
}

void WebSocketServer::on(const std::string& event, std::function<void(WebSocket&)> handler) const {
    if (!impl_ || event != "connection") return;
    impl_->connection0 = std::move(handler);
}

void WebSocketServer::on(const std::string& event,
                         std::function<void(WebSocket&, HttpRequest)> handler) const {
    if (!impl_ || event != "connection") return;
    impl_->connection1 = std::move(handler);
}

void WebSocketServer::close() const {
    if (!impl_ || impl_->app == nullptr || impl_->closed) return;
    impl_->closed = true;
    impl_->app->close();
    impl_->connections.clear();
    if (impl_->listening) {
        impl_->listening = false;
        releaseNetwork();
    }
}

}  // namespace cp3::runtime::network
