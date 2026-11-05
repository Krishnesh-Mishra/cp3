// =============================================================================
// cp3/std/network/udp.hpp - `std/network` UDP backing surface.
//
// OWNERSHIP: the std/network family (src/std/network/udp/**). Additive to the
// generated-code ABI (D14): this header is the codegen contract for the UDP
// sub-surface only; it does not modify cp3/std/network.hpp.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 §network, UDP)
// -----------------------------------------------------------------------------
//   createUdpSocket("udp4"|"udp6")  -> ::cp3::runtime::network::createUdpSocket(type)
//   socket.bind(port)               -> socket.bind(port)
//   socket.bind(port, address)      -> socket.bind(port, address)
//   socket.send(buf, port, address) -> socket.send(buf, port, address)
//                                      (buf: Buffer | string)
//   socket.on("message", (msg, from) => ...)
//                                   -> socket.on("message",
//                                        std::function<void(const Buffer&, UdpRemoteInfo)>)
//   socket.on("error", (err) => ...) -> socket.on("error", std::function<void(Thrown)>)
//   socket.on("close", () => ...)    -> socket.on("close", std::function<void()>)
//   socket.close()                  -> socket.close()
//   from.address / from.port / from.family -> UdpRemoteInfo members
//
// Named import: `import { createUdpSocket } from "std/network";`
// Backend: libuv uv_udp_t (third_party/libuv, D70). Implementation TU:
// src/std/network/udp/runtime_udp.cpp (selected via the std/network native
// package row; see native_udp.inc / WIRING.md).
//
// -----------------------------------------------------------------------------
// READ-BUFFER MODEL (the point of this sub-surface)
// -----------------------------------------------------------------------------
//   * ONE read buffer per thread: a `thread_local` 64 KiB buffer owned by
//     runtime_udp.cpp. The libuv alloc callback hands out that same buffer on
//     every recv; it never allocates. The remote sockaddr is likewise a
//     `thread_local` sockaddr_storage reused per recv.
//   * "No alloc per packet" covers the libuv read path only. A delivered
//     datagram must outlive the next recv (which overwrites the shared read
//     buffer), so the message handler receives an owning `Buffer` copy of the
//     nread bytes (one copy, one Buffer owner allocation). Nothing else on
//     the recv path allocates: no per-packet uv_buf, no per-packet handle.
//   * Datagrams larger than 64 KiB are truncated by the OS/libuv to the
//     buffer (standard UDP read-buffer semantics); they are not reassembled.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "cp3/core/error.hpp"
#include "cp3/std/buffer.hpp"

namespace cp3::runtime::network {

namespace detail {
struct UdpImpl;
}  // namespace detail

/// The `from` argument of a UDP "message" event (Node `RemoteInfo` shape):
/// { address: string, port: number, family: "IPv4" | "IPv6" }. `port` is held
/// at its semantic width (u16); a TypeScript-mode read widens it to `number`
/// at codegen, exactly like FetchResponse::status.
struct UdpRemoteInfo {
    std::string address;
    std::uint16_t port = 0;
    std::string family;  // "IPv4" | "IPv6"
};

/// One UDP socket (`createUdpSocket(type)`). Copyable handle: copies share
/// the same underlying uv_udp_t (same pattern as WebSocket). All methods run
/// on the runtime (libuv) thread; the socket retains the runtime loop from
/// construction until its close callback, so the program cannot exit while
/// it is open (the §14 pending-work rule).
class UdpSocket {
public:
    /// `createUdpSocket("udp4" | "udp6")` - anything else throws a `Thrown`.
    /// Prefer the free `createUdpSocket(type)` factory below; this
    /// constructor exists so codegen has a constructible type.
    explicit UdpSocket(std::string type);

    /// `socket.on(event, handler)`; one handler per event (a later
    /// registration replaces the earlier one), mirroring WebSocket:
    ///   "message" -> (msg: Buffer, from: UdpRemoteInfo) => void
    ///   "error"   -> (err: unknown) => void
    ///   "close"   -> () => void
    ///   "listening" -> () => void  (fired once bind() succeeds)
    void on(const std::string& event,
            std::function<void(const buffer::Buffer&, const UdpRemoteInfo&)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;
    void on(const std::string& event, std::function<void()> handler) const;

    /// `socket.bind(port)`: binds the wildcard address of the socket family
    /// ("0.0.0.0" for udp4, "::" for udp6). Port 0 requests an ephemeral
    /// port. Starts receiving; throws a `Thrown` on failure (bad port,
    /// address in use, already bound/closed).
    void bind(double port) const;
    /// `socket.bind(port, address)`: binds a specific numeric address
    /// (IP literal only - no hostnames; see limitations in WIRING.md).
    void bind(double port, const std::string& address) const;
    /// `socket.bind(port, callback)`: the callback form; the callback runs
    /// once the port is bound (and "listening" fires). Kept as a separate
    /// overload so the documented one-argument bind stays unambiguous.
    void bind(double port, std::function<void()> callback) const;

    /// `socket.send(data, port, address)`: sends one datagram to a numeric
    /// address (IP literal only). `data` is a Buffer (raw bytes) or a
    /// string (UTF-8 bytes). Fire-and-forget: a send failure is reported
    /// through the "error" event, not a throw, except for a bad port /
    /// unparseable address / closed socket, which throw synchronously.
    /// The socket need not be bound first (libuv binds an ephemeral port).
    void send(const buffer::Buffer& data, double port, const std::string& address) const;
    void send(const std::string& data, double port, const std::string& address) const;

    /// `socket.close()`: stops receiving and closes the handle. Idempotent;
    /// the "close" event fires once, from the libuv close callback.
    void close() const;

    /// Whether bind() has succeeded and the socket is still open.
    bool bound() const;
    /// The bound address (getsockname). Throws a `Thrown` when not bound.
    UdpRemoteInfo address() const;

private:
    std::shared_ptr<detail::UdpImpl> impl_;
};

/// `createUdpSocket("udp4" | "udp6")`.
UdpSocket createUdpSocket(const std::string& type);

}  // namespace cp3::runtime::network
