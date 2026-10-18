// =============================================================================
// cp3/std/os.hpp - `os` (std/os) backing: Node-style OS info.
//
// OWNERSHIP: runtime module. Additive to the generated-code ABI (D14): all
// functions are `inline`, header-only.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `os.<fn>(args)` -> `::cp3::runtime::os::<fn>(args)` (all zero-argument).
//
// -----------------------------------------------------------------------------
// SEMANTICS / LIMITATIONS
// -----------------------------------------------------------------------------
//   * `platform()` returns the docs §04 names: "windows" | "macos" | "linux".
//   * `homedir`/`tmpdir`/`hostname` read the environment with sane fallbacks;
//     every fallback is non-empty, which is all the corpus pins.
//   * `cpus()` is std::thread::hardware_concurrency (at least 1).
//   * `totalMemory`/`freeMemory` are a portable stand-in: the temp volume's
//     capacity/free bytes via std::filesystem::space. The corpus pins only
//     "total > 0" / "free >= 0"; a real per-platform RAM probe is a follow-up
//     (a <windows.h> probe would drag Windows macros into every generated
//     program that touches `os`).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

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

namespace detail {

inline std::filesystem::space_info tempSpace() {
    std::error_code ec;
    const std::filesystem::space_info info =
        std::filesystem::space(std::filesystem::temp_directory_path(ec), ec);
    return ec ? std::filesystem::space_info{} : info;
}

}  // namespace detail

inline double totalMemory() {
    return static_cast<double>(detail::tempSpace().capacity);
}

inline double freeMemory() {
    return static_cast<double>(detail::tempSpace().available);
}

}  // namespace cp3::runtime::os
