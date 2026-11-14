---
title: No garbage collector
---
# No garbage collector

CP3 manages object lifetimes without a tracing garbage collector. There is no periodic heap scan, no stop-the-world pause, and no background reclamation thread. Instead, the runtime combines three mechanisms, each documented below: **deterministic reference counting** for ordinary object graphs, a **non-tracing cycle collector** for the islands that plain counting cannot reclaim, and **weak links** when you want to express non-ownership directly.

Understanding which mechanism applies to your object is the single most useful fact in this chapter.

## The three mechanisms

| Mechanism | Applies to | Cost profile |
| --- | --- | --- |
| `Ref<T>` — atomic reference counting | The default for class instances, arrays, and closures | Retains/releases are atomic operations on the control block; the object is destroyed exactly when the last handle goes away |
| `CycleRef<T>` — Bacon–Rajan trial deletion | Cycle-capable class islands the compiler proves collector-safe | One allocation carries the object and its bookkeeping; collection happens at event-loop safe points, never inside a macrotask |
| `WeakRef<T>` / `weak` fields | Non-owning back-links, observers, caches | No ownership; a read either acquires a strong handle or yields `undefined` |

If none of these apply (an unprovable cycle of opaque edges), the compiler rejects the program at compile time (CP3401) rather than fall back to a global collector. That is the trade CP3 makes: there is no invisible tracing pass, ever.

## Values versus references

CP3 has two kinds of types:

- **Value-shaped** — `number`, `boolean`, native scalars, enums, and literals. Copied by value. No allocation, no lifetime question.
- **Reference-shaped** — class instances, arrays, closures, and several runtime values. Behave like JavaScript references: sharing one handle is sharing one object; copying copies the handle, not the object graph.

The ordinary reference handle is `Ref<T>`, which is literally `std::shared_ptr<T>` (`src/runtime/include/cp3/core/value.hpp`). The control block uses atomic reference counting, so handles can be copied and released across supported thread boundaries. Those atomics protect **ownership bookkeeping only** — they do not serialize access to the object's fields.

Objects are allocated with `makeRef<T>(args...)`, which calls `std::allocate_shared` through CP3's `RefPoolAllocator`: a thread-local, bounded free list (at most 2 MiB of cached blocks per thread per specialization) that can reuse a released block for a later allocation **of the same type**. The control block is still created and the count is still atomic — the pool is block reuse, not a bump arena, and it does not change lifetime semantics.

## Arrays are `std::vector` behind a reference

The ordinary array is `Ref<std::vector<E>>`. `makeArray` creates the shared vector; growth costs capacity reallocation plus moving or copying elements (for reference-element arrays, that movement also copies handles). Nothing here is managed for you beyond that: there is no hidden array copying and no magic pre-sizing.

