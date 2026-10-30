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

## How to read a result

The suite checks program output against an oracle before reporting a timing. It records the median, best time, p95, coefficient of variation, resident memory, and binary size. Different workloads measure different things: process startup, numeric loops, allocations, hashing, filesystem calls, or HTTP request handling.

A result with high run-to-run variation should not be treated as a stable win. A single best run is not enough to support a performance claim. Compiler flags, generated C++, host load, and runtime warmup can all change the outcome.

## Reproduce the measurements

Run a focused workload from the repository root:

```sh
python benchmarks/runner/run_benchmark.py --only compute-single --reps 10
```

Run more than one named workload by separating names with commas. The runner lives under `benchmarks/runner`, and each language implementation lives beside the others under `benchmarks/cases`.

## What “native” does and does not mean

Native code gives the backend optimizer access to the generated C++, but it does not guarantee SIMD, stack allocation, or an optimal algorithm. The optimizer can only exploit what it can prove from the generated loop and runtime calls. The [optimization pipeline](#04-compiler-cli/03-optimization-pipeline) documents which CP3 passes exist; it distinguishes implemented behavior from future work.
