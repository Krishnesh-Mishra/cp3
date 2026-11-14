---
title: Async and concurrency
chapter: Chapter 2 · Runtime and memory
---
# Async and concurrency

CP3 has an event-loop runtime for asynchronous operations and separate facilities for parallel and worker execution. Async sequencing and multithreaded execution have different memory and scheduling behavior.

## `async` and `await`

An async function returns a promise-like result. `await` suspends the current continuation until that result settles, then resumes according to the runtime’s event loop.

```ts
async function readInput(): Promise<string> {
  const text = await console.input("Continue? ");
  return text;
}
```

Async I/O avoids blocking the event loop while waiting for that operation. It does not mean the code runs in parallel on another CPU core.

## `for await`

`for await` prefixes a `for...of` loop and binds the awaited value rather than the raw element: a promise-typed element resolves to its value (`Promise<void>` binds `undefined`), and anything else is its own value. It is legal only inside an `async` function (or at the top level):

```ts
for await (const name of names) {
  console.log(name);
}
```

This is not an async-iterator protocol: it is the ordinary `for...of` shape over the same iterable forms (arrays, collections, tuple types) with an await on each bound value — the coroutine suspends on every iteration. The loop's receiver types are the same as ordinary `for...of`; awaiting suspends only the current coroutine, like any other `await`.

## Promises

CP3 provides promise member operations and static combinators implemented by the runtime. `Promise.all` waits for all supplied promises; `Promise.race` settles from the first result (fulfilled or rejected); `Promise.allSettled` records each outcome as `{ ok, value, reason }` without rejecting; `Promise.any` resolves with the first fulfilled value and rejects with an `AggregateError` when every input rejected. `Promise.withResolvers()` returns a record with `promise`, `resolve`, and `reject` — useful for bridging callbacks with async code. See [Global builtins](../language/99-global-builtins.md) for the checked signatures.

## Timers and event loop

Timers and pending asynchronous work use the runtime event loop. The loop is associated with a thread, and scheduled tasks may retain closures and their captured state. A long-running synchronous loop blocks progress on that event loop.

## Native non-blocking I/O

`async` schedules continuations; it does not move a blocking C call to another thread. A native call that blocks still freezes the loop. Libraries that expose a socket and a non-blocking step API must use the reactor path instead:

```ts
import io from "std/io";

await io.waitReadable(fd); // suspend this async function until fd is readable
await io.waitWritable(fd); // suspend until fd is writable

await io.pollUntilDone(
  () => getSocket(),   // called again every iteration: connect can change the fd
  () => checkProgress() // io.POLL_READING | io.POLL_WRITING | io.POLL_DONE | io.POLL_FAILED
);
```

Only the awaiting function suspends. Timers, promise continuations and other fd waiters keep running on the same loop. A poll error, hangup/disconnect, invalid descriptor, or a descriptor closed while waiting rejects the promise. One shared 100 ms descriptor-validity check covers all pending waits, so a local close is detected without a timer or thread per descriptor. Phase 1 exposes no cancellation token; a pending waiter stays pending until it settles or the descriptor fails.

`std/io` shares one libuv loop per thread with `std/network`. The runtime `EventLoop` remains authoritative and drives libuv from its idle phase. Linux uses libuv's epoll backend; Windows uses libuv's IOCP backend. Cross-thread wakeup uses libuv's async handle.

Pure blocking C libraries with no pollable fd are the separate pool path: `import native async "<lib>" as x;`. That surface is phase 3 and is not available until the phase 1-2 reactor tests pass. See [Native packages and ABI boundaries](../cli-and-compiler/07-native-interop.md).

## Parallel operations

The parallel API splits suitable work across native threads and combines results using supported forms. The checker restricts shared mutation, capture reassignment, accumulator types, and closure shapes. Prefer a reduction value over mutation of a shared object. See [Global builtins](../language/99-global-builtins.md) for the checked `parallel.for` / `parallel.reduce` / `parallel.run` signatures and the half-open `start..end` range syntax.

## Workers

