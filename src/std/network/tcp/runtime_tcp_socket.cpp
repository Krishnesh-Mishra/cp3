// =============================================================================
// src/std/network/tcp/runtime_tcp_socket.cpp - `std/network` TcpSocket.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). Same selection
// rule as runtime.cpp (the std/network native package row, D68): compiled +
// linked only for a program that imports "std/network" and uses the TCP
// surface (the TcpSocket/createTcpServer/connectTcp markers, native_tcp.hpp).
//
// One uv_tcp_t per connection on the shared per-thread libuv loop
// (runtime_loop.hpp). No thread per connection, no thread at all: reads,
// writes, the idle timer and the connect timer are all libuv callbacks on
// that loop. The write path is a single outbox buffer drained by one
// uv_write at a time (the WsClient pattern); back-pressure is accounted on
// the queued bytes, not on kernel buffers.
// =============================================================================
#include "cp3/std/network/tcp.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <uv.h>

#include "../runtime/runtime_loop.hpp"
#include "runtime_tcp.hpp"

namespace cp3::runtime::network::tcp::detail {
namespace {

// =============================================================================
// Handle attachment (the FetchOp/WsClient holder pattern)
// =============================================================================
void attachImpl(uv_handle_t* handle, const std::shared_ptr<TcpSocketImpl>& impl) {
    handle->data = new std::shared_ptr<TcpSocketImpl>(impl);
}

std::shared_ptr<TcpSocketImpl> implOf(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<TcpSocketImpl>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

void detachImpl(uv_handle_t* handle) {
    // Release the holder LAST: dropping it can free the impl (and this
    // handle) while libuv is still inside the close callback.
    auto* holder = static_cast<std::shared_ptr<TcpSocketImpl>*>(handle->data);
    handle->data = nullptr;
    delete holder;
}

void closeTcpHandle(const std::shared_ptr<TcpSocketImpl>& impl) {
    if (!impl->tcpReady) return;
    uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&impl->tcp);
    if (uv_is_closing(handle)) return;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&impl->tcp));
    uv_close(handle, [](uv_handle_t* closing) {
        const std::shared_ptr<TcpSocketImpl> impl = implOf(closing);
        detachImpl(closing);
        if (impl != nullptr) {
            impl->emitClose();
            tcpServerForget(impl.get());
        }
    });
}

std::string addressText(const struct sockaddr* addr, int& portOut) {
    char text[80]{};
    portOut = 0;
    if (addr == nullptr) return {};
    if (addr->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const struct sockaddr_in*>(addr);
        portOut = ntohs(in->sin_port);
        return uv_ip4_name(in, text, sizeof(text)) == 0 ? std::string(text) : std::string();
    }
    if (addr->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const struct sockaddr_in6*>(addr);
        portOut = ntohs(in6->sin6_port);
        return uv_ip6_name(in6, text, sizeof(text)) == 0 ? std::string(text) : std::string();
    }
    return {};
}

std::string joinAddress(const std::string& ip, int port) {
    if (ip.empty()) return {};
    return ip + ":" + std::to_string(port);
}

// =============================================================================
// libuv write / read / timer callbacks
// =============================================================================
struct WriteRequest {
    uv_write_t req{};
    std::string data;
    std::shared_ptr<TcpSocketImpl> impl;
};

void onWriteDone(uv_write_t* req, int status) {
    auto* write = reinterpret_cast<WriteRequest*>(req);
    const std::shared_ptr<TcpSocketImpl> impl = std::move(write->impl);
    // The bytes libuv just tried to send leave the pending count whether
    // they succeeded or not (on error the socket is retired below).
    impl->outboxOffset += write->data.size();
    if (impl->outboxOffset >= impl->outbox.size()) {
        impl->outbox.clear();
        impl->outboxOffset = 0;
    }
    delete write;
    if (impl->finished) return;
    impl->writeActive = false;
    if (status < 0) {
        failSocket(impl, "EPIPE", "could not write to the socket");
        return;
    }
    // Drain: owed when a write() returned false and the queue has fallen
    // below the low-water mark (half the high-water mark, Node's rule).
    if (impl->drainPending && impl->pendingBytes() < impl->lowWater()) {
        impl->drainPending = false;
        impl->emitNone("drain");
    }
    if (impl->ended && impl->pendingBytes() == 0) {
        shutdownAfterFlush(impl);
        return;
    }
    flushWrites(impl);
}

void onAlloc(uv_handle_t*, std::size_t suggested, uv_buf_t* buf) {
    const std::size_t size = suggested == 0 ? kReadBufferSize : suggested;
    buf->base = new char[size];
    buf->len = size;
}

void onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    const std::shared_ptr<TcpSocketImpl> impl = implOf(reinterpret_cast<uv_handle_t*>(stream));
    std::string chunk;
    if (impl != nullptr && nread > 0 && buf->base != nullptr) {
        chunk.assign(buf->base, static_cast<std::size_t>(nread));
    }
    if (buf->base != nullptr) delete[] buf->base;
    if (impl == nullptr || impl->finished || nread == 0) return;
    if (nread < 0) {
        if (nread == UV_EOF) {
            impl->emitEndThenFinish();
        } else {
            failSocket(impl, "ECONNRESET", "connection error while reading");
        }
        return;
    }
    armIdleTimer(impl);
    impl->emitData(chunk);
}

void onIdleTimeout(uv_timer_t* timer) {
    const std::shared_ptr<TcpSocketImpl> impl = implOf(reinterpret_cast<uv_handle_t*>(timer));
    if (impl == nullptr || impl->finished) return;
    failSocket(impl, "ETIMEDOUT",
               "idle timeout after " +
                   std::to_string(static_cast<long long>(impl->idleTimeoutMs)) + " ms");
}

void onConnectTimeout(uv_timer_t* timer) {
    const std::shared_ptr<TcpSocketImpl> impl = implOf(reinterpret_cast<uv_handle_t*>(timer));
    if (impl == nullptr || impl->finished || impl->connected) return;
    failSocket(impl, "ETIMEDOUT",
               "connect timeout after " +
                   std::to_string(static_cast<long long>(impl->connectTimeoutMs)) + " ms");
}

}  // namespace

// =============================================================================
// Validation / Error helpers
// =============================================================================
double validatedCount(double value, double maxValue) {
    if (!std::isfinite(value) || value < 0.0 || value != std::floor(value) ||
        value > maxValue) {
        return -1.0;
    }
    return value;
}

int validatedPort(double value) {
    if (!std::isfinite(value) || value < 0.0 || value > 65535.0 ||
        value != std::floor(value)) {
        return -1;
    }
    return static_cast<int>(value);
}

Thrown makeTcpError(const std::string& code, const std::string& message) {
    const std::string full = code.empty() ? message : code + ": " + message;
    return Thrown::makeError(makeRef<::cp3::Error>(full));
}

Thrown makeTcpError(const std::string& message) { return makeTcpError("", message); }

// =============================================================================
// Emit
// =============================================================================
void TcpSocketImpl::emitNone(const std::string& event) {
    if (closedEmitted) return;
    const auto it = slots.find(event);
    if (it != slots.end() && it->second.none) it->second.none();
}

void TcpSocketImpl::emitData(const std::string& data) {
    if (closedEmitted) return;
    const auto it = slots.find("data");
    if (it == slots.end()) return;
    // The documented payload is `string | Buffer`: the registered slot
    // decides, and a single handler still sees every chunk (the WS rule).
    if (it->second.binary) {
        it->second.binary(buffer::from(data));
    } else if (it->second.text) {
        it->second.text(data);
    } else if (it->second.none) {
        it->second.none();
    }
}

void TcpSocketImpl::emitError(const std::string& code, const std::string& message) {
    if (closedEmitted) return;
    const auto it = slots.find("error");
    if (it == slots.end()) return;
    if (it->second.thr) {
        it->second.thr(makeTcpError(code, message));
    } else if (it->second.none) {
        it->second.none();
    }
}

void TcpSocketImpl::emitError(const std::string& message) { emitError("", message); }

void TcpSocketImpl::emitEndThenFinish() {
    if (!eof) {
        eof = true;
        emitNone("end");
    }
    // A handler may have called close() during "end"; finish is idempotent.
    finishSocket(implOf(reinterpret_cast<uv_handle_t*>(&tcp)));
}

void TcpSocketImpl::emitClose() {
    if (closedEmitted) return;
    closedEmitted = true;
    const auto it = slots.find("close");
    if (it != slots.end() && it->second.none) it->second.none();
}

// =============================================================================
// Lifecycle
// =============================================================================
std::shared_ptr<TcpSocketImpl> makeSocketImpl(double maxWriteBuffer, double idleTimeoutMs) {
    auto impl = std::make_shared<TcpSocketImpl>();
    impl->maxWriteBuffer = maxWriteBuffer;
    impl->idleTimeoutMs = idleTimeoutMs;
    ensureLoop();
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        impl->emitError("ENOTCONN", "could not create the libuv loop");
        return nullptr;
    }
    retainNetwork();
    impl->retained = true;
    uv_tcp_init(loop, &impl->tcp);
    attachImpl(reinterpret_cast<uv_handle_t*>(&impl->tcp), impl);
    impl->tcpReady = true;
    if (idleTimeoutMs > 0.0) {
        uv_timer_init(loop, &impl->idleTimer);
        attachImpl(reinterpret_cast<uv_handle_t*>(&impl->idleTimer), impl);
        impl->idleTimerReady = true;
    }
    return impl;
}

