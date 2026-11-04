// =============================================================================
// src/std/os/runtime.cpp - `os` (std/os): libuv-backed probes.
//
// OWNERSHIP: the std/os family (src/std/os/**). Implements the libuv calls that
// cp3/std/os.hpp declares. Linked only for programs that import "std/os";
// libuv is shared with std/io and std/network through third_party/libuv.
// =============================================================================
#include "cp3/std/os.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <uv.h>

namespace cp3::runtime::os {

double totalMemory() { return static_cast<double>(uv_get_total_memory()); }
double freeMemory() { return static_cast<double>(uv_get_free_memory()); }
double totalmem() { return totalMemory(); }
double freemem() { return freeMemory(); }

std::tuple<double, double, double> loadavg() {
    double avg[3] = {0.0, 0.0, 0.0};
    uv_loadavg(avg);
    return {avg[0], avg[1], avg[2]};
}

Array<NetworkInterfaceInfo> networkInterfaces() {
    uv_interface_address_t* addresses = nullptr;
    int count = 0;
    if (uv_interface_addresses(&addresses, &count) != 0 || addresses == nullptr) {
        return makeRef<std::vector<NetworkInterfaceInfo>>();
    }
    std::vector<NetworkInterfaceInfo> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const uv_interface_address_t& info = addresses[i];
        char host[INET6_ADDRSTRLEN] = {};
        const char* family = info.address.address4.sin_family == AF_INET ? "IPv4" : "IPv6";
        const void* src = info.address.address4.sin_family == AF_INET
                              ? static_cast<const void*>(&info.address.address4.sin_addr)
                              : static_cast<const void*>(&info.address.address6.sin6_addr);
        if (uv_inet_ntop(info.address.address4.sin_family == AF_INET ? AF_INET : AF_INET6,
                         src, host, sizeof(host)) == 0) {
            out.emplace_back(info.name ? std::string(info.name) : std::string(),
                             std::string(host), family,
                             info.is_internal != 0);
        }
    }
    uv_free_interface_addresses(addresses, count);
    return makeRef<std::vector<NetworkInterfaceInfo>>(std::move(out));
}

}  // namespace cp3::runtime::os
