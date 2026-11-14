---
title: Async non-blocking I/O
---
# Async non-blocking I/O

CP3 runs async work on **one event loop per thread** — never a hidden pool, never a background thread you cannot see. Async sequencing (`async`/`await`, promises, timers) and multithreaded execution (workers, `parallel.*`) are different machines with different rules, and this page covers both: how the loop works, how libuv and uWebSockets ride it, and what you must know to keep it unblocked.

## The two-loop design: one authoritative, one subordinate

CP3's runtime `EventLoop` (`src/runtime/include/cp3/async/eventloop.hpp`) is the **only** scheduling authority, and there is exactly one per thread (`currentLoop()`). It owns timers, promise continuations, and the exit rule: **a program exits when there is no pending work** — an open server counts as pending work, which is why `app.listen(...)` keeps the process alive.

Libuv is deliberately **not** a peer. It is an external event source plugged into the loop through three opaque callbacks (`src/runtime/src/async/uv_loop.cpp`):

| Callback | What it does |
| --- | --- |
| `poll` | Runs `uv_run(&loop, UV_RUN_NOWAIT)` — one non-blocking turn |
| `waitTurn` | Arms a bridge timer bounded by the nearest runtime timer, runs `uv_run(&loop, UV_RUN_ONCE)` |
| `wake` | `uv_async_send` — the thread-safe nudge used by cross-thread posts |

If a program never imports anything network- or I/O-related, no libuv is initialized at all, and generated code never sees a `uv.h` type. uWebSockets rides the same libuv loop through `uWS::Loop` with a deliberately borrowed (never freed) handle. There is exactly one `uv_loop_init` in the tree — the per-thread loop is leaked for the life of the thread by design, because a program exits once the event loop has no pending work left.

## Coroutines and await semantics

`async` functions are lowered to **C++20 coroutines** with JavaScript's eager semantics: calling an async function starts it immediately. `await` always suspends to a queued microtask — never inline — and the continuation resumes after the loop drains the microtask queue, which is emptied completely and FIFO between macrotasks.

Promises are first-class: `Promise<T>` with `.then` / `.catchError` / `.finally`, combinators (`all`, `race`, `allSettled`, `any`), `Promise.withResolvers()`, and `for await` as the ordinary `for...of` shape with the bound value awaited (not an async-iterator protocol — it suspends the coroutine on each iteration). A rejected promise that nobody observes is **fatal at loop drain** (an unhandled-rejection abort, not a warning), which is deliberate: silent background failures are worse than loud ones.

## Timers and the heap

Runtime timers live in a binary min-heap ordered by `(deadline, insertion id)`, with a fast FIFO path for delay-zero timers. Repeating timers use libuv's fixed-schedule semantics (`deadline += period`, minimum 1 ms), and the timer phase drains due timers back-to-back. Await-able sleeps, microtasks, and `setImmediate` are scheduled on the same machinery.

## Where non-blocking calls actually suspend

An `await` suspends the *current coroutine*; it does not move code to another thread and it does not make a blocking C call non-blocking. The async CP3 exposes:

- **`std/io`** — a libuv `uv_poll_t` reactor over a shared per-thread libuv loop: `waitReadable(fd)`, `waitWritable(fd)`, and `pollUntilDone(pollable)` suspend the calling coroutine while other work (timers, promise continuations, other fd waiters) keeps running. One 100 ms descriptor-validity check covers all pending waits; a descriptor closed while waiting rejects the promise.
- **`std/network` servers** — `createServer()` builds a uWebSockets `uWS::App` (or `SSLApp` for TLS) on the same loop; HTTP parsing and WebSocket framing happen inside uWS, in-process, in the same event loop. `createServer({ threads: n })` starts `n` OS threads, each with **its own loop and its own uWS app** (Windows: loop-0 listener plus a pre-open socket handoff; POSIX: `SO_REUSEPORT` listeners) — still one loop per thread, not one process and not a second scheduler.
- **HTTP fetch** — plain `http://` over its own libuv TCP pump; `https://` rides the same OpenSSL `TlsSocket` engine as everything else. One request per connection; a response body is fully buffered before resolution.

A native call that blocks freezes its loop. Pure blocking libraries without a pollable descriptor are on the (not yet shipped) pool path, not the reactor path. If you need CPU parallelism, that is what `parallel.*` and workers are for — not `async`.

## Workers and `parallel.*`: real threads, explicit rules

**Workers** (`new Worker("./worker.cp3")` or an inline closure) run on a separate OS thread with its **own event loop**. The sides communicate only by typed messages through `Thread<In, Out>`:

- `postMessage(v)` deep-copies the payload into the receiving queue (numbers, strings, booleans, arrays, records, enums, unions); FIFO per sender; delivery queues until the receiver attaches a listener.
- `request(v)` resolves with the next `Out` message; `terminate()` is idempotent and drops queued messages and pending requests.
- The compiler rejects what it cannot transfer: wrong payload shapes, mismatched channel directions, and — for inline workers — any capture of an enclosing variable binding (CP3909). Top-level functions, imports, builtins, and types stay visible, because they are code, not state.

The `parallel.*` API is a **hand-rolled fixed `std::thread` pool** — explicitly not TBB (the spec disallowed third-party libraries in that layer; `src/runtime/src/parallel/parallel.cpp`). It runs supported work decomposition (`parallel.for`, `parallel.reduce`, `parallel.run`) under checker-enforced rules about captures, accumulator types, and mutation: prefer returning a reduction value over mutating captured state, because a captured mutable reference is rejected or races.

## What the event loop never does

- It never runs a background thread you didn't ask for.
- It never stops the world. The cycle collector's trial-deletion pass (see [No garbage collector](./02-no-garbage-collector.md)) runs *at safe points* in this loop, budgeted and resumable — it is not a hidden GC interlude.
- It never shares mutable state across threads for you. Cross-thread delivery goes through `postFromThread` (mutex + condition variable + libuv async wake) — the one sanctioned path — and ownership of everything else stays explicit.

## Practical rules

1. One heavy synchronous loop in an `async` function stalls that loop — timers, fetches, and server requests on it all wait. Move CPU work to `parallel.*` or a worker.
2. `await` marks a suspension point, not a thread hop. Two tasks interleave only at awaits.
3. Unhandled rejections are fatal at drain; catch what you can or deliberately let it propagate — silently dropping them is not possible.
4. State belongs to a thread by construction: initialize module state before the loop starts, and prefer message passing to shared references when a boundary exists (`isWorkerBoundary`/`isParallelBoundary` are the checks).

## Inspecting the runtime

- `src/runtime/include/cp3/async/eventloop.hpp` — the loop contract, microtasks, `Task`, the exit rule.
- `src/runtime/src/async/uv_loop.cpp` — libuv bootstrap, `poll`/`waitTurn`/`wake` wiring.
- `src/runtime/src/async/timers.cpp` — heap and FIFO timer phases.
- `src/runtime/include/cp3/async/promise.hpp` — the `Promise<T>` codegen contract.
- Runtime unit tests: `src/runtime/tests/io_test.cpp`, `promise_test.cpp`, `worker_test.cpp`, `parallel_test.cpp`.

## Related pages

- [No garbage collector](./02-no-garbage-collector.md) — lifetime across the loop boundary
- [Async and concurrency, in the language](../language/99-global-builtins.md) — checked `parallel.*` signatures and diagnostics
- [I/O readiness (std/io)](../standard-library/18-io.md) and [Network](../standard-library/13-network.md) — the user-facing APIs
- [Runtime libraries](./05-runtime-libraries.md) — libuv and uWebSockets, the native substrate
