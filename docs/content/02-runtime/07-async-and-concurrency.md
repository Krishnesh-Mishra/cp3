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

## Promises

CP3 provides promise member operations and static combinators implemented by the runtime. `Promise.all` waits for all supplied promises; `Promise.race` settles from the first result; `Promise.allSettled` records each outcome; `Promise.any` resolves with the first fulfilled value. `Promise.withResolvers()` returns a record with `promise`, `resolve`, and `reject` — useful for bridging callbacks with async code. An uncaught `Promise.any` rejection surfaces an `AggregateError`.

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

Pure blocking C libraries with no pollable fd are the separate pool path: `import native async "<lib>" as x;`. That surface is phase 3 and is not available until the phase 1-2 reactor tests pass. See [Native packages and ABI boundaries](../04-compiler-cli/07-native-interop.md).

## Parallel operations

The parallel API splits suitable work across native threads and combines results using supported forms. The checker restricts shared mutation, capture reassignment, accumulator types, and closure shapes. Prefer a reduction value over mutation of a shared object.

## Workers

Workers communicate through typed messages and handles. Values sent across the boundary must satisfy the worker payload rules. The compiler rejects unsupported captures, payload mismatches, and module forms instead of treating a `shared_ptr` as permission to share arbitrary state.

A worker's channel is typed by the entry arrow's `Thread<In, Out>` annotation. Use `undefined` in a direction to declare it empty — the direction can never carry a value: sends into it are a compile error (CP3906), and the receive side's listener is nullary. `request` also needs a non-empty `Out` direction to carry its reply.

```ts
export default (thread: Thread<number, undefined>) => {
  thread.on("message", (n) => {
    console.log(`${n}`); // receives; cannot postMessage back
  });
};
```

```ts
const w = new Worker("./worker.cp3");
w.postMessage(21); // worker side of an empty In direction: CP3906
```

## Reference counts are not locks

`std::shared_ptr` uses an atomic ownership count so its control block can track references safely. It does not synchronize field accesses or prevent two threads from racing on the same object. Only use a shared value concurrently when the API and type rules provide a safe protocol.

## Lifecycle and errors

An unhandled rejected async operation can affect process completion according to the event-loop runtime. Await operations that can reject should be caught or intentionally allowed to propagate. A detached task may outlive the lexical function that started it, so its captures need valid lifetimes.
