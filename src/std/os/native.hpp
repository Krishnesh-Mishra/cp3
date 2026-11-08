// =============================================================================
// src/std/os/native.hpp - `std/os` native backend row (D72).
//
// OWNERSHIP: the std/os family (src/std/os/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. The row supplies the generated-code include tree and the libuv-backed
// runtime TUs selected when a program imports "std/os". libuv is shared with
// `std/io` and `std/network` through vendor/third_party/libuv.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kOsNativePackage{
    "std/os",
    {"src/std/os/include", "vendor/third_party/libuv/include"},
    {},
    {"src/std/os/runtime.cpp"},
    {},
    {},
    {},
    {{"windows-x64",
      {"vendor/third_party/libuv/lib/windows-x64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32", "-ldbghelp",
       "-lsynchronization"}},
     {"windows-arm64",
      {"vendor/third_party/libuv/lib/windows-arm64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32", "-ldbghelp",
       "-lsynchronization"}},
     {"linux-x64",
      {"vendor/third_party/libuv/lib/linux-x64"},
      {"-luv", "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"vendor/third_party/libuv/lib/linux-arm64"},
      {"-luv", "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
