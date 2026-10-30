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

CP3 provides promise member operations and static combinators implemented by the runtime. `Promise.all` waits for all supplied promises; `Promise.race` settles from the first result; `Promise.allSettled` records each outcome. Check the concrete semantic and runtime implementation for accepted container shapes.

## Timers and event loop

Timers and pending asynchronous work use the runtime event loop. The loop is associated with a thread, and scheduled tasks may retain closures and their captured state. A long-running synchronous loop blocks progress on that event loop.

## Parallel operations

The parallel API splits suitable work across native threads and combines results using supported forms. The checker restricts shared mutation, capture reassignment, accumulator types, and closure shapes. Prefer a reduction value over mutation of a shared object.

## Workers

Workers communicate through typed messages and handles. Values sent across the boundary must satisfy the worker payload rules. The compiler rejects unsupported captures, payload mismatches, and module forms instead of treating a `shared_ptr` as permission to share arbitrary state.

## Reference counts are not locks

`std::shared_ptr` uses an atomic ownership count so its control block can track references safely. It does not synchronize field accesses or prevent two threads from racing on the same object. Only use a shared value concurrently when the API and type rules provide a safe protocol.

## Lifecycle and errors

An unhandled rejected async operation can affect process completion according to the event-loop runtime. Await operations that can reject should be caught or intentionally allowed to propagate. A detached task may outlive the lexical function that started it, so its captures need valid lifetimes.
