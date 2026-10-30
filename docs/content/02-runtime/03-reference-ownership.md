---
title: Reference counts, cycles, and threads
chapter: Chapter 2 · Runtime and memory
---
# Reference counts, cycles, and threads

CP3’s ordinary strong reference is `std::shared_ptr`. Copies share an object and update its reference count. This is convenient for aliasing but has a real cost on allocation-heavy paths.

## What a `Ref<T>` copy means

```ts
const first = new RecordValue();
const second = first;
```

The assignment copies a handle. It does not copy the object graph. Both names refer to the same instance, so mutation through one alias may be visible through the other.

In the C++ runtime representation, copying a `std::shared_ptr` increments a shared reference count and destroying a handle decrements it. Standard library implementations use atomic operations for the shared control block. The payload fields are not made atomic by the shared pointer.

## There is no general non-atomic ref path today

The compiler does not currently replace an ordinary `Ref<T>` with a custom local, non-atomic reference count. Escape analysis can avoid ownership entirely for a restricted local object or value-array pattern; otherwise the generated code uses the shared representation. These are different optimizations.

Do not describe a CP3 program as having “non-atomic local refs” unless the generated code and runtime show that representation. In this checkout, `Ref<T>` remains `std::shared_ptr<T>`.

## Strong cycles

Reference counting alone cannot reclaim a cycle whose members keep one another strongly referenced:

```text
parent ─strong→ child
   ↑              │
   └────strong────┘
```

When an object graph needs a back-reference, use a weak reference when the language and field type permit it. A weak link does not own the target; attempting to read an expired weak link yields the absent value described by the runtime.

## Threads and workers

An atomic reference count protects the count update, not concurrent access to every field in the object. Sharing a `Ref<T>` across threads does not make simultaneous field mutation safe. CP3’s worker and parallel checks constrain which captures and writes are accepted, but native interop can still require careful synchronization.

## Allocation costs

The normal `makeRef<T>` path uses `std::make_shared`, which typically allocates the object and control block together. The allocation, initialization, reference-count traffic, and eventual destruction all contribute to churn. The standard allocator is not currently replaced by a CP3 thread-local arena for general escaping allocations.

For hot code, first determine whether the object can be represented as a primitive or reused value. If it cannot, measure the actual allocator and ownership costs before adding a new runtime representation.

## Follow the owning handle

For every reference-shaped value, ask which strong binding keeps it alive. A local variable may own the value; an array or class field may retain another handle; a returned value may extend ownership into the caller. Dropping one local name does not destroy an object while another strong path remains.

```ts
const first = new RecordValue();
const second = first;
// Both names keep the same instance alive.
```

This is handle sharing, not a deep copy. If `RecordValue` contains mutable fields, a write through either alias changes the same instance. Copying the handle does not recursively clone its object graph.

## Construction, copying, and destruction costs

The normal runtime allocation helper calls `std::make_shared<T>`. This generally combines allocation of the class object and its control block into one allocation, then initializes the object. Each additional strong handle participates in shared-count bookkeeping; dropping the final strong handle destroys the payload and releases the control block.

In a tight loop, separate the cost categories before optimizing: object allocation, constructor work, handle copies, vector growth, field access, and destruction. Escape analysis can remove the shared ownership path only for its supported local shapes. Array reservation only changes container capacity. They solve different costs.

## Why the count is atomic

The standard shared-pointer control block supports reference-count updates by independent threads. Those atomic operations are for control-block lifetime. The program's fields are not guarded by the reference count, and concurrent reads/writes still need a valid synchronization rule.

An atomic reference count is not equivalent to a mutex around the object, and it does not serialize method calls. If two threads mutate one class instance, protect that state with a supported protocol or redesign around messages/reductions.

## Weak references and cycle structure

A strong cycle is a graph in which every remaining object is kept alive by another strong edge. A weak edge avoids contributing to the target's strong count. Reading a weak class field yields an optional live handle or `undefined`; test for the missing case before using the result.

Weak fields are intended for non-owning links such as parent pointers. They are not suitable for a field that must keep its target available. Read [Weak links and reference cycles](#02-runtime/09-reference-cycles-and-weak-links) for exact syntax and tested behavior.

## Current optimization boundaries

The compiler can represent a small set of non-escaping class values directly in local storage. That can avoid creating a shared pointer altogether. It is not a replacement `LocalRef`; objects that need shared lifetime retain `Ref<T>`. There is no general thread-local arena for escaping objects in this runtime.

When investigating performance, inspect the specific generated function. Search for `makeRef`/`std::make_shared`, count relevant handle-producing expressions, and confirm whether the candidate was transformed. Do not infer a fast reference path from the presence of escape-analysis code in the compiler.

## Where to inspect

`Ref<T>`, `WeakRef<T>`, and `makeRef<T>` are defined in `src/runtime/include/cp3/core/value.hpp`. The local-object analysis is in `src/codegen/src/support/core/escape_analysis.cpp`. Semantic worker/parallel rules live in `src/sema`; the runtime count itself does not establish that a payload transfer is legal.

## Related pages

- [Value model](#02-runtime/01-value-model)
- [Escape analysis](#02-runtime/02-escape-analysis)
- [Thread safety](#02-runtime/10-thread-safety)
- [No tracing GC](#02-runtime/08-no-tracing-gc)
