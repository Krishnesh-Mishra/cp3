// =============================================================================
// src/std/network/webserver/router/native.hpp - webserver Router native rows.
//
// OWNERSHIP: the std/network family
// (src/std/network/webserver/router/**). This is a SPLICE FRAGMENT, not a TU
// the build compiles: the Router is part of `std/network` (imported as
// `import { Router } from "std/network"`), so it must join the EXISTING
// kNetworkNativePackage row in src/std/network/codegen/native.hpp. A
// separate row for the same specifier would be selected twice (see
// selectNativePackages), and the router TU needs no extra vendor flags
// (it never sees the uWS headers), so the two-line change below is the
// whole wiring.
//
// ---------------------------------------------------------------------------
// WIRING (lead applies): in src/std/network/codegen/native.hpp, extend
// kNetworkNativePackage:
//   includeDirs:  += "src/std/network/webserver/router/include"
//   runtimeSources += "src/std/network/webserver/router/runtime_router.cpp"
//   runtimeSourceMarkers += "Router("   (parallel to runtimeSources, so a
//     program that imports std/network without using Router does not link
//     the router TU; the marker is the generated construct spelling
//     `::cp3::runtime::network::webserver::Router(` - see codegen_router.cpp).
//     Simpler alternative: leave the marker "" so it always links with the
//     server row; the TU is small and dependency-free.
// No other build file changes (the native row's lists are explicit, so no
// CMake glob edit is needed either way).
//
// The include root serves `cp3/std/network/webserver/router.hpp` (the
// generated-code include of the Router surface).
// =============================================================================
#pragma once
