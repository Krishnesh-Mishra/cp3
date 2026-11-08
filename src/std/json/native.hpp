// =============================================================================
// src/std/json/native.hpp - `std/json` native backend row (D68).
//
// OWNERSHIP: the std/json family (src/std/json/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module.
//
// Backend: the JSON reader is header-only (fast_float, header-only, vendored).
// The `std/json` FILE API (JSON.parseFile(Sync)/JSON.stringifyToFile(Sync),
// src/std/json/include/cp3/std/json_file.hpp) is implemented over `std/fs`'s
// runtime TU, `src/std/fs/runtime.cpp`: this row compiles it and links it only
// when the generated program names the fs runtime namespace (marker
// `::cp3::runtime::fs::` - only the file API lowering emits it). The file
// include chain (cp3/std/fs.hpp) fs.hpp drags in the buffer/date header-only
// families and, through `uv_loop.cpp`'s selection rule, libuv; the buffer/date
// include roots, the libuv include root and the libuv archive rows mirror
// src/std/fs/native.hpp so a program can use the file API without importing
// "std/fs". Marker-gating keeps the TU out of the link when unused; the libuv
// archive rows follow the fs row (they are unconditional in a selected row -
// see the report for that trade-off).
//
// Verified flags per backend (include roots, defines, archives and the system
// libraries each archive actually needs) are recorded in vendor/third_party/MANIFEST.md
// under "Verified consumer flags"; keep the rows in sync with it.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

// Verified consumer flags (header-only backend):
//   compile: -I src/std/json/include -I vendor/third_party/fast_float/include
//   (the file API, additionally):
//   -I src/std/fs/include -I src/std/buffer/include -I src/std/date/include
//   -I vendor/third_party/libuv/include
//   link    (the file API): -luv -lws2_32 -luserenv -liphlpapi -lole32
//   -ldbghelp -lsynchronization   (windows)
//   -luv -pthread -ldl -lrt      (linux)
inline const NativePackage kJsonNativePackage{
    "std/json",
    {"src/std/json/include", "vendor/third_party/fast_float/include",
     "src/std/fs/include", "src/std/buffer/include", "src/std/date/include",
     "vendor/third_party/libuv/include"},
    {},
    // The fs TU compiles and links only when the generated program names the
    // fs runtime namespace (marker below) - i.e. when the JSON file API is
    // used. Programs importing "std/fs" link the same TU via the fs row;
    // selectRuntimeSources deduplicates across rows.
    {"src/std/fs/runtime.cpp"},
    {},
    {},
    // Marker-gated: matches only when the generated text names the file API
    // (the emitted call spells ::cp3::runtime::json::parseFile(Sync) /
    // stringifyToFile(Sync) and nothing else does).
    {"::cp3::runtime::json::parseFile|::cp3::runtime::json::parseFileSync|::cp3::runtime::json::stringifyToFile|::cp3::runtime::json::stringifyToFileSync"},
    // Parallel to std/fs's row: when the file API names the fs runtime the
    // selectRuntimeSources marker ("cp3/std/fs.hpp" in the generated text)
    // pulls uv_loop.cpp in; libuv is a hard link dependency then. Same
    // platform rows as src/std/fs/native.hpp.
    {{"windows-x64",
      {"vendor/third_party/libuv/lib/windows-x64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32",
       "-ldbghelp", "-lsynchronization"}},
     {"windows-arm64",
      {"vendor/third_party/libuv/lib/windows-arm64"},
      {"-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32",
       "-ldbghelp", "-lsynchronization"}},
     {"linux-x64",
      {"vendor/third_party/libuv/lib/linux-x64"},
      {"-luv", "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"vendor/third_party/libuv/lib/linux-arm64"},
      {"-luv", "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
