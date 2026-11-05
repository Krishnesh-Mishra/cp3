// =============================================================================
// src/std/network/udp/runtime_udp.cpp - `std/network` UDP API surface.
//
// OWNERSHIP: the std/network family (src/std/network/udp/**). Same selection
// rule as runtime.cpp (the std/network native package row, D68; see
// native_udp.inc). Backend: libuv v1.53.0 uv_udp_t, static (D70).
//
// THE READ PATH (no alloc per packet):
//   * `tlsReadBuffer` (one per thread, 64 KiB) is the ONLY recv buffer. The
//     alloc callback returns it verbatim on every recv - it never calls
//     new/malloc, whatever `suggested_size` libuv passes.
//   * `tlsRemoteAddr` (one sockaddr_storage per thread) is the ONLY recv
//     sockaddr; libuv fills it in place.
//   * The recv callback copies nread bytes into the owning Buffer handed to
//     the "message" listener (one copy - the datagram must survive the next
//     recv overwriting the shared buffer) and reuses the same buffer for
//     the next datagram. No uv_buf is allocated per packet, no handle
//     state is allocated per packet.
//   * Sends are the only per-call heap use: one SendRequest per send(),
//     owning the payload copy libuv must see until its callback runs.
//
// Lifetime: construction retains the runtime loop (retainNetwork); the
// libuv close callback releases it exactly once. The handle's `data`
// holder (a shared_ptr<UdpImpl>) keeps the impl alive across every
// callback, including while uv_close runs.
// =============================================================================
#include "cp3/std/network/udp.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "../runtime/runtime_loop.hpp"
#include "runtime_udp.hpp"

namespace cp3::runtime::network::detail {
namespace {

// The one read buffer per thread, and the one recv sockaddr per thread.
// thread_local: each libuv loop thread owns its own, so sockets on
// different threads never share (and never race on) a recv buffer.
thread_local std::vector<char> tlsReadBuffer(kUdpReadBufferSize);
thread_local sockaddr_storage tlsRemoteAddr{};

void attachUdp(uv_handle_t* handle, const std::shared_ptr<UdpImpl>& impl) {
    handle->data = new std::shared_ptr<UdpImpl>(impl);
}

std::shared_ptr<UdpImpl> udpOf(const uv_handle_t* handle) {
    auto* holder = static_cast<std::shared_ptr<UdpImpl>*>(handle->data);
    return holder == nullptr ? nullptr : *holder;
}

void detachUdp(uv_handle_t* handle) {
    // Release the holder LAST: dropping it can free the impl (and this
    // handle) while libuv is still inside the close callback.
    auto* holder = static_cast<std::shared_ptr<UdpImpl>*>(handle->data);
    handle->data = nullptr;
    delete holder;
}

/// The libuv alloc callback: hands out the thread's single read buffer.
/// No allocation happens here, ever - this is the "no alloc per packet"
/// guarantee. `suggested_size` is deliberately ignored: the buffer is
/// already at its fixed 64 KiB size.
void onUdpAlloc(uv_handle_t* /*handle*/, std::size_t /*suggested*/, uv_buf_t* buf) {
    buf->base = tlsReadBuffer.data();
    buf->len = tlsReadBuffer.size();
}

void onUdpClose(uv_handle_t* handle) {
    const std::shared_ptr<UdpImpl> impl = udpOf(handle);
    if (impl != nullptr) {
        impl->bound = false;
        impl->reading = false;
        impl->emitClose();
        if (impl->retained) {
            impl->retained = false;
            releaseNetwork();
        }
    }
    detachUdp(handle);
}

void onUdpRecv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* /*buf*/,
               const sockaddr* addr, unsigned int /*flags*/) {
    const std::shared_ptr<UdpImpl> impl = udpOf(reinterpret_cast<uv_handle_t*>(handle));
    if (impl == nullptr || impl->closed) return;
    if (nread < 0) {
        // Transient recv errors (e.g. UV_EAGAIN) are not datagrams; drop
        // them silently, everything else surfaces as an "error" event.
        if (nread == UV_EAGAIN || nread == UV_EWOULDBLOCK) return;
        impl->emitError(std::string("std/network: UDP receive error: ") + uv_strerror(static_cast<int>(nread)));
        return;
    }
    if (nread == 0 || addr == nullptr) return;  // no data / no sender
    // libuv filled the thread's single sockaddr_storage in place; format
    // it into the `from` value before the next recv can overwrite it.
    std::memcpy(&tlsRemoteAddr, addr, sizeof(tlsRemoteAddr));
    UdpRemoteInfo from;
    if (!udpRemoteInfo(tlsRemoteAddr, from)) return;
    // One copy out of the shared read buffer into the owning Buffer the
    // listener receives. The buffer itself is reused for the next recv.
    const buffer::Buffer msg(
        buffer::detail::BufferString(tlsReadBuffer.data(),
                                     tlsReadBuffer.data() + static_cast<std::size_t>(nread)));
    impl->emitMessage(msg, from);
}

struct SendRequest {
    uv_udp_send_t req{};
    std::string payload;  // owned copy; libuv reads it until onSent runs
    sockaddr_storage dest{};
    std::shared_ptr<UdpImpl> impl;
};

void onUdpSent(uv_udp_send_t* req, int status) {
    auto* send = reinterpret_cast<SendRequest*>(req);
    const std::shared_ptr<UdpImpl> impl = send->impl;
    delete send;
    if (impl == nullptr || impl->closed) return;
    if (status < 0) {
        impl->emitError(std::string("std/network: UDP send error: ") + uv_strerror(status));
    }
}

}  // namespace

