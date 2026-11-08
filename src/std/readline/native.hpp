// =============================================================================
// src/std/readline/native.hpp - `std/readline` native backend row (D68).
//
// OWNERSHIP: the std/readline family (src/std/readline/**). One row of the
// D68 native-package table; the rest of the table never needs to change for
// this module.
//
// Backend: the readline surface is header-only delegation onto the process
// family's stdin pump (cp3/std/process.hpp), so this row lists the process
// runtime TUs (child.cpp + runtime.cpp) and the libuv link line alongside
// its own anchor TU. A program importing std/readline alone still links the
// stdin pump; a program importing both rows compiles each TU once (the
// build driver deduplicates by path).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kReadlineNativePackage{
    "std/readline",
    {"src/std/readline/include", "src/std/process/include",
     "src/std/collections/include", "vendor/third_party/libuv/include",
     "vendor/third_party/unordered_dense/include",
     "vendor/third_party/libsodium/include", "vendor/third_party/xxhash/include"},
    {},
    {"src/std/readline/runtime.cpp", "src/std/process/child.cpp",
     "src/std/process/runtime.cpp",
     // The process header includes the collections Map, so the seeded
     // bucket-hash TU and its sodium/xxhash link ride along.
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