void armIdleTimer(const std::shared_ptr<TcpSocketImpl>& impl) {
    if (!impl->idleTimerReady || impl->finished) return;
    uv_timer_start(&impl->idleTimer, onIdleTimeout,
                   static_cast<std::uint64_t>(impl->idleTimeoutMs), 0);
}

void stopIdleTimer(TcpSocketImpl& impl) {
    if (impl.idleTimerReady) uv_timer_stop(&impl.idleTimer);
}

void stopConnectTimer(TcpSocketImpl& impl) {
    if (impl.connectTimerReady) uv_timer_stop(&impl.connectTimer);
}

void refreshAddresses(TcpSocketImpl& impl) {
    struct sockaddr_storage storage{};
    int len = static_cast<int>(sizeof(storage));
    int remotePort = 0;
    int localPort = 0;
    if (uv_tcp_getpeername(&impl.tcp, reinterpret_cast<struct sockaddr*>(&storage), &len) == 0) {
        impl.remote = joinAddress(addressText(reinterpret_cast<struct sockaddr*>(&storage), remotePort), remotePort);
        impl.remotePort = static_cast<double>(remotePort);
    }
    len = static_cast<int>(sizeof(storage));
    if (uv_tcp_getsockname(&impl.tcp, reinterpret_cast<struct sockaddr*>(&storage), &len) == 0) {
        impl.local = joinAddress(addressText(reinterpret_cast<struct sockaddr*>(&storage), localPort), localPort);
        impl.localPort = static_cast<double>(localPort);
    }
}

void startReading(const std::shared_ptr<TcpSocketImpl>& impl) {
    if (impl->finished || impl->eof || impl->reading || impl->paused) return;
    if (uv_read_start(reinterpret_cast<uv_stream_t*>(&impl->tcp), onAlloc, onRead) == 0) {
        impl->reading = true;
    }
}

void flushWrites(const std::shared_ptr<TcpSocketImpl>& impl) {
    if (impl->finished || impl->writeActive || !impl->connected) return;
    if (impl->pendingBytes() == 0) return;
    auto* write = new WriteRequest;
    write->impl = impl;
    write->data = impl->outbox.substr(impl->outboxOffset);
    impl->writeActive = true;
    uv_buf_t buf = uv_buf_init(write->data.data(),
                               static_cast<unsigned int>(write->data.size()));
    if (uv_write(&write->req, reinterpret_cast<uv_stream_t*>(&impl->tcp), &buf, 1,
                 onWriteDone) != 0) {
        delete write;
        impl->writeActive = false;
        failSocket(impl, "EPIPE", "could not write to the socket");
    }
}

bool queueBytes(const std::shared_ptr<TcpSocketImpl>& impl, const std::string& data) {
    if (impl->finished || impl->ended) {
        impl->emitError("ERR_STREAM_WRITE_AFTER_END",
                        "write after end");
        return false;
    }
    impl->outbox += data;
    armIdleTimer(impl);
    flushWrites(impl);
    const std::size_t high = impl->highWater();
    if (high == 0) return true;  // unbounded
    if (impl->pendingBytes() >= high) {
        impl->drainPending = true;
        return false;
    }
    return true;
}

void shutdownAfterFlush(const std::shared_ptr<TcpSocketImpl>& impl) {
    if (impl->finished || impl->shutdownStarted || !impl->connected) return;
    impl->shutdownStarted = true;
    impl->writeEnded = true;
    // FIN is on the wire. The socket stays readable until the peer's EOF
    // (or close()). The request must stay alive until its callback, so it
    // is heap-allocated and freed there (the WsClient WriteRequest pattern).
    auto* req = new uv_shutdown_t{};
    uv_shutdown(reinterpret_cast<uv_stream_t*>(&impl->tcp), req,
                [](uv_shutdown_t* r, int /*status*/) { delete r; });
}

void finishSocket(const std::shared_ptr<TcpSocketImpl>& impl) {
    if (impl == nullptr || impl->finished) return;
    impl->finished = true;
    stopIdleTimer(*impl);
    stopConnectTimer(*impl);
    impl->outbox.clear();
    impl->outboxOffset = 0;
    // Release the retain before uv_close: the close callback only emits
    // "close" and forgets the server entry, it touches no loop state.
    if (impl->retained) {
        impl->retained = false;
        releaseNetwork();
    }
    closeTcpHandle(impl);
}

