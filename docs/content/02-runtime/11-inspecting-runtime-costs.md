---
title: Inspecting runtime costs
chapter: Chapter 2 · Runtime and memory
---
# Inspecting runtime costs

CP3 source types do not tell the whole performance story. A short expression can lower to a helper call, a reference-counted copy, a bounds check, or an allocation. To explain a cost, follow the program through generated C++ and the native compiler rather than inferring behavior from syntax alone.

## Start with a workload and a claim

Name the exact operation being measured: object construction, array growth, typed-array access, a string conversion, or a call boundary. State what you expect to disappear or become cheaper. For example: “the counted loop should reserve the destination vector once before the first push.” Avoid vague claims like “escape analysis makes objects stack allocated” unless the generated code for this source proves it.

## Preserve the measurement input

Do not edit `benchmarks/cases/` when measuring a compiler optimization. The runner's oracle checks output; a changed source can invalidate both correctness and performance comparisons. Use the named benchmark selector and repetitions supported by the runner:

```powershell
python benchmarks/runner/run_benchmark.py --only object-churn --reps 7
```

Record compiler versions, build profile, CPU, OS, median, best, p95, and coefficient of variation. High variation means the sample may include scheduling, frequency, or background-load effects. A best run is not a stable speedup.

## Preserve the exact generated runtime

For CP3 applications, use `--show-runtime` (`--show` is an alias) to copy generated C++ and the runtime headers/translation units used by the build into the project build tree. Inspect the files corresponding to the benchmark's source module, not unrelated generated support code.

For allocation work, search the hot function for `makeRef`, `makeLocalRef`, `Ref`/`LocalRef` handle copies, vector growth, and helper calls. `makeRef` uses `std::allocate_shared` with CP3's pool allocator. For numeric loops, inspect loop control, memory accesses, bounds helpers, and calls. Match generated source filenames and line numbers to backend optimization reports.

## Proving vectorization

A compiler report about a standard-library header or a generated helper does not prove that the benchmark loop vectorized. Use the native compiler's optimization remarks and make sure the report identifies the generated source loop. Confirm the generated loop has the memory form and control flow required by the vectorizer, then check the report's width and whether it says vectorization succeeded or was missed.

Do not infer SIMD solely from a faster median, a compiler flag such as `-O3`, or a generic report line saying some basic block was vectorized. Report which source loop was transformed and which operation remains scalar.

## Escape-analysis evidence

For an eligible local object, inspect whether codegen replaced the shared allocation with a local object or value storage. Check that no alias or return still requires the original heap lifetime. For an ineligible object, verify that it retains the checked shared path. A compiler pass existing in the source is not evidence that a particular source case met its proof conditions.

## Separating compile-time from runtime cost

Benchmark timing should state whether it measures only program execution or includes compilation and process startup. Compare identical runner modes and input sizes across languages. A cold build, cache hit, JIT warmup, and steady-state executable are different measurements.

The CP3 runner reports per-language results and output verification. Preserve the command and raw result when making a performance claim; do not update expectations merely to hide a gap.

## Useful source locations

- Compiler transformations: `src/codegen/src/emit` and `src/codegen/src/support`.
- Semantic range and type facts: `src/sema`.
- Runtime ownership and typed storage: `src/runtime/include` and `src/runtime/src`.
- Benchmark input and runner: `benchmarks/cases` and `benchmarks/runner`.

Trace a cost from source to semantic fact, generated C++, runtime helper, and native instruction report. If a stage does not emit or consume the expected fact, report that missing link instead of attributing the result to an optimization by name.

## Related pages

- [Optimization pipeline and guarantees](#04-compiler-cli/03-optimization-pipeline)
- [Escape analysis and stack storage](#02-runtime/02-escape-analysis)
- [Allocation, arrays, and object churn](#02-runtime/05-allocation-and-arrays)
- [Typed arrays and DataView](#02-runtime/06-typed-arrays)
- [Performance, measured carefully](#00-welcome/02-performance)
