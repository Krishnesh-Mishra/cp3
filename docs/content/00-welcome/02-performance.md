---
title: Performance, measured carefully
chapter: Welcome
---
# Performance, measured carefully

CP3 can be competitive with C++ and Rust on some workloads, and substantially slower on others. The benchmark suite exists to show both outcomes. A benchmark result is useful only with its source, toolchain, run count, output check, and variation visible.

## Examples from the recorded suite

The following figures are from the complete run recorded in the repository file `benchmarks/results.md`, dated **28 October 2026** on one Windows 11 x64 machine. These are measurements from that run, not universal rankings.

| Workload | CP3 median | C++ median | Rust median | What the run showed |
| --- | ---: | ---: | ---: | --- |
| `compute-single` | 191.1 ms | 202.8 ms | 184.9 ms | CP3 was about 6% faster than C++ and about 3% slower than Rust. |
| `compute-single-int` | 191.1 ms | 206.2 ms | 189.6 ms | CP3 was about 8% faster than C++ and about 1% slower than Rust. |
| `string-churn` | 235.7 ms | 2145.7 ms | 354.6 ms | CP3 was much faster in this specific string workload. |
| `health-server` | 19,038 req/s | 19,741 req/s | 20,087 req/s | All three were close in this one local server run. |

The suite also recorded larger gaps. In the same run CP3 took 253.9 ms for `buffer-copy`, versus 17.41 ms for Rust; `object-churn` took 299.7 ms, versus 70.06 ms for Go and roughly 150 ms for C++ and Rust. These are active performance problems, not details to hide from the introduction.

## A later noisy sample

On 30 October 2026, a seven-repetition run reported a `typed-array-sum` median of 21.19 ms for CP3, 25.52 ms for C++, and 31.21 ms for Rust. The same table reported a **107.6% coefficient of variation** for CP3. `object-churn` reported 42.26 ms for CP3, 142.3 ms for C++, and 149.9 ms for Rust, but CP3's variation was **81.2%**. Those medians are not stable evidence that CP3 has beaten either compiler on these workloads. The best and worst samples were far apart, so a quiet rerun with lower variation is needed before drawing that conclusion.

These rows are included to make the measurement history visible. A compiler optimization claim should be tied to repeatable timings and inspected generated code, not to one unusually fast sample.

## Reading the measurement columns

The suite checks program output against an oracle before reporting a timing. It records a median, best time, p95, coefficient of variation (CV), resident memory, and binary size. Different workloads measure different things: process startup, numeric loops, allocations, hashing, filesystem calls, or HTTP request handling. Read the task description before comparing two rows; a server throughput result and an arithmetic-loop latency result say nothing about the same cost.

| Column | What it tells you | What not to infer |
| --- | --- | --- |
| Median | The middle observed runtime among measured repetitions. | It is not a confidence interval and does not remove background-load effects. |
| Best | The fastest repetition. | It is not the representative steady-state result. |
| p95 | A high-end latency estimate from the sample. | With very few repetitions, it is a coarse statistic. |
| CV | Standard deviation relative to the mean, shown as a percentage. | A high CV does not identify its cause; it means the sample is unstable. |
| RSS | Process resident memory as observed by the runner. | It is not peak allocation volume or a language-wide memory guarantee. |
| Binary size | The measured native executable size where available. | It does not include every runtime or system dependency. |

Always retain the runner's output-verification line with the timing. A fast result for a program that computed a different answer is not an optimization.

A result with high run-to-run variation should not be treated as a stable win. A single best run is not enough to support a performance claim. Compiler flags, generated C++, host load, power management, process scheduling, and runtime warmup can all change the outcome. When one implementation's CV is much higher than the others, compare the raw repetitions before interpreting a close median.

The benchmark suite is a collection of workloads, not a single score. A language can be ahead on a math loop and behind on allocation churn because the programs stress different layers: generated instructions, runtime helpers, allocator behavior, reference ownership, data representation, or startup cost. Describe the exact workload and measurement instead of saying that one language is universally faster.

