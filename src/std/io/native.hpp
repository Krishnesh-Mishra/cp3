// =============================================================================
// src/std/io/native.hpp - `std/io` native backend row (D68).
//
// OWNERSHIP: the std/io family (src/std/io/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module.
//
// Backend: libuv v1.53.0 (vendor/third_party/libuv), shared with `std/network` through
// cp3/async/uv_loop.hpp. The row is selected only when a program imports
// "std/io"; a program that never names the module compiles the same runtime
// TUs as before.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kIoNativePackage{
    "std/io",
    {"src/std/io/include", "vendor/third_party/libuv/include"},
    {},
    {"src/std/io/runtime.cpp"},
    {},
    {},
    {},
    {{"windows-x64",
      {"vendor/third_party/libuv/lib/windows-x64"},
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
