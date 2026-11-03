// =============================================================================
// src/std/network/runtime_loop.hpp - PRIVATE std/network loop plumbing.
//
// OWNERSHIP: the std/network family (src/std/network/**). Shared by the network
// TUs (runtime.cpp, runtime_server.cpp, runtime_ws.cpp, runtime_ws_client.cpp);
// not part of the generated-code contract.
//
// MODEL. One libuv loop exists per thread, owned by `cp3/async/uv_loop.hpp`.
// `std/network` shares it with `std/io` and hands it to uWebSockets with
// `uWS::Loop::get(uvLoop)`: uSockets keeps the pointer with `cleanMe = false`
// (a borrowed "alien" loop it never frees), and every uWS App created on this
// thread uses it. The runtime's EventLoop stays authoritative: the shared loop
// installs the external event source declared in cp3/async/eventloop.hpp, so
// `run()` drives libuv from the idle phase and the program exit rule is
// unchanged (a live fetch, server or socket calls `retainNetwork()` and
// therefore counts as pending work, exactly like a timer).
// =============================================================================
#pragma once

#include <cstdint>

namespace cp3::runtime::network {

// Creates the per-thread libuv loop (idempotent), integrates uWS onto it and
// installs the EventLoop external source. Every public entry point calls this.
void ensureLoop();

// The libuv loop handle as `void*` (uv_loop_t*), so callers that need the raw
// loop do not have to agree on the uv.h include order with uWS headers.
void* loopHandle();

// Live network objects (in-flight fetch, listening server, open socket). Each
// call to retainNetwork() is a retainExternal() on the runtime loop; the pair
// must be balanced. releaseNetwork() from a libuv callback is safe: the
// counter lives on the runtime loop, which the caller is already inside.
void retainNetwork();
void releaseNetwork();

}  // namespace cp3::runtime::network
