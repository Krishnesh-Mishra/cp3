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
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "App.h"
#include "Loop.h"

#include "../runtime/runtime_loop.hpp"
#include "runtime_ws.hpp"

namespace cp3::runtime::network {

// =============================================================================
// WsImpl event emission (shared by both directions)
// =============================================================================
namespace detail {

/// Per-connection user data for the uWS websocket context. uWS requires the
/// type to fit its socket extension; the actual handle state lives in the
/// `WsImpl` map below, keyed by the native pointer.
struct WsUserData {
    std::uint32_t reserved = 0;
};

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
    bool isTls = false;
    const std::string error = detail::parseWsUrl(url, host, port, target, isTls);
    if (!error.empty()) {
        throw Thrown::make(error);
    }
    ensureLoop();
    detail::wsClientStart(impl_, host, port, target, isTls);
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
        detail::serverSend(*impl_, data, uWS::OpCode::TEXT);
        return;
    }
    detail::wsClientSend(impl_, data);
}

void WebSocket::send(const buffer::Buffer& data) const {
    if (!impl_ || impl_->closed) return;
    if (impl_->kind == detail::WsImpl::Kind::Server) {
        detail::serverSend(*impl_, data.bytes(), uWS::OpCode::BINARY);
        return;
    }
    detail::wsClientSendBinary(impl_, data.bytes());
}

void WebSocket::close(double code, const std::string& reason) const {
    if (!impl_ || impl_->closed) return;
    if (impl_->kind == detail::WsImpl::Kind::Server) {
        detail::serverEnd(*impl_, static_cast<int>(code), reason);
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
    uWS::App* app = nullptr;       // plain flavor
    uWS::SSLApp* sslApp = nullptr; // TLS flavor (only when tls options were passed)
    bool ssl = false;
    bool listening = false;
    bool closed = false;
    std::function<void(WebSocket&)> connection0;              // (ws)
    std::function<void(WebSocket&, HttpRequest)> connection1; // (ws, req)
    std::map<void*, std::unique_ptr<WebSocket>> connections;
    // TLS material (outlives uWS context creation; the uWS app is leaked).
    std::string sslCertFile;
    std::string sslKeyFile;
    std::string sslCaFile;
    std::string sslPassphrase;
};

}  // namespace detail

namespace detail {

template <bool Ssl>
using NativeServerWs = uWS::WebSocket<Ssl, true, WsUserData>;

void serverSend(detail::WsImpl& impl, const std::string& data, uWS::OpCode op) {
    if (impl.native == nullptr) return;
    if (impl.ssl) {
        static_cast<uWS::WebSocket<true, true, WsUserData>*>(impl.native)->send(data, op);
    } else {
        static_cast<uWS::WebSocket<false, true, WsUserData>*>(impl.native)->send(data, op);
    }
}

void serverEnd(detail::WsImpl& impl, int code, const std::string& reason) {
    if (impl.native == nullptr) return;
    if (impl.ssl) {
        static_cast<uWS::WebSocket<true, true, WsUserData>*>(impl.native)->end(code, reason);
    } else {
        static_cast<uWS::WebSocket<false, true, WsUserData>*>(impl.native)->end(code, reason);
    }
}

void onServerOpenImpl(const std::shared_ptr<WssImpl>& impl, bool ssl, void* native) {
    const WebSocket handle = WebSocket::adopt(native);
    if (const std::shared_ptr<detail::WsImpl> adopted = WebSocket::sharedImpl(handle)) {
        adopted->ssl = ssl;
    }
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
        if (ssl) {
            static_cast<uWS::WebSocket<true, true, WsUserData>*>(native)->end(1011, "handler error");
        } else {
            static_cast<uWS::WebSocket<false, true, WsUserData>*>(native)->end(1011, "handler error");
        }
    }
}

void onServerMessage(const std::shared_ptr<WssImpl>& impl, void* native,
                     std::string_view message, uWS::OpCode opCode) {
    if (opCode != uWS::OpCode::TEXT && opCode != uWS::OpCode::BINARY) return;
    const auto it = impl->connections.find(native);
    if (it == impl->connections.end() || it->second == nullptr) return;
    const std::shared_ptr<detail::WsImpl> connection = WebSocket::sharedImpl(*it->second);
    if (connection != nullptr) {
        connection->emitMessage(std::string(message), opCode == uWS::OpCode::BINARY);
    }
}