## Reproducing a fair comparison

For a useful comparison, keep the following inputs fixed:

1. Use the same benchmark task and do not edit its source or expected output.
2. Use the same repetition count and workload scale for each language.
3. Record the CP3 compiler executable and build identity used by the runner.
4. Record native compiler versions, operating system, CPU, and machine load.
5. Run on an otherwise quiet machine; avoid overlapping compiler builds, test suites, and benchmark clients.
6. Check that every implementation produced the oracle result.
7. Report median, variation, and the raw run count; include best and p95 as context.

If variation is high, do not tune the performance gate around the current median. First remove competing work, increase repetitions, and rerun. If variation stays high, investigate why the workload has different fast and slow modes; do not choose the best sample as the answer.

## Reproduce the measurements

Run a focused workload from the repository root:

```sh
python benchmarks/runner/run_benchmark.py --only compute-single --reps 10
```

Run more than one named workload by separating names with commas. The runner lives under `benchmarks/runner`, and each language implementation lives beside the others under `benchmarks/cases`.

For example, to focus on the two workload families discussed above:

```sh
python benchmarks/runner/run_benchmark.py --only typed-array-sum,object-churn --reps 10
```

Use `--only` for focused diagnosis. The full suite is useful for a broad performance report, but it costs more time and can make iteration slower. The repository file `benchmarks/README.md` lists supported selectors, output, and runner behavior.

## From source code to a performance claim

CP3 compiles source to C++, then the selected native compiler builds an executable. A source-level optimization claim therefore needs evidence at the stage it is meant to affect:

| Claim | Evidence to inspect |
| --- | --- |
| An object became stack storage | Generated C++ uses a local value and does not retain a heap-owning handle for that object. |
| An array reserves capacity once | The generated function places the reserve call before the eligible loop and preserves logical length. |
| A typed-array loop is bounds-check-free | The actual generated hot loop uses the proven raw access; a separate analysis fact is not enough. |
| A loop vectorizes | The backend optimization remark points to that generated loop and reports successful vectorization. |
| A refcount optimization applies | Generated handles use the expected representation (`LocalRef` or ordinary `Ref`) for that class in that translation unit. |

`-O3` or a native executable alone proves none of these. A compiler can optimize a runtime helper or standard-library block without transforming the benchmark's loop. Match diagnostic paths and line numbers to the generated benchmark source; ignore unrelated remarks in headers when establishing a workload-specific claim. See [Generated C++ inspection](../04-compiler-cli/10-generated-cpp-inspection.md) and [Optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md).

## Server measurements

HTTP benchmarks measure a service plus its request path, event loop, framework or runtime, and load generator. The runner uses keep-alive clients and reports request throughput; concurrency, warmup, duration, and latency-probe settings influence the result. Compare the same server task under the same settings and inspect the exact command. A single local server run is a snapshot, not a stable ranking.

The benchmark suite includes both single-server and multicore variants. The single-server row emphasizes one process under a defined request load. The multicore row exercises the language's configured parallel server mode. Do not compare one language's single-core result to another language's multicore result as if they were equivalent.

## Native compilation is not an optimization guarantee

## What “native” does and does not mean

Native code gives the backend optimizer access to the generated C++, but it does not guarantee SIMD, stack allocation, or an optimal algorithm. The optimizer can only exploit what it can prove from the generated loop and runtime calls. The [optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md) documents which CP3 passes exist; it distinguishes implemented behavior from future work.

## Links

- Latest recorded benchmark results: `benchmarks/results.md` in the repository.
- Benchmark suite, tasks, and options: `benchmarks/README.md` in the repository.
- [Compiler optimization pipeline](../04-compiler-cli/03-optimization-pipeline.md)
- [Inspect generated C++](../04-compiler-cli/10-generated-cpp-inspection.md)
