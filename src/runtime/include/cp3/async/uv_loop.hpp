// =============================================================================
// cp3/async/uv_loop.hpp - the one shared libuv loop per thread.
//
// OWNERSHIP: runtime module. This is an implementation seam, not part of the
// generated-code ABI: generated programs never include uv.h.
//
// `std/network` and `std/io` both need a libuv poller. They MUST share this
// loop: one event loop per thread, never one libuv loop per module. The loop is
// created on first use, installs the EventLoop external source (poll/wait/wake)
// and is intentionally leaked for the life of the thread.
// =============================================================================
#pragma once

namespace cp3::runtime::uv {

// Create the per-thread libuv loop and install it as the EventLoop external
// source. Idempotent. Must be called on the loop's own thread.
void ensureLoop();

// The libuv loop as `void*` (uv_loop_t*). Null when initialisation failed.
// Callers that need the concrete type cast it after including <uv.h>.
void* loopHandle();

}  // namespace cp3::runtime::uv
