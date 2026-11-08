// =============================================================================
// src/std/fs/native.hpp - `std/fs` native backend row (D68).
//
// OWNERSHIP: the std/fs family (src/std/fs/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module.
//
// Backend: the C++ standard library only (`std::filesystem` + `std::fstream`,
// see src/std/fs/runtime.cpp) PLUS libuv for `fs.watch` (src/std/fs/runtime_watch.cpp,
// ledger H): the libuv include root and the libuv archive/link rows are the
// network row's libuv half (vendor/third_party/MANIFEST.md "Verified consumer flags").
// No extra compile flags. The two runtime TUs are selected when a program
// imports "std/fs".
//
// The header (cp3/std/fs.hpp) carries the real std/buffer `Buffer` chunk and
// the std/date `FileStats.modifiedAt` value (the round-3 seams closed in the
// finish phase), so the two header-only family trees are include roots too.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kFsNativePackage{
    "std/fs",
    {"src/std/fs/include", "src/std/buffer/include", "src/std/date/include",
     "vendor/third_party/libuv/include"},
    {},
    {"src/std/fs/runtime.cpp", "src/std/fs/runtime_watch.cpp"},
    {},
    {},
    // Parallel to runtimeSources: both TUs compile whenever the row is selected.
    {"", ""},
    // The libuv archive rows (the network row's libuv half; the fs row needs no
    // uSockets). Filled from vendor/third_party/MANIFEST.md.
    {{"windows-x64", {"vendor/third_party/libuv/lib/windows-x64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32",
       "-ldbghelp", "-lsynchronization"}},
     {"windows-arm64", {"vendor/third_party/libuv/lib/windows-arm64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32",
       "-ldbghelp", "-lsynchronization"}},
     {"linux-x64",
      {"vendor/third_party/libuv/lib/linux-x64"}, {"-luv", "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"vendor/third_party/libuv/lib/linux-arm64"}, {"-luv", "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
