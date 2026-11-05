// =============================================================================
// cp3/std/network/dns.hpp - `std/network` dns.lookup(hostname) backing surface.
//
// OWNERSHIP: the std/network/dns family (src/std/network/dns/**). The header
// is the generated-code contract, so keep it additive like every cp3/std
// header (D14).
//
// CODEGEN CONTRACT (ROADMAP §H, `std/network` row: "Thin wrapper over the
// libuv getaddrinfo already used by fetch"):
//   dns.lookup(hostname)   -> ::cp3::runtime::network::dns::lookup(hostname)
//   lookup(hostname)       -> ::cp3::runtime::network::dns::lookup(hostname)
//                             (named import: import { lookup } from
//                             "std/network")
//   network.lookup(host)   -> ::cp3::runtime::network::dns::lookup(host)
//                             (default import: import network from
//                             "std/network")
//
// The result is the FIRST address of the libuv resolution, in text form
// (IPv4 via uv_ip4_name, IPv6 via uv_ip6_name). Resolution runs on the
// shared libuv loop (cp3/async/uv_loop.hpp, reached through
// ../runtime/runtime_loop.hpp) - never a blocking getaddrinfo on the calling
// thread - and settles a Promise<std::string>. Every failure (no loop,
// resolution start failure, no result, unknown address family) rejects with
// a genuine ::cp3::Error object boxed in the Thrown, so `catch (e)` binds
// an Error with .message/.name/.stack as the v0.2 error model requires.
//
// This is the runtime `connectTcp` (and any future TCP client) resolves its
// host through; fetch keeps its own inline getaddrinfo callback
// (runtime_fetch_io.cpp) because it also needs the port and the addrinfo
// for the connect itself.
// =============================================================================
#pragma once

#include <string>

#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"

namespace cp3::runtime::network::dns {

// `dns.lookup(hostname)`: resolves with the first address (text form) of the
// libuv getaddrinfo result; rejects with an Error when the hostname cannot
// be resolved. AF_UNSPEC + SOCK_STREAM hints, exactly like the fetch DNS
// path (runtime_fetch_io.cpp) and the previous runtime_dns.cpp.
Promise<std::string> lookup(const std::string& host);

}  // namespace cp3::runtime::network::dns