// =============================================================================
// Checks / address helpers (runtime_udp.hpp)
// =============================================================================
int udpPortChecked(double port, const char* what) {
    if (!(port >= 0.0) || port > 65535.0 || port != std::floor(port)) {
        throw Thrown::make(std::string("std/network: UDP ") + what +
                           " port must be an integer between 0 and 65535");
    }
    return static_cast<int>(port);
}

std::string udpParseAddress(int family, const std::string& address, int port,
                            sockaddr_storage& out) {
    std::memset(&out, 0, sizeof(out));
    if (family == AF_INET6) {
        if (uv_ip6_addr(address.c_str(), port, reinterpret_cast<sockaddr_in6*>(&out)) != 0) {
            return "std/network: UDP address is not a valid IPv6 literal: " + address;
        }
        return {};
    }
    if (uv_ip4_addr(address.c_str(), port, reinterpret_cast<sockaddr_in*>(&out)) != 0) {
        return "std/network: UDP address is not a valid IPv4 literal: " + address;
    }
    return {};
}

bool udpRemoteInfo(const sockaddr_storage& addr, UdpRemoteInfo& out) {
    char text[INET6_ADDRSTRLEN] = {};
    if (addr.ss_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        if (uv_ip6_name(in6, text, sizeof(text)) != 0) return false;
        out.address = text;
        out.port = static_cast<std::uint16_t>(ntohs(in6->sin6_port));
        out.family = "IPv6";
        return true;
    }
    if (addr.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&addr);
        if (uv_ip4_name(in, text, sizeof(text)) != 0) return false;
        out.address = text;
        out.port = static_cast<std::uint16_t>(ntohs(in->sin_port));
        out.family = "IPv4";
        return true;
    }
    return false;
}

// =============================================================================
// UdpImpl event emission (same shape as WsImpl)
// =============================================================================
void UdpImpl::emitMessage(const buffer::Buffer& msg, const UdpRemoteInfo& from) {
    if (closed) return;
    const auto it = slots.find("message");
    if (it == slots.end()) return;
    if (it->second.message) {
        it->second.message(msg, from);
    } else if (it->second.none) {
        it->second.none();
    }
}

void UdpImpl::emitError(const std::string& message) {
    if (closed) return;
    const auto it = slots.find("error");
    if (it == slots.end()) return;
    if (it->second.thr) {
        it->second.thr(Thrown::make(message));
    } else if (it->second.none) {
        it->second.none();
    }
}

void UdpImpl::emitClose() {
    const auto it = slots.find("close");
    if (it == slots.end()) return;
    if (it->second.none) it->second.none();
}

void UdpImpl::emitListening() {
    if (closed) return;
    const auto it = slots.find("listening");
    if (it == slots.end()) return;
    if (it->second.none) it->second.none();
}

}  // namespace cp3::runtime::network::detail

namespace cp3::runtime::network {

UdpSocket::UdpSocket(std::string type) : impl_(std::make_shared<detail::UdpImpl>()) {
    if (type != "udp4" && type != "udp6") {
        throw Thrown::make("std/network: createUdpSocket type must be \"udp4\" or \"udp6\", got: " + type);
    }
    impl_->type = type;
    impl_->family = (type == "udp6") ? AF_INET6 : AF_INET;
    ensureLoop();
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        throw Thrown::make("std/network: could not create the libuv loop");
    }
    if (uv_udp_init(loop, &impl_->udp) != 0) {
        throw Thrown::make("std/network: could not create the UDP socket");
    }
    impl_->init = true;
    detail::attachUdp(reinterpret_cast<uv_handle_t*>(&impl_->udp), impl_);
    retainNetwork();
    impl_->retained = true;
}

UdpSocket createUdpSocket(const std::string& type) { return UdpSocket(type); }

void UdpSocket::on(const std::string& event,
                   std::function<void(const buffer::Buffer&, const UdpRemoteInfo&)> handler) const {
    if (impl_) impl_->slots[event].message = std::move(handler);
}

void UdpSocket::on(const std::string& event, std::function<void(Thrown)> handler) const {
    if (impl_) impl_->slots[event].thr = std::move(handler);
}

void UdpSocket::on(const std::string& event, std::function<void()> handler) const {
    if (impl_) impl_->slots[event].none = std::move(handler);
}

