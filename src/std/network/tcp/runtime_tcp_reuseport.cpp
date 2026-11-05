// =============================================================================
// src/std/network/tcp/runtime_tcp_reuseport.cpp - SOME_REUSEPORT listeners.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). Same selection
// rule as runtime_tcp_socket.cpp.
//
// `createTcpServer({ threads: n })` does NOT create n OS threads and never a
// thread per connection. It creates n uv_tcp_t listener handles on the SAME
// shared libuv loop, each bound with SO_REUSEPORT (POSIX); the kernel
// balances accepts across them and every accepted socket then lives on that
// one loop, so a connection never changes loops and handlers never run
// concurrently. On Windows (no SO_REUSEPORT in this model) the caller
// collapses to a single SO_REUSEADDR listener - applyReusePort reports
// false there and `threads > 1` is accepted but has no extra effect.
// =============================================================================
#include "runtime_tcp.hpp"

#include <cmath>

#include <uv.h>

namespace cp3::runtime::network::tcp::detail {

int validatedTcpThreads(double threads) {
    if (!std::isfinite(threads) || threads < 1.0 || threads != std::floor(threads) ||
        threads > 64.0) {
        return -1;
    }
    return static_cast<int>(threads);
}

bool applyReusePort(uv_tcp_t* tcp) {
#if defined(_WIN32)
    (void)tcp;
    return false;  // No SO_REUSEPORT listener fan-out on Windows.
#else
    // SO_REUSEPORT: 15 on Linux, 0x200 on macOS/BSD. libuv does not export
    // the constant, so spell the platform value (uv_os_setsockopt needs the
    // raw level/optname pair before bind).
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    constexpr int kReusePort = 0x200;
#else
    constexpr int kReusePort = 15;
#endif
    const int one = 1;
    const int rc = uv_os_setsockopt(reinterpret_cast<uv_handle_t*>(tcp), SOL_SOCKET,
                                    kReusePort, reinterpret_cast<const char*>(&one),
                                    sizeof(one));
    return rc == 0;
#endif
}

}  // namespace cp3::runtime::network::tcp::detail
