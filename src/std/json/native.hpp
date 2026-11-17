// =============================================================================
// src/std/json/native.hpp - `std/json` native backend row (D68).
//
// OWNERSHIP: the std/json family (src/std/json/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module.
//
// Backend: the JSON parse hot path is the vendored simdjson DOM parser
// (vendor/third_party/simdjson, v5.1.1, Apache-2.0), reached through
// src/std/json/parse.cpp - the single engine TU. This row compiles BOTH it
// and vendor/third_party/simdjson/src/simdjson.cpp, once per consumer program
// that names the `std/json` parse/verify surface (the markers below; a
// program that only stringifies still links them because the generated
// headers transitively reach the engine even in stringify-only paths). The
// marker is preferred over an import-only gate because the family's headers
// carry the parse machinery into generated text without importing "std/json"
// (e.g. `fetch<T>()` through std/network); applyNativePackages in
// src/cli/src/commands/build/build.cpp additionally selects this row by
// content ("cp3/std/json.hpp" in the generated text) for exactly those
// facade cases.
//
// `-DSIMDJSON_MINUS_ZERO_AS_FLOAT=1`: simdjson v5.1.1 writes a bare `-0`
// literal onto the tape as integer `0` without this flag, losing the sign
// the reader must preserve (`JSON.parse<number>("-0")` -> -0.0). The define
// is read only by the TU compiling simdjson.cpp (the fs and glaze-facing
// TUs never see the simdjson headers, so the flag is inert there).
// The writer is glaze's JSON serializer (Ledger J), also header-only.
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

// Verified consumer flags:
//   compile: -DSIMDJSON_MINUS_ZERO_AS_FLOAT=1 -I src/std/json/include -I vendor/third_party/fast_float/include
//            -I vendor/third_party/simdjson/include
//            -I vendor/third_party/glaze/include
//   (the file API, additionally):
//   -I src/std/fs/include -I src/std/buffer/include -I src/std/date/include
//   -I vendor/third_party/libuv/include
//   link    (the file API): -luv -lws2_32 -luserenv -liphlpapi -lole32
//   -ldbghelp -lsynchronization   (windows)
//   -luv -pthread -ldl -lrt      (linux)
inline const NativePackage kJsonNativePackage{
    "std/json",
    {"src/std/json/include", "vendor/third_party/fast_float/include",
     "vendor/third_party/glaze/include",
     "vendor/third_party/simdjson/include",
     "src/std/fs/include", "src/std/buffer/include", "src/std/date/include",
     "vendor/third_party/libuv/include"},
    // -DSIMDJSON_MINUS_ZERO_AS_FLOAT=1: a bare `-0` literal must survive the
    // engine (see the note above). Read only by the TU compiling simdjson.cpp.
    {"-DSIMDJSON_MINUS_ZERO_AS_FLOAT=1"},
    // The fs TU compiles and links only when the generated program names the
    // fs runtime namespace (marker below) - i.e. when the JSON file API is
    // used. Programs importing "std/fs" link the same TU via the fs row;
    // selectRuntimeSources deduplicates across rows. The parse engine TUs
    // (parse.cpp + the vendored simdjson single TU) compile and link only
    // when the generated program names the parse/verify spellings; the
    // verify* readers ride the same gate because they share the engine
    // entry point.
    {"src/std/fs/runtime.cpp", "src/std/json/parse.cpp",
     "vendor/third_party/simdjson/src/simdjson.cpp"},
    {},
    {},
    // Marker-gated, parallel to runtimeSources: the fs TU matches only when
    // the generated text names the file API (the emitted call spells
    // ::cp3::runtime::json::parseFile(Sync) / stringifyToFile(Sync) and
    // nothing else does); the engine TUs match whenever the generated text
    // includes "cp3/std/json.hpp" itself (the same content cue that pulls
    // this row for facade/`fetch<T>()` uses without an import). Programs
    // that only stringify pay for the vendored engine TUs worth two compile
    // jobs; the alternative (marker-gating those TUs on parse/verify spellings)
    // cannot see across the callback templates the network family expands
    // inline to reach the engine. A stringify-only path therefore links them.
    {"::cp3::runtime::json::parseFile|::cp3::runtime::json::parseFileSync|::cp3::runtime::json::stringifyToFile|::cp3::runtime::json::stringifyToFileSync",
     "cp3/std/json.hpp",
     "cp3/std/json.hpp"},
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
