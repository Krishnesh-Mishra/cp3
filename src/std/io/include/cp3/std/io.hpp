// =============================================================================
// cp3/std/io.hpp - `std/io` non-blocking fd readiness.
//
// OWNERSHIP: the std/io family (src/std/io/**). This is the generated-code
// contract for the reactor primitives. Generated programs include it only when
// they import "std/io".
//
// One runtime helper, one per-thread libuv poller. `waitReadable` and
// `waitWritable` suspend only the awaiting coroutine; the event loop keeps
// running timers, microtasks and other fd waiters. `pollUntilDone` is the
// phase-2 helper package shims use instead of writing their own loop.
//
// `fd` is a native socket descriptor. On POSIX it is an int; on Windows it is
// the SOCKET value exposed by the native library. Keep it in the `number`
// exact-integer range.
// =============================================================================
#pragma once

#include <functional>

#include "cp3/async/promise.hpp"

namespace cp3::runtime::io {

// `checkProgress` result values for `pollUntilDone`.
inline constexpr double POLL_READING = 1;
inline constexpr double POLL_WRITING = 2;
inline constexpr double POLL_DONE = 0;
inline constexpr double POLL_FAILED = -1;

// Resolve when `fd` is readable / writable. The promise rejects on poll error,
// hangup/disconnect, invalid descriptor, or when the descriptor is closed
// while the wait is registered. Phase 1 has no cancellation token; a waiter
// stays pending until one of those events occurs.
Promise<void> waitReadable(double fd);
Promise<void> waitWritable(double fd);

// The shared phase-2 helper. Each iteration:
//   state = checkProgress()
//   DONE -> resolve, FAILED -> reject,
//   READING -> await waitReadable(getSocket()),
//   WRITING -> await waitWritable(getSocket()).
// `getSocket` is called again on every iteration because connect-time fds can
// change.
Promise<void> pollUntilDone(std::function<double()> getSocket,
                            std::function<double()> checkProgress);

}  // namespace cp3::runtime::io
