// =============================================================================
// src/std/network/tcp/native_tcp.hpp - std/network TCP native row fragment.
//
// OWNERSHIP: the std/network family (src/std/network/tcp/**). This is a
// SPLICE FRAGMENT, not a TU the build compiles: TCP is part of `std/network`
// (imported as `import { createTcpServer, connectTcp } from "std/network"`),
// so it must join the EXISTING kNetworkNativePackage row in
// src/std/network/codegen/native.hpp. A separate row for the same specifier
// would be selected twice (see selectNativePackages).
//
// ---------------------------------------------------------------------------
// WIRING (lead applies): in src/std/network/codegen/native.hpp, extend
// kNetworkNativePackage:
//   includeDirs:  += "src/std/network/tcp/include",
//                 (serves the generated-code include
//                  "cp3/std/network/tcp.hpp"; the TUs need no uWS headers,
//                  only <uv.h> from third_party/libuv/include, already in
//                  the row, and cp3/std/buffer.hpp from the buffer include
//                  root, also already in the row.)
//   runtimeSources +=
//                 "src/std/network/tcp/runtime_tcp_socket.cpp",
//                 "src/std/network/tcp/runtime_tcp_client.cpp",
//                 "src/std/network/tcp/runtime_tcp_server.cpp",
//                 "src/std/network/tcp/runtime_tcp_reuseport.cpp",
//   runtimeSourceMarkers += (PARALLEL to runtimeSources - append exactly
//                 these four, in the same order as the sources above)
//                 "TcpSocket|TcpServer|createTcpServer(|connectTcp(",
//                 "connectTcp(",
//                 "TcpServer|createTcpServer(",
//                 "TcpServer|createTcpServer(",
//   No link change (libuv is already linked for std/network), no define
//   change (these TUs never see the uWS headers, so -DUWS_NO_ZLIB is
//   irrelevant to them), no CMake change (the native row's lists are
//   explicit, so no glob edit is needed).
//   Marker alternative: leave all four markers "" so the TUs always link
//   with std/network; they are small and dependency-free apart from libuv.
// =============================================================================
#pragma once
