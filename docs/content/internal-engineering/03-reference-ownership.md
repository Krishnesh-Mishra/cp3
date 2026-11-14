---
title: Reference counts, cycles, and threads
chapter: Chapter 2 · Runtime model
---
# Reference counts, cycles, and threads

CP3 values can have reference semantics: copying a handle shares the object instead of cloning the full object graph. Most reference handles use `Ref<T>`, backed by `std::shared_ptr<T>`. Some eligible user classes use `LocalRef<T>` with a non-atomic count under a conservative compiler gate.

## What copying a reference means

```ts
const first = new RecordValue();
const second = first;
```

The assignment copies a handle. It does not copy the object. Both names refer to the same instance, so a mutation through one alias can be observed through the other.

When the last strong handle is released, the runtime destroys the object. A value can remain alive after one local binding goes out of scope if an array, field, closure, or caller still retains another strong handle.

## The ordinary `Ref<T>` path

The runtime defines `Ref<T>` as `std::shared_ptr<T>`. Its control block uses atomic reference-count operations so strong handles can be copied and released safely across supported thread boundaries. Those atomics protect ownership bookkeeping; they do not protect the object's fields.

`makeRef<T>` constructs the object using `std::allocate_shared` and CP3's `RefPoolAllocator`. The allocator maintains a bounded thread-local free list per allocator specialization. It can reuse released storage on a later allocation; it does not eliminate initial allocation, use a bulk bump arena, or change the atomic shared-reference count.

## Non-atomic `LocalRef<T>` path

The compiler can choose `LocalRef<T>` for a restricted set of non-exported, non-generic classes when a conservative compilation-unit scan finds no worker/parallel boundary, weak-reference use, interface declarations, exported values, or inheritance. Interface implementations and other unsupported class forms keep `Ref<T>`.

`LocalRef<T>` is a runtime-managed shared handle with a plain integer owner count. It preserves aliasing and last-owner destruction. It is **not** a raw pointer, and it does not by itself place the object on the stack. Its control-block storage is also served through the runtime pool allocator.

The safety scan is unit-wide rather than a precise per-reference proof. One disqualifying feature can keep all classes in that unit on the atomic `Ref<T>` path. Do not infer which representation a class uses from syntax alone. Inspect generated C++ from the exact compiler build for `LocalRef`/`makeLocalRef` or `Ref`/`makeRef`.

## Stack storage is a separate optimization

Escape analysis can also replace a few tightly constrained class objects or local object arrays with direct local storage. That optimization can avoid a reference handle and allocation for the candidate it proves. It is separate from `LocalRef`, which still has an allocated control block and shared alias behavior.

The escape-analysis proof is narrow: class shape, fields, constructor, aliases, and uses must all be recognized. An object that escapes or uses unsupported members remains on the reference path. Read [Escape analysis](./02-escape-analysis.md) for positive and negative proof conditions.

## Strong cycles

Reference counting does not reclaim an unreachable cycle when every object in it is held alive by another strong edge:

```text
parent ─strong→ child
   ↑              │
   └────strong────┘
```

Use a weak reference for a non-owning back-link when the relationship permits it. Reading an expired weak link yields the absent value rather than a live object. See [Weak links and reference cycles](./09-reference-cycles-and-weak-links.md).

## Atomic count does not mean synchronized object

An atomic count lets the runtime maintain ownership across independent handles. It does not serialize method calls, protect fields, or make compound operations atomic. Two threads that mutate the same payload still need a synchronization protocol supported by the API.

`LocalRef<T>` uses a non-atomic count and is only selected when the compiler's conservative scan permits thread-confined representation. It must not cross a worker or parallel boundary. Native code and foreign integrations can add sharing the CP3 compiler cannot see, so keep their ownership contracts explicit.

## Allocation and churn costs

Object-heavy workloads can pay for construction, allocation, control-block management, handle copies, container growth, and destruction. These are separate costs:

- `LocalRef` removes atomic count operations for eligible class handles, but still allocates and retains shared ownership semantics.
- The thread-local free list can reuse some allocation blocks, but it does not mean all objects allocate without touching the allocator.
- Stack lowering can remove allocation for a narrowly proven candidate, but it does not optimize arbitrary graphs.
- Array `reserve` can avoid vector capacity growth, but it does not remove each element's own reference representation.

Measure the generated path before deciding which cost dominates. `makeRef`, `makeLocalRef`, local object values, and vector growth mean different things in the generated C++.

## Choosing ownership relationships

For each reference-shaped field, decide whether it owns its target. Strong fields are appropriate for owned children and values that must remain alive. Weak fields are useful for observer links, parent pointers, and caches where the target may disappear.

Avoid using weak references just to reduce reference-count traffic. A weak read can be absent and needs handling. Likewise, copying a strong handle is not a deep copy; use the explicit clone or structured-copy operation when independent object state is required.

## Where to inspect

- `Ref<T>`, `LocalRef<T>`, `RefPoolAllocator`, and allocation helpers: `src/runtime/include/cp3/core/value.hpp`.
- Class-representation and local escape decisions: `src/codegen/src/support/core/escape_analysis.cpp`.
- Generated class handle type: class-model and class codegen files under `src/codegen/src`.

For a performance claim, preserve generated C++ from the measured build and verify the candidate allocation site. A benchmark median cannot tell whether `LocalRef` or stack lowering fired.

## Related pages

- [Value model](./01-value-model.md)
- [Escape analysis](./02-escape-analysis.md)
- [Thread safety](./10-thread-safety.md)
- [No tracing GC](./08-no-tracing-gc.md)
