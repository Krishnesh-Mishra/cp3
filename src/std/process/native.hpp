// =============================================================================
// src/std/process/native.hpp - `std/process` native backend row (D72).
//
// OWNERSHIP: the std/process family (src/std/process/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module. exec/spawn do need real runtime TUs: child.cpp (the platform child
// primitives) and runtime.cpp (the argv store and the exec/spawn
// orchestration). The include tree also carries std/collections (D44) and the
// vendored libuv (D68) for the process-information probes.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kProcessNativePackage{
    "std/process",
    {"src/std/process/include", "src/std/collections/include",
     "third_party/libuv/include"},
    {},
    {"src/std/process/child.cpp", "src/std/process/runtime.cpp"},
    {},
    {},
    {},
    {{"windows-x64",
      {"third_party/libuv/lib/windows-x64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32", "-ldbghelp",
       "-lsynchronization"}},
     {"linux-x64",
      {"third_party/libuv/lib/linux-x64"},
      {"-luv", "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"third_party/libuv/lib/linux-arm64"},
      {"-luv", "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
