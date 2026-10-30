---
title: Chapter 2 · Runtime model
chapter: Chapter 2 · Runtime model
---
# Chapter 2 · Runtime model

This chapter explains what CP3 values become after type checking: which values are copied, which values refer to shared storage, how closures retain captures, and what the runtime does when references are released. These details matter for memory use, concurrency, and performance, but C++ representation is not itself the source-language contract.

## Runtime pages

| Page | Question it answers |
| --- | --- |
| [Value model](#02-runtime/01-value-model) | Which CP3 values are primitive-like and which are reference-shaped? |
| [Escape analysis](#02-runtime/02-escape-analysis) | Which local allocations can the compiler currently eliminate or store locally? |
| [Reference ownership](#02-runtime/03-reference-ownership) | How do strong references keep objects alive and what does copying a handle cost? |
| [Closures](#02-runtime/04-closures) | What data a closure retains and how captures can outlive their creating scope. |
| [Allocation and arrays](#02-runtime/05-allocation-and-arrays) | How ordinary arrays grow and what capacity reservation can save. |
| [Typed arrays](#02-runtime/06-typed-arrays) | How typed views share fixed-width binary storage. |
| [Async and concurrency](#02-runtime/07-async-and-concurrency) | How promises, event loops, workers, and parallel tasks are scheduled. |
| [No tracing GC](#02-runtime/08-no-tracing-gc) | What the absence of a tracing collector means in practice. |
| [Cycles and weak links](#02-runtime/09-reference-cycles-and-weak-links) | How strong cycles retain objects and how weak references change ownership. |
| [Thread safety](#02-runtime/10-thread-safety) | Which state may be shared and what synchronization is still required. |
| [Inspect runtime costs](#02-runtime/11-inspecting-runtime-costs) | How to trace allocations, generated helpers, and measurements. |

## Three levels to keep separate

When reasoning about a runtime behavior, distinguish three layers:

1. **Language behavior:** what CP3 source observes, such as array aliasing or an out-of-range typed-array read.
2. **Runtime representation:** the C++ object or helper currently used to implement that behavior.
3. **Optimization:** a compiler or C++ backend transformation that may remove some work while preserving the behavior.

For example, assigning one array handle to another means both names refer to the same array. The current general representation uses a shared reference. A local optimization might replace storage for a proven case, but it cannot change the aliasing that CP3 source observes.

## Values and lifetime

Numbers, booleans, and several native scalar forms are represented as values. Arrays, class instances, many object shapes, and closures may own or refer to runtime storage. Copying a primitive copies the value; copying a reference-shaped value generally copies a handle and extends or shares ownership of its target.

An object is eligible for a local transformation only if all uses and effects satisfy the analysis's proof conditions. Returning it, storing it into longer-lived state, capturing it in a closure, passing it to an unknown function, or sending it to another thread can extend its required lifetime. Read [Escape analysis](#02-runtime/02-escape-analysis) for the current implementation boundaries; do not assume that “local variable” means “stack allocation.”

## Reference counting and cycles

The general `Ref<T>` representation is backed by `std::shared_ptr<T>`. Strong references keep their target alive. Releasing the final strong reference destroys an acyclic object and releases the references held in its fields.

Reference counting does not discover unreachable cycles. If objects strongly own one another, their counts can remain nonzero after the program loses its last external handle. Weak links can represent a relationship that should not own the target; see [Cycles and weak links](#02-runtime/09-reference-cycles-and-weak-links).

The current general runtime does not replace all shared references with a non-atomic counter and does not route all class allocations through a thread-local arena. Narrow local codegen transformations exist, but their scope is described on the escape-analysis page.

## Concurrency and lifetime

Async work, worker threads, and `parallel.*` tasks have different scheduling and ownership rules. Capturing a reference can keep an object alive until a continuation or worker releases it. A shared reference count does not make the referenced object's fields safe for simultaneous mutation.

Read [Async and concurrency](#02-runtime/07-async-and-concurrency) for scheduling and [Thread safety](#02-runtime/10-thread-safety) for ownership and synchronization. Use explicit coordination when multiple threads can access mutable state.

## Performance investigation path

For a suspected runtime cost:

1. Identify the operation in CP3 source and the benchmark/test input that exercises it.
2. Follow the codegen route and see which runtime helper or representation is emitted.
3. Inspect the generated C++ from the exact compiler build and profile.
4. Check whether an optimization actually changed that code.
5. Measure repeated runs and retain variation and output validation.

An optimization pass can exist but fail to trigger; a type fact can exist but not be consumed by codegen; backend vectorization can apply to a runtime header rather than the benchmark loop. See [Inspecting runtime costs](#02-runtime/11-inspecting-runtime-costs) and [Generated C++ inspection](#04-compiler-cli/10-generated-cpp-inspection).

## Related chapters

- [Chapter 1 · The language](#01-language/00-overview)
- [Chapter 3 · Standard library](#03-standard-library/00-overview)
- [Chapter 4 · Compiler and tools](#04-compiler-cli/00-overview)
