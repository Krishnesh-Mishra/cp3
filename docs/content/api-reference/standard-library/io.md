---
title: I/O readiness
---
# I/O readiness

`std/io` exposes reactor primitives for native libraries that already hold a socket and a non-blocking step API. It does not make a blocking C call concurrent. Import the default namespace:

```ts
import io from "std/io";
```

`std/io` and `std/network` share one libuv-backed event loop per thread. A blocking C call with no pollable descriptor cannot be made concurrent by this module.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `waitReadable` | `waitReadable(fd: number): Promise<void>` | Suspends only the calling async function; other waiters and timers continue |
| `waitWritable` | `waitWritable(fd: number): Promise<void>` | Same suspension rules as `waitReadable` |
| `pollUntilDone` | `pollUntilDone(getSocket: () => number, checkProgress: () => number): Promise<void>` | Calls `checkProgress`; waits per the returned constant; `getSocket` is re-called every iteration (descriptors may change mid-connect); an unknown state rejects |
| `POLL_READING` | `POLL_READING: number` | `1` — await `waitReadable(getSocket())` |
| `POLL_WRITING` | `POLL_WRITING: number` | `2` — await `waitWritable(getSocket())` |
| `POLL_DONE` | `POLL_DONE: number` | `0` — resolve |
| `POLL_FAILED` | `POLL_FAILED: number` | `-1` — reject |

## Contract

- `fd` is a native socket descriptor: an `int` on POSIX, or the `SOCKET` value on Windows. Keep it in exact-integer range of `number`.
- The wait suspends only the calling async function; many waiters can share a descriptor (e.g. one reader + one writer).
- The promise rejects when the poll reports an error, the peer hangs up/disconnects, the descriptor is invalid, or the descriptor is closed while the wait is registered (the runtime re-checks descriptors that have waiters and rejects the wait with `std/io: fd closed while waiting`). There is no cancellation token: a pending waiter stays pending until it settles or fails.
- `POLL_*` constants are plain `number`s for `checkProgress` return values; compare exactly against the constants.
- `std/io` does not wrap arbitrary blocking calls in `async` out of the loop; only descriptor-based waits suspend.

Behavior details: [guide](../../standard-library/18-io.md)
