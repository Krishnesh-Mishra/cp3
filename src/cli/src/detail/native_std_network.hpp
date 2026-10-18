// =============================================================================
// src/cli/src/detail/native_std_network.hpp - `std/network` native backend row.
//
// OWNERSHIP: the network wave (std/network). One row of the D68 native-package
// table; the rest of the table never needs to change for this module.
//
// Backend: libuv v1.53.0 (client side / event loop) plus uWebSockets v20.80.0
// with its in-tree uSockets (server + WebSocket side), pinned static builds -
// DECISIONS D70 + third_party/MANIFEST.md. TLS and per-message deflate are off
// by design (LIBUS_NO_SSL, UWS_NO_ZLIB): beta-one `fetch` handles `http://`
// only, and the define must be on every TU that sees the uWS headers.
// =============================================================================
#pragma once

#include "detail/native_packages.hpp"

namespace cp3::cli::detail {

// Verified against third_party/MANIFEST.md "Verified consumer flags":
//   compile: -DUWS_NO_ZLIB -I third_party/uWebSockets/include
//            -I third_party/libuv/include (uSockets headers include <uv.h>)
//   link:    -L third_party/uWebSockets/lib -luSockets
//            -L third_party/libuv/lib -luv plus the 6 Windows system libraries
//            libuv genuinely needs (each discovered from a real undefined
//            reference; see the MANIFEST for the symbol-to-library mapping).
inline const NativePackage kNetworkNativePackage{
    "std/network",
    {"third_party/uWebSockets/include", "third_party/libuv/include"},
    {"-DUWS_NO_ZLIB"},
    {"src/runtime/src/std/network.cpp"},
    {"third_party/uWebSockets/lib", "third_party/libuv/lib"},
    {"-luSockets", "-luv", "-lws2_32", "-luserenv", "-liphlpapi", "-lole32",
     "-ldbghelp", "-lsynchronization"},
};

}  // namespace cp3::cli::detail