- `push` on a counted, proven loop can reserve capacity once (see [Escape analysis](./03-escape-analysis.md#the-counted-push-reserve-hint)).
- `reserve` exists and is the honest tool when you know the size ahead of time.
- For hot paths over fixed-width data, typed arrays (`Float64Array` and friends, `DataView`, `ArrayBuffer`) store raw binary with **no per-element references at all** — the fastest, simplest storage CP3 offers when the shape fits.

## LocalRef: paying for counting only where someone might look

Some classes qualify for `LocalRef<T>` — a handle with a **plain (non-atomic) owner count**, same shared-ownership semantics, same last-owner destruction, no thread-confinement caveat **enforced by construction** (the compiler emits it only under a unit-level gate).

The gate is deliberate and coarse (`analyzeEscapeAllocations`, `src/codegen/src/support/core/escape_analysis.cpp`): a compilation unit disqualifies itself entirely if it contains any worker/parallel boundary marker, any weak-reference use, any interface declaration, any exported value, or any inheritance. Eligible non-exported, non-generic classes in a clean unit use `LocalRef` instead of `Ref`; one disqualifying feature reverts all of them to atomic `Ref`. It is a compilation-unit decision, not a per-object escape proof — do not infer representation from syntax; inspect generated C++ (`makeRef` vs `makeLocalRef`, `Ref<` vs `LocalRef<`).

When both an escape-analysis proof and a cycle-capable classification apply to one class, **`CycleRef` wins** over `LocalRef` in the generated spelling.

## Closures capture — and captured bindings can extend lifetimes

A capture that is never reassigned can be copied into the closure environment (a small scalar capture is often stored inline). A captured binding that **is** reassigned lowers to a shared **cell** (`Cell<T>` in `value.hpp`): the enclosing function and every closure observe one box, which is the source-level fact CP3 preserves. The box is allocated when the first reassigned capture is created, and lives as long as any capturing closure survives.

Practical consequence: an escaping closure keeps everything it captured alive — including large objects you did not think about. This is the most common accidental-retention source in CP3 programs, and the reason "it went out of scope" is not a lifetime proof.

## Cycles: three answers

1. **Provable islands are reclaimed automatically.** At compile time, the compiler builds the whole-program strong-reference graph (class fields, inherited statics, arrays, collections, unions, closures at the type level) and computes strongly connected components. A component of size > 1, or a class with a self-edge, is **cycle-capable**; a class is **collector-eligible** only when every strong edge is a walkable handle (a direct class handle or an optional of one) to another eligible class, and no edge is opaque (an interface the compiler cannot enumerate, a type parameter, an opaque object field, a `ConcurrentMap` payload).
2. **Islands that are not eligible are a compile error (CP3401).** "reference cycle ... has no weak edge" is not going away for unprovable cycles — the compiler fails closed rather than leak silently. Eligible islands compile and run with no `weak` annotation.
3. **`weak` fields remain the direct tool** for non-owning back-links: parent/child back-pointers, observers, caches. A `weak` field is `WeakRef<T>` (backed by `std::weak_ptr<T>`); assigning stores a non-owning link; reading yields an optional — `T | undefined` — because the target may be gone. Reading an expired link is not an error; it is `undefined`. A weak field breaks the count chain deterministically and is the right answer when you want prompt release, not just eventual reclamation.

How the cycle collector actually runs (`src/runtime/include/cp3/core/cycle.hpp`): the algorithm is **Bacon–Rajan trial deletion**, one collector per event loop, single-threaded, with no locks and no atomics. When a `CycleRef`'s count drops but does not reach zero, the object becomes a *suspect* (purple). At a loop safe point the collector walks the suspects' reachable set, trial-decrements internal edges, and an island whose external count is still positive is restored; one with no external reference left is freed. A pass is budgeted — at most 4096 visited objects and 200 µs — and is resumable, drained completely at loop exit. Acyclic values never pay for any of this: their handles, layout, and code are unchanged.

## What this model is not

- **Not tracing.** Nothing scans the heap. "Not reachable from my source variable" is not, by itself, what the collector uses — it uses counts and island proofs, and only for classified types.
- **Not refcounting-is-free.** Every handle copy is atomic work (or plain-count work, for `LocalRef`). Hot paths with heavy churn pay for it; use `--show-runtime` and benchmarks before blaming the model.
- **Not escape analysis.** Escape analysis ([next chapter](./03-escape-analysis.md)) can remove the allocation entirely for proven-local objects. It is an optimization; the reference-counting semantics above are the contract.
- **Not destruction-order guarantees.** Sibling objects with no owning relation are not destroyed in a specified order. If cleanup order matters, say so with a scoped API or explicit cleanup.
- **Not thread synchronization.** Atomic counts keep handles safe to copy; they do not make fields safe to mutate concurrently. Cross-thread sharing needs a protocol ([Async non-blocking I/O](./04-async-non-blocking-io.md), thread safety).

## Inspecting the model

For any claim you want to verify about a specific program:

1. Compile with `--show-runtime` and read the generated C++: `Ref<` vs `LocalRef<` vs `CycleRef<` at construction sites tells you which path the compiler chose.
2. Look for `stack_object_` locals — evidence escape analysis fired (see [Escape analysis](./03-escape-analysis.md)).
3. Runtime unit tests pin the collector contract (`src/runtime/tests/cycle_test.cpp`): an unreachable island is freed, a reachable one is not, a weak read after collection is `undefined`, and a budgeted pass resumes where it stopped.

## Related pages

- [Escape analysis](./03-escape-analysis.md) — removing allocations the compiler can prove are local
- [Async non-blocking I/O](./04-async-non-blocking-io.md) — one event loop per thread, and where the collector runs
- [Classes and interfaces](../language/04-values-and-objects/02-classes-and-interfaces.md) — the source-level class rules
- [Weak references and reference cycles](../for-js-developers.md#weak-references-and-reference-cycles) — from the TypeScript side