void UdpSocket::bind(double port) const {
    if (!impl_ || impl_->closed) throw Thrown::make("std/network: UDP socket is closed");
    if (impl_->bound) throw Thrown::make("std/network: UDP socket is already bound");
    const int checked = detail::udpPortChecked(port, "bind");
    const std::string wildcard = (impl_->family == AF_INET6) ? "::" : "0.0.0.0";
    sockaddr_storage addr{};
    const std::string error = detail::udpParseAddress(impl_->family, wildcard, checked, addr);
    if (!error.empty()) throw Thrown::make(error);
    const int rc = uv_udp_bind(&impl_->udp, reinterpret_cast<const sockaddr*>(&addr), 0);
    if (rc != 0) {
        throw Thrown::make(std::string("std/network: could not bind UDP port ") +
                           std::to_string(checked) + ": " + uv_strerror(rc));
    }
    impl_->bound = true;
    const int startRc = uv_udp_recv_start(&impl_->udp, detail::onUdpAlloc, detail::onUdpRecv);
    if (startRc != 0) {
        impl_->bound = false;
        throw Thrown::make(std::string("std/network: could not start UDP receive: ") + uv_strerror(startRc));
    }
    impl_->reading = true;
    impl_->emitListening();
}

void UdpSocket::bind(double port, const std::string& address) const {
    if (!impl_ || impl_->closed) throw Thrown::make("std/network: UDP socket is closed");
    if (impl_->bound) throw Thrown::make("std/network: UDP socket is already bound");
    const int checked = detail::udpPortChecked(port, "bind");
    sockaddr_storage addr{};
    const std::string error = detail::udpParseAddress(impl_->family, address, checked, addr);
    if (!error.empty()) throw Thrown::make(error);
    const int rc = uv_udp_bind(&impl_->udp, reinterpret_cast<const sockaddr*>(&addr), 0);
    if (rc != 0) {
        throw Thrown::make(std::string("std/network: could not bind UDP " + address + ":" +
                                       std::to_string(checked) + ": ") + uv_strerror(rc));
    }
    impl_->bound = true;
    const int startRc = uv_udp_recv_start(&impl_->udp, detail::onUdpAlloc, detail::onUdpRecv);
    if (startRc != 0) {
        impl_->bound = false;
        throw Thrown::make(std::string("std/network: could not start UDP receive: ") + uv_strerror(startRc));
    }
    impl_->reading = true;
    impl_->emitListening();
}

void UdpSocket::bind(double port, std::function<void()> callback) const {
    bind(port);
    if (callback) callback();
}

void UdpSocket::send(const buffer::Buffer& data, double port, const std::string& address) const {
    if (!impl_ || impl_->closed) throw Thrown::make("std/network: UDP socket is closed");
    const int checked = detail::udpPortChecked(port, "send");
    auto* request = new detail::SendRequest();
    request->impl = impl_;
    const std::string_view view = data.view();
    request->payload.assign(view.data(), view.size());
    const std::string error = detail::udpParseAddress(impl_->family, address, checked, request->dest);
    if (!error.empty()) {
        delete request;
        throw Thrown::make(error);
    }
    uv_buf_t buf = uv_buf_init(request->payload.data(),
                               static_cast<unsigned int>(request->payload.size()));
    const int rc = uv_udp_send(&request->req, &impl_->udp, &buf, 1,
                               reinterpret_cast<const sockaddr*>(&request->dest),
                               detail::onUdpSent);
    if (rc != 0) {
        delete request;
        throw Thrown::make(std::string("std/network: could not send UDP datagram: ") + uv_strerror(rc));
    }
}

void UdpSocket::send(const std::string& data, double port, const std::string& address) const {
    send(buffer::from(data), port, address);
}

void UdpSocket::close() const {
    if (!impl_ || !impl_->init || impl_->closed) return;
    impl_->closed = true;
    uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&impl_->udp);
    if (impl_->reading) {
        uv_udp_recv_stop(&impl_->udp);
        impl_->reading = false;
    }
    if (uv_is_closing(handle)) return;
    uv_close(handle, detail::onUdpClose);
}

bool UdpSocket::bound() const { return impl_ != nullptr && impl_->bound && !impl_->closed; }

UdpRemoteInfo UdpSocket::address() const {
    if (!impl_ || !impl_->bound) throw Thrown::make("std/network: UDP socket is not bound");
    sockaddr_storage local{};
    int len = static_cast<int>(sizeof(local));
    if (uv_udp_getsockname(&impl_->udp, reinterpret_cast<sockaddr*>(&local), &len) != 0) {
        throw Thrown::make("std/network: could not read the UDP socket address");
    }
    UdpRemoteInfo out;
    if (!detail::udpRemoteInfo(local, out)) {
        throw Thrown::make("std/network: could not read the UDP socket address");
    }
    return out;
}

}  // namespace cp3::runtime::network
