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
     "vendor/third_party/libuv/include", "vendor/third_party/unordered_dense/include",
     "vendor/third_party/libsodium/include", "vendor/third_party/xxhash/include"},
    {},
    {"src/std/process/child.cpp", "src/std/process/runtime.cpp",
     // cp3/std/process.hpp includes cp3/std/collections.hpp (process.env is
     // a Map); instantiating it references bucketHashBytes, so the seeded
     // bucket-hash TU and its sodium/xxhash link ride along (the collections
     // row's backend).
     "src/runtime/src/core/bucket_hash.cpp"},
    {},
    {"-lsodium", "-lxxhash"},
    {},
    {{"windows-x64",
      {"vendor/third_party/libuv/lib/windows-x64",
       "vendor/third_party/libsodium/lib/windows-x64",
       "vendor/third_party/xxhash/lib/windows-x64"},
      {"-luv", "-lsodium", "-lxxhash", "-lws2_32", "-luserenv", "-liphlpapi",
       "-lole32", "-ldbghelp", "-lsynchronization"}},
     {"windows-arm64",
      {"vendor/third_party/libuv/lib/windows-arm64",
       "vendor/third_party/libsodium/lib/windows-arm64",
       "vendor/third_party/xxhash/lib/windows-arm64"},
      {"-luv", "-lsodium", "-lxxhash", "-lws2_32", "-luserenv", "-liphlpapi",
       "-lole32", "-ldbghelp", "-lsynchronization"}},
     {"linux-x64",
      {"vendor/third_party/libuv/lib/linux-x64",
       "vendor/third_party/libsodium/lib/linux-x64",
       "vendor/third_party/xxhash/lib/linux-x64"},
      {"-luv", "-lsodium", "-lxxhash", "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"vendor/third_party/libuv/lib/linux-arm64",
       "vendor/third_party/libsodium/lib/linux-arm64",
       "vendor/third_party/xxhash/lib/linux-arm64"},
      {"-luv", "-lsodium", "-lxxhash", "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
