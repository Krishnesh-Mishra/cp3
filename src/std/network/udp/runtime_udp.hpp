// =============================================================================
// src/std/network/udp/runtime_udp.hpp - PRIVATE std/network UDP surfaces.
//
// OWNERSHIP: the std/network family (src/std/network/udp/**). Shared by
// runtime_udp.cpp only; never installed and never part of the generated-code
// contract. The public surface is include/cp3/std/network/udp.hpp.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <uv.h>

#include "cp3/std/network/udp.hpp"

namespace cp3::runtime::network::detail {

/// The listener shapes a documented UDP event can have. A handler may
/// declare fewer parameters and ignore the payload, which lands in `none`.
///   "message"   -> (Buffer, UdpRemoteInfo)
///   "error"     -> (Thrown)
///   "close" / "listening" -> ()
struct UdpSlots {
    std::function<void()> none;
    std::function<void(const buffer::Buffer&, const UdpRemoteInfo&)> message;
    std::function<void(const Thrown&)> thr;
};

/// The state behind one UdpSocket. `udp` is embedded (not heap-allocated
/// separately) so the libuv handle and its state share one allocation.
struct UdpImpl : std::enable_shared_from_this<UdpImpl> {
    uv_udp_t udp{};
    bool init = false;     // uv_udp_init succeeded
    bool bound = false;    // uv_udp_bind succeeded
    bool reading = false;  // uv_udp_recv_start is active
    bool closed = false;   // close() called (close event emitted once)
    bool retained = false; // retainNetwork() balance flag
    int family = AF_INET;  // AF_INET (udp4) | AF_INET6 (udp6)
    std::string type;      // "udp4" | "udp6"
    std::map<std::string, UdpSlots> slots;

    void emitMessage(const buffer::Buffer& msg, const UdpRemoteInfo& from);
    void emitError(const std::string& message);
    void emitClose();
    void emitListening();
};

/// Size of the single per-thread read buffer (see udp.hpp). 64 KiB covers
/// any datagram that fits in one IPv4/IPv6 packet on common MTUs and the
/// maximum jumbo / localhost datagram; larger datagrams truncate, they do
/// not reallocate.
inline constexpr std::size_t kUdpReadBufferSize = 65536;

/// Validates a `double` port number (0..65535, integral) and returns it as
/// an int; throws a `Thrown` otherwise. Shared by bind() and send().
int udpPortChecked(double port, const char* what);

/// Parses a numeric IP literal into `out` for `family`; returns an empty
/// string on success, else the reason to throw. Hostnames are rejected.
std::string udpParseAddress(int family, const std::string& address, int port,
                            sockaddr_storage& out);

/// Formats a sockaddr_storage as UdpRemoteInfo text form (uv_ip*_name).
/// Returns false when the family is neither IPv4 nor IPv6.
bool udpRemoteInfo(const sockaddr_storage& addr, UdpRemoteInfo& out);

}  // namespace cp3::runtime::network::detail