Workers communicate through typed messages and handles. Values sent across the boundary must satisfy the worker payload rules. The compiler rejects unsupported captures, payload mismatches, and module forms instead of treating a `shared_ptr` as permission to share arbitrary state.

A worker is declared either as a separate module (a static `.cp3` path string) or inline as a single closure in the same file. The worker entry's default export is an arrow `(thread: Thread<In, Out>) => { ... }`; the annotation is the single source of truth for the channel types. `new Worker("./worker_impl.cp3")` loads the module's default export as the entry; `new Worker((thread: Thread<In, Out>) => { ... })` synthesizes the same entry in the current translation unit:

```ts
// file form — ./worker_sum_impl.cp3
export default (thread: Thread<{ a: number }, number>) => {
  thread.on("message", (data) => {
    thread.postMessage(data.a);
  });
};

// inline form — same file
const w = new Worker((thread: Thread<number, number>) => {
  thread.on("message", (data) => {
    thread.postMessage(data * 2);
  });
});
const doubled = await w.request(14); // 28
w.terminate();
```

`Thread<In, Out>` types the two message directions. `In` is the main-to-worker direction; `Out` is worker-to-main. Use `undefined` in a direction to declare it empty — the direction can never carry a value: sends into it are a compile error (CP3906), and the receive side's listener is nullary.

```ts
export default (thread: Thread<number, undefined>) => {
  thread.on("message", (n) => {
    console.log(`${n}`); // receives; cannot postMessage back
  });
};

const w = new Worker("./worker.cp3");
w.postMessage(21); // worker side of an empty In direction: CP3906
```

`request` also needs a non-empty `Out` direction to carry its reply — an empty `Out` reports CP3906 (`Out direction is empty`), and a mismatched argument reports CP3908 (`does not match the channel's In type`).

Inline closures have a narrower surface than file workers: a reference to an enclosing variable binding — a local of the enclosing function or a module variable of the entry unit — is CP3909. Top-level functions, imports, builtins, and types stay visible to the inline worker, because they are code rather than state:

```ts
const base = 10;
const w = new Worker((thread: Thread<number, number>) => {
  thread.on("message", (data) => {
    thread.postMessage(data + base); // CP3909: 'base' is module state
  });
});
```

The main side handle surface:

| Member | Signature | Semantics |
| --- | --- | --- |
| `w.postMessage(v)` | `(value: In) => void` | Deep-copies `value` into the worker's input queue (numbers, strings, booleans, arrays, records, enums, unions/options). |
| `w.on("message", listener)` | `(listener: (value: Out) => void) => void` | Registers a listener for messages from the worker. |
| `w.on("error", listener)` | `(listener: ...) => void` | Registers a listener for catchable errors from a worker handler; the handler runs on the throwing worker's own loop. |
| `w.request(v)` | `(value: In) => Promise<Out>` | Sends `value` and resolves with the **next** `Out` message; a plain `Promise<Out>`. |
| `w.terminate()` | `() => void` | Stops the worker's loop, frees the thread, and drops queued messages and pending requests. Idempotent. |

The worker side surface (`thread`) mirrors this: `thread.on("message" | "error", listener)`, `thread.postMessage(v)` (deep copy out).

Delivery is FIFO per sender, and a message queues until the receiving side attaches a listener (attaching later still receives it). A live worker keeps the main loop alive; `terminate()` stops the worker's loop, frees the thread, drops queued messages and pending `request`s, and is idempotent — the process then exits once the main loop is idle and no live handles remain. A thrown error inside a message handler is delivered to the other side as a catchable `"error"` event and the throwing side's loop keeps serving; a throw at worker start-up is an `"error"` event to the main side and the thread ends.

## Reference counts are not locks

`std::shared_ptr` uses an atomic ownership count so its control block can track references safely. It does not synchronize field accesses or prevent two threads from racing on the same object. Only use a shared value concurrently when the API and type rules provide a safe protocol.

## Lifecycle and errors

An unhandled rejected async operation can affect process completion according to the event-loop runtime. Await operations that can reject should be caught or intentionally allowed to propagate. A detached task may outlive the lexical function that started it, so its captures need valid lifetimes.
