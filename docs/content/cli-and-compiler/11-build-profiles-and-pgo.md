---
title: Build profiles and PGO
chapter: Chapter 4 · Compiler and tools
---
# Build profiles and PGO

CP3's build profile determines which optimization and debug settings the native Clang toolchain receives. The same CP3 source can have different compile times, binary layouts, and runtime speeds under different profiles. Always record the profile when comparing measurements.

## Default release build

`cp3 build` defaults to the release profile. The CLI describes the default as `-O3`, link-time optimization, target-native ISA, loop unrolling, and PGO when a profile exists. PGO is conditional: a release build does not invent representative workload data merely because the profile is selected.

Target-native ISA flags can make an executable depend on CPU features of the machine used to build it. Do not distribute such a binary as universally portable without considering the deployment target. Use `--no-native` to disable this part of the release profile where supported.

## Incremental profile

`--incremental` is intended for fast cached development builds and disables LTO/PGO according to the current CLI contract. It is useful for short edit/build cycles, but its timings should not be compared directly with release-mode benchmark results.

The incremental profile does not mean the compiler deletes and recreates every object file. Cache keys determine which compile products can be reused.

## Debug profile

`--debug` uses `-O0` and debug information. Use it for source-level debugging and to keep native optimization from obscuring generated control flow. It is not a representative performance profile.

## Release overrides

The CLI exposes release controls including:

- `--no-lto` to disable link-time optimization.
- `--no-native` to avoid target-specific ISA selection.
- `--no-unroll` to disable explicit loop unrolling.
- `--strict-fp` to retain math error/errno behavior that may inhibit some transformations.
- `--opt-level <n>` to select an optimization level from the documented range.
- `--no-pgo` to disable automatic profile use.

These options affect the native compiler invocation. Inspect `cp3 help` in the build being used; older versions can differ.

## Profile-guided optimization

PGO has two distinct phases. Instrumentation builds collect execution counts for a representative workload. A later optimized build consumes the resulting profile. A profile trained on a different input distribution may mislead inlining and branch decisions.

The CLI exposes `--pgo <file>` to consume a Clang `.profdata` file and `--pgo-generate[=<dir>]` to request an instrumentation build. `--pgo-generate` requires the release profile: the CLI rejects it when `--release` is not in effect (the error message is "--pgo-generate requires the release profile (use --release)"), the `--pgo` and `--pgo-generate` flags are mutually exclusive, and `--pgo-generate=<dir>` requires a non-empty directory. Use the same native toolchain family/version expected by the profile workflow, exercise representative cases, and record the training command and input.

PGO cannot make an unsafe transformation correct and does not replace CP3 semantic analysis. It optimizes native decisions using observed execution data.

## Benchmark requirements

For a fair CP3 comparison, rebuild the compiler if compiler source changed, then compile the benchmark with the same CP3 profile used for the recorded result. Keep the benchmark source and output oracle unchanged. Record Clang version, profile flags, repetition count, and run variation.

Do not compare a cached incremental run against another language's optimized release binary and conclude the language is intrinsically slower. State whether the measurement includes compilation, startup, JIT warm-up, or only steady-state execution.

## Related pages

- [Native toolchains](./05-native-toolchains.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Tests and compiler verification](./08-tests-and-verification.md)
