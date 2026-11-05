// =============================================================================
// src/std/network/native.hpp - `std/network` native backend row.
//
// OWNERSHIP: the std/network family (src/std/network/**). One row of the D68
// native-package table; the rest of the table never needs to change for this module.
//
// Backend: libuv v1.53.0 (client side / event loop) plus uWebSockets v20.80.0
// with its in-tree uSockets (server + WebSocket side), pinned static builds -
// DECISIONS D70 + third_party/MANIFEST.md. TLS and per-message deflate are off
// by design (LIBUS_NO_SSL, UWS_NO_ZLIB): beta-one `fetch` handles `http://`
// only, and the define must be on every TU that sees the uWS headers.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

// Verified against third_party/MANIFEST.md "Verified consumer flags":
//   compile: -DUWS_NO_ZLIB -I third_party/uWebSockets/include
//            -I third_party/libuv/include (uSockets headers include <uv.h>)
//   link (Windows): -L third_party/uWebSockets/lib -luSockets
//                   -L third_party/libuv/lib -luv plus libuv's Windows system
//                   libraries. Linux uses architecture-specific archives and
//                   pthread/dl/rt instead.
inline const NativePackage kNetworkNativePackage{
    "std/network",
    // The family ABI tree (D72) first; src/std/json/include serves
    // runtime_server.cpp's cp3/std/json.hpp include, and the buffer /
    // collections roots serve cp3/std/network.hpp's includes (res.headers and
    // req.params/query/headers are Map<string, string>, arrayBuffer is Buffer).
    {"src/std/network/include", "src/std/json/include",
     "src/std/buffer/include", "src/std/collections/include",
     "third_party/uWebSockets/include", "third_party/libuv/include"},
    {"-DUWS_NO_ZLIB"},
    {"src/std/network/runtime/runtime.cpp",
     "src/std/network/runtime/runtime_fetch.cpp",
     "src/std/network/runtime/runtime_fetch_io.cpp",
     "src/std/network/runtime/runtime_dns.cpp",
     "src/std/network/runtime/runtime_web.cpp",
     "src/std/network/server/runtime_server.cpp",
     "src/std/network/server/runtime_server_dispatch.cpp",
     "src/std/network/server/runtime_server_scaled.cpp",
     "src/std/network/ws/runtime_ws.cpp",
     "src/std/network/ws/runtime_ws_client.cpp",
     "src/std/network/ws/runtime_ws_frames.cpp"},
    {},
    {},
    // Parallel to runtimeSources. A program that imports std/network only for
    // fetch should not link the HTTP server or WebSocket runtime, and a
    // WebSocket program should not link the server. The dispatch TU and the
    // scaled server TU are selected by the same HttpServer marker as
    // runtime_server.cpp: listen/close reference the scaled entry points, and
    // the request glue references the dispatcher.
    {"", "fetch(", "fetch(", "dnsResolve(", "makeStaticMiddleware(|makeCorsMiddleware(|makeBodyLimitMiddleware(|Headers(|Request(|Response(|FormData(|AbortSignal::", "HttpServer(", "HttpServer(",
     "HttpServer(", "WebSocket", "WebSocket", "WebSocket"},
    {{"windows-x64", {"third_party/uWebSockets/lib/windows-x64",
                       "third_party/libuv/lib/windows-x64"},
      {"-luSockets", "-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32",
       "-ldbghelp", "-lsynchronization"}},
     {"linux-x64",
      {"third_party/uWebSockets/lib/linux-x64", "third_party/libuv/lib/linux-x64"},
      {"-luSockets", "-luv", "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"third_party/uWebSockets/lib/linux-arm64", "third_party/libuv/lib/linux-arm64"},
      {"-luSockets", "-luv", "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
