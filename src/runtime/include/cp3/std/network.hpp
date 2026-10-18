// =============================================================================
// cp3/std/network.hpp - `std/network` backing surface.
//
// OWNERSHIP: runtime module (the network wave fills this file; the header is the
// generated-code contract, so keep it additive like every cp3/std header, D14).
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 §network)
// -----------------------------------------------------------------------------
//   fetch(url, options)            -> http:// only in beta one (TLS off, D70)
//   createServer(...)              -> uWebSockets-backed HTTP server
//   WebSocket / WebSocketServer    -> uSockets/uWS framing, same event loop
//   Named imports only:
//     import { fetch, createServer, WebSocket, WebSocketServer } from "std/network";
//
// Backend: libuv v1.53.0 + uWebSockets v20.80.0 (third_party/**, D70). The
// implementation TU is src/runtime/src/std/network.cpp and is compiled + linked
// only when a program imports "std/network"
// (src/cli/src/detail/native_std_network.hpp, D68).
// =============================================================================
#pragma once

namespace cp3::runtime::network {

// STUB: the network wave lands the surface here.

}  // namespace cp3::runtime::network
