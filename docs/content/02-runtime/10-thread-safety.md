---
title: Thread safety and ownership boundaries
chapter: Chapter 2 · Runtime and memory
---
# Thread safety and ownership boundaries

CP3 has asynchronous operations that resume on an event loop and separate APIs that execute work on native threads or workers. The distinction matters for object lifetime and data races. A reference-counting handle protects ownership bookkeeping; it does not make the referenced object safe for concurrent mutation.

## Async work does not mean parallel work

An `async` function and `await` coordinate a promise with the runtime event loop. While waiting for I/O, the loop can make progress on other ready tasks. A synchronous CPU-heavy loop still occupies the executing thread until it returns. See [Async and concurrency](#02-runtime/07-async-and-concurrency).

## Worker and parallel boundaries

Worker APIs execute code in a separate worker context and exchange values through explicit messages. Parallel APIs run supported work across native threads and combine results. They are not interchangeable with callbacks or ordinary promises.

The semantic checker rejects unsupported closures, captures, writes, payload types, and accumulator shapes for these APIs. The acceptance rules are purpose-specific. A type passing normal type checking does not prove that it can be moved to a worker or safely mutated in a parallel closure.

## Atomic ownership is not atomic payload access

The ordinary CP3 `Ref<T>` is `std::shared_ptr<T>`. Its shared control block uses atomic reference-count operations so independent handles can maintain ownership. Those atomics protect the count and lifetime transitions only.

This does not make these operations safe:

```text
worker A: object.counter = object.counter + 1
worker B: object.counter = object.counter + 1
```

Both threads can race while reading and writing the payload. The increment can be lost, and a C++ data race can be undefined behavior. Use a supported reduction, message passing, or synchronization mechanism instead of sharing mutable state without a protocol.

## Captures need two separate reviews

When reviewing a closure that crosses a concurrency boundary, check both:

1. **Lifetime:** will every captured value remain alive for all possible calls?
2. **Synchronization:** can another thread access or mutate the same payload at the same time?

A shared pointer can help with lifetime; it does not answer the synchronization question. An immutable scalar copy may be safe to transfer where the API accepts it. A captured mutable cell or class reference needs stricter review.

## Prefer reductions to shared mutation

Parallel APIs are designed for supported work decomposition and result combination. Prefer a reduction that returns a value to mutating a captured accumulator. The `tests/cases/perf-parallel` cases exercise accepted reductions, boundary conditions, unsupported helper effects, stale analysis facts, and overflow behavior.

Do not generalize one successful integer sum into support for arbitrary callbacks. A helper with side effects, a global read, or an unsupported range shape can change whether a parallel transformation is valid.

## Worker messages are explicit data transfer

A worker message crosses a protocol boundary. Use the payload types accepted by the worker checker and avoid assuming that arbitrary class identity, closures, native pointers, or shared references are transferred intact. The `tests/cases/concurrency/worker` family covers strings, records, unions, return values, termination, and rejected shapes.

A message-oriented design makes ownership clearer: the sender transfers a value, the receiver handles that value, and any reply is another explicit message or promise result.

## Why CP3 does not currently use `LocalRef`

The compiler does not generally prove that all references are confined to one thread and then replace `std::shared_ptr` with a non-atomic local reference count. A future proof would need to account for worker messages, parallel closures, global state, escaped closures, callbacks, and foreign code. Until such a transformation exists and is verified, ordinary references retain the shared runtime representation.

## A review checklist for concurrency changes

- Identify whether execution is on the event loop, a worker, or a parallel thread.
- List all captured bindings and whether they are reassigned.
- Identify every mutable object reachable from those bindings.
- Check whether data is copied, moved, serialized, or shared by the API.
- Review rejection, worker termination, and shutdown behavior.
- Add positive tests and negative tests for rejected capture and payload shapes.
- Use race-aware native testing when modifying runtime synchronization.

## Implementation and tests

The semantic checks live under `src/sema` for the corresponding worker and parallel constructs. Code generation and runtime execution live under `src/codegen` and `src/runtime`; inspect all three layers before changing a rule. Start with `tests/cases/concurrency` and `tests/cases/perf-parallel` to find the existing contract.

## Related pages

- [Async and concurrency](#02-runtime/07-async-and-concurrency)
- [Reference counts, cycles, and threads](#02-runtime/03-reference-ownership)
- [Closure storage and lifetime](#02-runtime/04-closures)
- [Escape analysis and stack storage](#02-runtime/02-escape-analysis)
