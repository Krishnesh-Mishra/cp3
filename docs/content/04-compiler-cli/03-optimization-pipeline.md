---
title: Optimization pipeline and guarantees
chapter: Chapter 4 · Compiler and tools
---
# Optimization pipeline and guarantees

CP3 applies compiler transformations only where its analysis can justify them. An optimization should preserve observable language behavior; uncertainty keeps the general checked path.

## Current local escape analysis

The code generator contains a conservative local class escape analysis. It recognizes a narrow subset of simple class constructions and uses, including cases where fields can be represented in local storage. Some local class arrays can also be lowered to direct value storage when the analysis proves the required restrictions.

This is not a whole-program escape analysis, general stack allocation pass, or general scalar replacement of every object. Constructors with unsupported effects, uncertain aliases, escapes, inheritance, weak references, or unknown calls are reasons the proof may not apply. Unrecognized cases retain the regular reference representation.

## Array reserve hints

For selected counted-loop patterns, code generation can reserve capacity for an array before appends. This reduces growth reallocations when the iteration range and append target are proven. It does not change array semantics, and it is not a universal inference that every loop's maximum append count is known.

## Typed arrays and DataView

The compiler includes analysis scaffolding that recognizes a limited fixed-size DataView pattern and records potential fast reads. The per-read code generator does not currently consume those recorded facts. Therefore the compiler does not yet establish that the DataView helper call is removed or that the loop is SIMD-vectorized.

To claim vectorization, inspect the generated benchmark C++ and verify a backend report tied to that source loop. A report about `shared_ptr` or another standard-library header is unrelated to the numeric loop. See [Typed arrays, buffers, and DataView](#03-standard-library/16-typed-arrays).

## No general LocalRef or arena allocator

CP3's general `Ref<T>` representation uses `std::shared_ptr<T>` and `makeRef` uses `std::make_shared`. The current compiler does not generally replace all proven thread-confined references with a non-atomic `LocalRef`, and the runtime does not provide a general thread-local bump arena for ordinary class allocations.

Some objects can avoid this representation through narrow local lowering, but ordinary heap references still use the shared reference representation. See [Reference ownership](#02-runtime/03-reference-ownership).

## Measure claims against generated output

Benchmark summaries depend on warmup, machine load, compiler version, and repetition count. A best run is not the median, and a noisy sample should not support an optimization claim. Compare identical workloads, verify output, preserve the run metadata, and inspect generated code for the transformation being discussed.

## Separate source proofs from backend optimization

CP3 front-end and codegen passes can expose simpler C++ to Clang, remove supported runtime work, or provide range/capacity facts. The native compiler then applies its own inlining, loop, vector, and machine-specific optimizations. These are separate layers: an optimization report may describe a runtime helper rather than the source loop, and `-O3` cannot always recover a proof hidden by calls or aliasing.

## Current transformations and their boundaries

| Transformation | Current scope | What to inspect |
| --- | --- | --- |
| Local escape analysis | Restricted class and local-array patterns with known fields/uses | Whether `makeRef` disappeared at the candidate site |
| Counted-push reserve | Recognized counted loops with one push pattern | Reserve helper before loop; logical length remains unchanged |
| Parallel reduction analysis | Supported pure reduction shapes | Reduction facts and fallback for effectful/unsupported bodies |
| Typed-array range facts | Narrow loop/index patterns | Whether the relevant checked read actually consumes the fact |
| DataView fast-read facts | Analysis records limited candidates | Facts are not currently consumed by per-read lowering |
| Local non-atomic references | Not generally implemented | Ordinary `Ref<T>` remains `std::shared_ptr<T>` outside local elimination |
| General thread-local arena | Not generally implemented | Escaping allocations still use ordinary runtime allocation |

An optimization's existence in a source file does not mean every syntactically similar loop triggers it. Check each proof condition and emitted use.

## Fail-closed behavior

A proof-based transformation must preserve the ordinary checked behavior when aliases, side effects, bounds, lifetime, or thread ownership are uncertain. Extending a pass requires a positive case and negative cases for the boundaries that must keep the fallback. Avoid turning one benchmark-shaped example into a hard-coded rewrite.

## Establishing vectorization

To prove SIMD, inspect the generated source for the benchmark loop and request compiler vectorization diagnostics from the selected Clang/GCC version. The report must cite the generated loop's source location and vector width. A report about standard-library internals, a helper function, or a basic block elsewhere is not proof of the target loop's vectorization.

A report of “missed” vectorization should be tied to an actual missed-reason message. Then inspect whether the loop has helper calls, bounds checks, aliasing, uncertain stride, or control flow that prevents the transformation. Do not guess the remaining cost from the benchmark median alone.

## Establishing allocation removal

For object churn, inspect whether eligible objects use local storage instead of `makeRef`/`std::make_shared`, and whether the kept array still contains shared handles. For array growth, inspect reserve placement and element movement. A reserve hint cannot remove per-object allocation; stack lowering cannot eliminate a vector reallocation unless the array proof also applies.

## Build profile matters

Release flags, target ISA, LTO, loop unrolling, and PGO influence native output. Compare the same build profile and toolchain version. Preserve the generated C++ from the exact binary measured; do not profile one profile and present results from another.

## Performance report checklist

Report the benchmark name and unchanged source status, expected output, exact compiler build/profile, repetitions, median and variation, generated-code evidence, and the compiler report location. If performance misses the target, isolate which transformation did not fire or which work remains rather than describing the whole pipeline as optimized.

## Related pages

- [Escape analysis](#02-runtime/02-escape-analysis)
- [Allocation and arrays](#02-runtime/05-allocation-and-arrays)
- [Typed arrays](#02-runtime/06-typed-arrays)
- [Build profiles and PGO](#04-compiler-cli/11-build-profiles-and-pgo)
