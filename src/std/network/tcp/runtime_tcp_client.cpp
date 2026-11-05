// =============================================================================
// src/std/network/tcp/runtime_tcp_client.cpp - `std/network` connectTcp.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). Same selection
// rule as runtime_tcp_socket.cpp.
//
// connectTcp returns the TcpSocket immediately (Node net.createConnection
// semantics); the TCP connect is asynchronous on the shared loop:
// uv_getaddrinfo (hostnames) or uv_ip4_addr (IPv4 literals, the fast path),
// then uv_tcp_connect, with a libuv timer for the default connect timeout.
// All failures - DNS, refused, timeout - arrive as an "error" event whose
// argument is a cp3 Error object, followed by "close" (failSocket).
// =============================================================================
#include "cp3/std/network/tcp.hpp"

#include <cmath>
#include <memory>
#include <string>

#include <uv.h>

#include "../runtime/runtime_loop.hpp"
#include "runtime_tcp.hpp"

namespace cp3::runtime::network::tcp::detail {
namespace {

void attachImpl(uv_handle_t* handle, const std::shared_ptr<TcpSocketImpl>& impl) {
    handle->data = new std::shared_ptr<TcpSocketImpl>(impl);
}

std::shared_ptr<TcpSocketImpl> implOf(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<TcpSocketImpl>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

void onConnectTimeout(uv_timer_t* timer) {
    const std::shared_ptr<TcpSocketImpl> impl = implOf(reinterpret_cast<uv_handle_t*>(timer));
    if (impl == nullptr || impl->finished || impl->connected) return;
    failSocket(impl, "ETIMEDOUT",
               "connect timeout after " +
                   std::to_string(static_cast<long long>(impl->connectTimeoutMs)) + " ms");
}

void onConnected(uv_connect_t* req, int status) {
    const std::shared_ptr<TcpSocketImpl> impl = implOf(reinterpret_cast<uv_handle_t*>(req->handle));
    if (impl == nullptr || impl->finished) return;
    if (status < 0) {
        // uv_strerror keeps the OS reason ("connection refused", ...).
        failSocket(impl, "ECONNREFUSED",
                   std::string("connect ") + uv_strerror(status));
        return;
    }
    impl->connected = true;
    stopConnectTimer(*impl);
    refreshAddresses(*impl);
    armIdleTimer(impl);
    impl->emitNone("connect");
    if (impl->finished) return;  // a "connect" handler may have closed it
    startReading(impl);
    flushWrites(impl);
    if (impl->ended && impl->pendingBytes() == 0) shutdownAfterFlush(impl);
}

void onResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    std::unique_ptr<std::shared_ptr<TcpSocketImpl>> holder(
        static_cast<std::shared_ptr<TcpSocketImpl>*>(req->data));
    const std::shared_ptr<TcpSocketImpl> impl = *holder;
    if (impl->finished) {
        if (res != nullptr) uv_freeaddrinfo(res);
        return;
    }
    if (status != 0 || res == nullptr) {
        if (res != nullptr) uv_freeaddrinfo(res);
        failSocket(impl, "ENOTFOUND", "DNS lookup failed");
        return;
    }
    const int rc = uv_tcp_connect(&impl->connect, &impl->tcp, res->ai_addr, onConnected);
    uv_freeaddrinfo(res);
    if (rc != 0) {
        failSocket(impl, "ECONNREFUSED", "could not start the connection");
    }
}

}  // namespace

void tcpClientStart(const std::shared_ptr<TcpSocketImpl>& impl, const std::string& host,
                    int port, double timeoutMs) {
    impl->connectTimeoutMs = timeoutMs;
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        failSocket(impl, "ENOTCONN", "could not create the libuv loop");
        return;
    }
    // Default connect timeout: a one-shot timer, stopped by onConnected.
    // timeoutMs == 0 disables it (fetch's "no timeout" spelling).
    if (timeoutMs > 0.0) {
        uv_timer_init(loop, &impl->connectTimer);
        attachImpl(reinterpret_cast<uv_handle_t*>(&impl->connectTimer), impl);
        impl->connectTimerReady = true;
        uv_timer_start(&impl->connectTimer, onConnectTimeout,
                       static_cast<std::uint64_t>(timeoutMs), 0);
    }
    struct sockaddr_in addr{};
    if (uv_ip4_addr(host.c_str(), port, &addr) == 0) {
        const int rc = uv_tcp_connect(&impl->connect, &impl->tcp,
                                      reinterpret_cast<const struct sockaddr*>(&addr),
                                      onConnected);
        if (rc != 0) {
            failSocket(impl, "ECONNREFUSED", "could not start the connection");
        }
        return;
    }
    // Hostname (IPv4 only, like fetch/WS): libuv resolution, never blocking.
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    impl->dns.data = new std::shared_ptr<TcpSocketImpl>(impl);
    const std::string portText = std::to_string(port);
    const int rc = uv_getaddrinfo(loop, &impl->dns, onResolved, host.c_str(),
                                  portText.c_str(), &hints);
    if (rc != 0) {
        delete static_cast<std::shared_ptr<TcpSocketImpl>*>(impl->dns.data);
        impl->dns.data = nullptr;
        failSocket(impl, "ENOTFOUND", "DNS lookup could not start");
    }
}

}  // namespace cp3::runtime::network::tcp::detail

namespace cp3::runtime::network::tcp {

TcpSocket connectTcp(const TcpConnectOptions& options) {
    if (options.host.empty()) {
        throw Thrown::make("std/network: connectTcp host must not be empty");
    }
    const int port = detail::validatedPort(options.port);
    if (port <= 0) {
        throw Thrown::make("std/network: connectTcp port must be an integer in 1..65535");
    }
    if (!(options.timeout >= 0.0) || !std::isfinite(options.timeout)) {
        throw Thrown::make("std/network: connectTcp timeout must be a non-negative number");
    }
    if (detail::validatedCount(options.maxWriteBuffer, 1099511627776.0) < 0.0) {
        throw Thrown::make("std/network: connectTcp maxWriteBuffer must be a non-negative integer");
    }
    if (detail::validatedCount(options.idleTimeout, 1099511627776.0) < 0.0) {
        throw Thrown::make("std/network: connectTcp idleTimeout must be a non-negative integer");
    }
    auto impl = detail::makeSocketImpl(options.maxWriteBuffer, options.idleTimeout);
    if (impl == nullptr) throw Thrown::make("std/network: could not create the libuv loop");
    impl->connectTimeoutMs = options.timeout;
    detail::tcpClientStart(impl, options.host, port, options.timeout);
    return TcpSocket::adopt(impl);
}

TcpSocket connectTcp(const std::string& host, double port, double timeoutMs) {
    TcpConnectOptions options;
    options.host = host;
    options.port = port;
    options.timeout = timeoutMs;
    return connectTcp(options);
}

}  // namespace cp3::runtime::network::tcp