void onServerClose(const std::shared_ptr<WssImpl>& impl, void* native, int code,
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

/// uWS takes FILE paths for TLS, never inline PEM: validate the pair exists
/// and carries PEM markers (same rule as the HTTP server path).
void readTlsPemFile(const std::string& path, const char* field, const char* marker) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Thrown::make(std::string("[ERR_TLS_INVALID_OPTIONS] tls.") + field +
                           " file not found or unreadable: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();
    if (content.find(marker) == std::string::npos) {
        throw Thrown::make(std::string("[ERR_TLS_INVALID_OPTIONS] tls.") + field +
                           " file does not contain '" + marker + "': " + path);
    }
}

/// One WebSocketServer app (plain App or TLS SSLApp) with the full RFC 6455
/// behavior registered; the flavor is a template parameter so the handlers
/// see the concrete uWS::WebSocket<false|true, ...> type.
template <bool Ssl>
void startWss(const std::shared_ptr<WssImpl>& impl, const std::string& path, double port) {
    using A = uWS::TemplatedApp<Ssl>;
    uWS::SocketContextOptions options{};
    if constexpr (Ssl) {
        options.key_file_name = impl->sslKeyFile.c_str();
        options.cert_file_name = impl->sslCertFile.c_str();
        if (!impl->sslPassphrase.empty()) options.passphrase = impl->sslPassphrase.c_str();
    }
    A* app = new A(options);  // leaked on purpose (process lifetime)
    if constexpr (Ssl) {
        if (app->constructorFailed()) {
            throw Thrown::make(
                "[ERR_TLS_INVALID_OPTIONS] could not create the TLS context (check tls.cert / tls.key)");
        }
    }
    typename A::template WebSocketBehavior<WsUserData> behavior;
    behavior.compression = uWS::DISABLED;
    behavior.open = [impl](uWS::WebSocket<Ssl, true, WsUserData>* native) {
        onServerOpenImpl(impl, Ssl, native);
    };
    behavior.message = [impl](uWS::WebSocket<Ssl, true, WsUserData>* native, std::string_view message,
                              uWS::OpCode opCode) {
        onServerMessage(impl, native, message, opCode);
    };
    behavior.close = [impl](uWS::WebSocket<Ssl, true, WsUserData>* native, int code,
                            std::string_view message) {
        onServerClose(impl, native, code, message);
    };
    app->template ws<WsUserData>(path, std::move(behavior));
    app->listen(static_cast<int>(port), [](us_listen_socket_t* socket) {
        if (socket == nullptr) {
            throw Thrown::make("std/network: could not listen (port already in use?)");
        }
    });
    if constexpr (Ssl) impl->sslApp = app;
    else impl->app = app;
    impl->listening = true;
    retainNetwork();
}

}  // namespace detail

WebSocketServer::WebSocketServer(double port, std::string path, tls::TlsOptions tlsOptions)
    : impl_(std::make_shared<detail::WssImpl>()) {
    ensureLoop();
    if (loopHandle() == nullptr) return;
    if (!tlsOptions.cert.empty() || !tlsOptions.key.empty()) {
        // TLS requested: validate, then serve TLS only (fail closed).
        if (tlsOptions.cert.empty() || tlsOptions.key.empty()) {
            throw Thrown::make(
                "[ERR_TLS_INVALID_OPTIONS] WebSocketServer({tls}) requires tls.cert and tls.key (PEM file paths)");
        }
        detail::readTlsPemFile(tlsOptions.cert, "cert", "-----BEGIN CERTIFICATE-----");
        detail::readTlsPemFile(tlsOptions.key, "key", "PRIVATE KEY-----");
        impl_->ssl = true;
        impl_->sslCertFile = tlsOptions.cert;
        impl_->sslKeyFile = tlsOptions.key;
        impl_->sslPassphrase = tlsOptions.keyPassphrase;
        detail::startWss<true>(impl_, path, port);
        return;
    }
    detail::startWss<false>(impl_, path, port);
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
    if (!impl_ || impl_->closed) return;
    impl_->closed = true;
    if (impl_->ssl && impl_->sslApp != nullptr) {
        impl_->sslApp->close();
    } else if (impl_->app != nullptr) {
        impl_->app->close();
    } else {
        return;
    }
    impl_->connections.clear();
    if (impl_->listening) {
        impl_->listening = false;
        releaseNetwork();
    }
}

}  // namespace cp3::runtime::network
