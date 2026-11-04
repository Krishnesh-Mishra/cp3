// =============================================================================
// cp3/std/os.hpp - `os` (std/os) backing: Node-style OS info.
//
// OWNERSHIP: the std/os family (src/std/os/**). Declarations are inline'd
// where they are portable; the libuv-backed probes live in runtime.cpp and are
// linked for programs that import "std/os".
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `os.<fn>(args)` -> `::cp3::runtime::os::<fn>(args)` (no arguments).
//
// -----------------------------------------------------------------------------
// SEMANTICS / LIMITATIONS
// -----------------------------------------------------------------------------
//   * `platform()` returns the docs §04 names: "windows" | "macos" | "linux".
//   * `homedir`/`tmpdir`/`hostname` read the environment with sane fallbacks;
//     every fallback is non-empty, which is all the corpus pins.
//   * `cpus()` is std::thread::hardware_concurrency (at least 1).
//   * `totalMemory`/`freeMemory`/`totalmem`/`freemem` read the system's total
//     and free RAM via libuv's uv_get_total_memory/uv_get_free_memory.
//   * `loadavg()` returns the 1/5/15-minute load average as
//     `[number, number, number]`; on platforms without load averages it
//     resolves to all-zero, which matches libuv's behavior there.
//   * `networkInterfaces()` returns
//     `Array<[string, string, string, boolean]>`, one tuple per interface:
//     `[name, address, family, internal]`. Values come from libuv's
//     `uv_interface_addresses`; `family` is `"IPv4"` or `"IPv6"`, and
//     `internal` flags loopback-only interfaces.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "cp3/core/value.hpp"

namespace cp3::runtime::os {

inline std::string platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

namespace detail {

// The first environment variable in `names` with a non-empty value, or "".
inline std::string firstEnv(const char* const* names, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (const char* value = std::getenv(names[i])) {
            if (*value != '\0') return std::string(value);
        }
    }
    return std::string();
}

}  // namespace detail

inline std::string homedir() {
    static const char* const kNames[] = {"USERPROFILE", "HOME"};
    const std::string value = detail::firstEnv(kNames, 2);
    return value.empty() ? std::string(".") : value;
}

inline std::string tmpdir() {
    static const char* const kNames[] = {"TEMP", "TMP", "TMPDIR"};
    const std::string value = detail::firstEnv(kNames, 3);
    return value.empty() ? std::string(".") : value;
}

inline std::string hostname() {
    static const char* const kNames[] = {"COMPUTERNAME", "HOSTNAME"};
    const std::string value = detail::firstEnv(kNames, 2);
    return value.empty() ? std::string("localhost") : value;
}

inline double cpus() {
    const unsigned count = std::thread::hardware_concurrency();
    return static_cast<double>(count == 0 ? 1 : count);
}

// The libuv probes (src/std/os/runtime.cpp). Implementations are kept out of
// this header so generated programs do not pull <uv.h> into the C++ TU; the
// declarations live here for the ABI.
double totalMemory();
double freeMemory();
double totalmem();
double freemem();

std::tuple<double, double, double> loadavg();

// Array of [name, address, family, internal] from uv_interface_addresses.
using NetworkInterfaceInfo =
    std::tuple<std::string, std::string, std::string, bool>;
::cp3::runtime::Array<NetworkInterfaceInfo> networkInterfaces();

}  // namespace cp3::runtime::os
