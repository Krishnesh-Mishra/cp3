---
title: Chapter 2 · Runtime and memory
chapter: Chapter 2 · Runtime and memory
---
# Chapter 2 · Runtime and memory

CP3 source is translated to C++, then compiled into a native executable. The CP3 runtime supplies reference handles, arrays, strings, promises, event-loop tasks, typed-array storage, and standard-library support.

## Pages in this chapter

- [The CP3 value model](#02-runtime/01-value-model)
- [Escape analysis and stack storage](#02-runtime/02-escape-analysis)
- [Reference counts, cycles, and threads](#02-runtime/03-reference-ownership)
- [Closure storage and captured values](#02-runtime/04-closures)
- [Allocation, arrays, and object churn](#02-runtime/05-allocation-and-arrays)
- [Typed arrays and DataView](#02-runtime/06-typed-arrays)
- [Async and concurrency](#02-runtime/07-async-and-concurrency)
- [Garbage collection: what CP3 does not provide](#02-runtime/08-no-tracing-gc)
- [Weak links and reference cycles](#02-runtime/09-reference-cycles-and-weak-links)
- [Thread safety and ownership boundaries](#02-runtime/10-thread-safety)
- [Inspecting runtime costs](#02-runtime/11-inspecting-runtime-costs)

## Keep language guarantees separate from optimizations

CP3’s value semantics must hold whether or not an optimization fires. Escape analysis and loop lowering are conservative transformations: when the compiler cannot prove a transformation safe, it keeps the ordinary runtime representation. A successful build does not imply every object was stack-allocated, every loop was vectorized, or every reference count was removed.

## Runtime implementation facts

In the current runtime, `Ref<T>` is an alias for `std::shared_ptr<T>`, and the canonical `makeRef<T>` calls `std::make_shared<T>`. This means ordinary reference copies use shared ownership with atomic reference counts. CP3 does not currently replace general references with a custom non-atomic `LocalRef`, nor does it route all small allocations through a thread-local bump arena.

The code generator does have local escape analysis for a restricted set of objects and arrays. Its exact eligibility rules are documented on the [escape analysis page](#02-runtime/02-escape-analysis).