void failSocket(const std::shared_ptr<TcpSocketImpl>& impl, const std::string& code,
                const std::string& message) {
    if (impl == nullptr || impl->finished) return;
    impl->emitError(code, message);
    finishSocket(impl);
}

// =============================================================================
// TcpSocket public surface
// =============================================================================
TcpSocket::TcpSocket() : impl_(std::make_shared<detail::TcpSocketImpl>()) {}

TcpSocket::TcpSocket(std::string host, double port, double timeoutMs)
    : impl_(detail::makeSocketImpl(detail::kDefaultMaxWriteBuffer, 0.0)) {
    if (impl_ == nullptr) throw Thrown::make("std/network: could not create the libuv loop");
    const int p = detail::validatedPort(port);
    if (p <= 0) throw Thrown::make("std/network: connectTcp port must be an integer in 1..65535");
    if (host.empty()) throw Thrown::make("std/network: connectTcp host must not be empty");
    if (!(timeoutMs >= 0.0) || !std::isfinite(timeoutMs)) {
        throw Thrown::make("std/network: connectTcp timeout must be a non-negative number");
    }
    impl_->connectTimeoutMs = timeoutMs;
    detail::tcpClientStart(impl_, host, p, timeoutMs);
}

TcpSocket TcpSocket::adopt(const std::shared_ptr<detail::TcpSocketImpl>& impl) {
    TcpSocket socket;
    socket.impl_ = impl;
    return socket;
}

void TcpSocket::on(const std::string& event, std::function<void()> handler) const {
    if (impl_) impl_->slots[event].none = std::move(handler);
}

void TcpSocket::on(const std::string& event,
                   std::function<void(const std::string&)> handler) const {
    if (impl_) impl_->slots[event].text = std::move(handler);
}

void TcpSocket::on(const std::string& event,
                   std::function<void(const buffer::Buffer&)> handler) const {
    if (impl_) impl_->slots[event].binary = std::move(handler);
}

void TcpSocket::on(const std::string& event, std::function<void(Thrown)> handler) const {
    if (impl_) impl_->slots[event].thr = std::move(handler);
}

bool TcpSocket::write(const std::string& data) const {
    if (!impl_ || impl_->finished) return false;
    return detail::queueBytes(impl_, data);
}

bool TcpSocket::write(const buffer::Buffer& data) const { return write(data.bytes()); }

void TcpSocket::end() const {
    if (!impl_ || impl_->finished || impl_->ended) return;
    impl_->ended = true;
    if (impl_->connected && impl_->pendingBytes() == 0) {
        detail::shutdownAfterFlush(impl_);
    } else if (!impl_->connected) {
        // Client still connecting: onConnected checks `ended` and shuts
        // down after the queued bytes flush (see runtime_tcp_client.cpp).
    }
}

void TcpSocket::end(const std::string& data) const {
    if (!data.empty()) (void)write(data);
    end();
}

void TcpSocket::end(const buffer::Buffer& data) const { end(data.bytes()); }

void TcpSocket::close() const {
    if (!impl_ || impl_->finished) return;
    detail::finishSocket(impl_);
}

void TcpSocket::pause() const {
    if (!impl_ || impl_->finished || impl_->paused) return;
    impl_->paused = true;
    if (impl_->reading && impl_->tcpReady) {
        uv_read_pause(reinterpret_cast<uv_stream_t*>(&impl_->tcp));
    }
}

void TcpSocket::resume() const {
    if (!impl_ || impl_->finished || !impl_->paused) return;
    impl_->paused = false;
    detail::startReading(impl_);
}

bool TcpSocket::isPaused() const { return impl_ != nullptr && impl_->paused; }
bool TcpSocket::isClosed() const { return impl_ == nullptr || impl_->finished; }
bool TcpSocket::isConnected() const { return impl_ != nullptr && impl_->connected; }
std::size_t TcpSocket::bufferedBytes() const {
    return impl_ == nullptr ? 0 : impl_->pendingBytes();
}
std::string TcpSocket::remoteAddress() const { return impl_ ? impl_->remote : std::string(); }
std::string TcpSocket::localAddress() const { return impl_ ? impl_->local : std::string(); }
double TcpSocket::remotePort() const { return impl_ ? impl_->remotePort : 0.0; }
double TcpSocket::localPort() const { return impl_ ? impl_->localPort : 0.0; }

}  // namespace cp3::runtime::network::tcp::detail
