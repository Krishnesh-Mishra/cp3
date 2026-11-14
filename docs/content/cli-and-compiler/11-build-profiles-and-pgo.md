---
title: Build profiles and PGO
---
# Build profiles and PGO

The build profile decides which optimization flags the native compiler receives and what the incremental cache is allowed to reuse. Two commands behave differently out of the box: **`cp3 build` produces a release binary**, while **`cp3 run` and `cp3 warm` use the fast cached incremental profile**. Record the profile with any number you intend to compare.

## The three profiles

| Profile | Flag | Flags CP3 passes | Cache | Use it for |
| --- | --- | --- | --- | --- |
| Incremental (default for `cp3 run`, `cp3 warm`) | `--incremental` | `-O0`, per-module translation units, a precompiled header, no LTO/PGO | Used across runs | Edit/build/restart loops |
| Debug | `--debug` | `-O0 -g` plus `-ffunction-sections` off (gc-sections omitted) | Used | Source-level debugging |
| Release (default for `cp3 build`) | `--release` | `-O3 -flto`, `-march=native` (`-mcpu=native` on ARM), `-funroll-loops`, `-fno-math-errno` (unless `--strict-fp`), `-fno-plt` on Linux, `-ffunction-sections -fdata-sections` | **Ignored — `.cp3/build` and `.cp3/cache` are wiped first** | Shipping; benchmark numbers |

The release profile is a clean rebuild, always: it never reuses cached objects and never carries a stale cache into the binary. If you want the release binary to reflect exactly the current sources, `cp3 build` is the answer; incremental output exists to keep development fast, not to double as a shipping artifact.

## Release knobs

| Flag | Effect |
| --- | --- |
| `--no-lto` | Disable link-time optimization. |
| `--no-native` | Drop the `-march=native` / `-mcpu=native` flag (portable binaries). |
| `--no-unroll` | Disable `-funroll-loops`. |
| `--strict-fp` | Keep math errno behavior that `-fno-math-errno` otherwise drops. |
| `--opt-level <0-3\|s\|z>` | Override the `-O` level. |
| `--no-pgo` | Disable automatic consumption of `.cp3/pgo/default.profdata`. |
| `--max-memory <MB\|N%>` | Cap codegen-phase memory (default: 70% of system RAM). |

## Incremental builds and caching

Incremental builds split the program into per-module translation units under a shared header, compile them independently, and reuse prior object files when nothing they depend on changed. A precompiled header speeds up the always-included headers; a `link-stamp` avoids relinking when nothing linked moved. `cp3 warm` runs all of this eagerly so a long session never pays the first-build cost mid-edit. This is the profile you feel during development.

Do not compare incremental timings against another language's optimized release binary — different workloads entirely.

## Debug builds

`--debug` emits `-O0 -g`: unoptimized code with symbols. Function/data sections are kept for the debugger. It exists so the debugger sees what you wrote — never use it for measurements.

## Profile-guided optimization (PGO)

PGO has two phases, both explicitly requested:

1. **Instrument** — build with `cp3 build --pgo-generate=<dir>` (a non-empty directory is required), then exercise a representative workload.
2. **Consume** — build again with `cp3 build --pgo <file.profdata>`.

Rules worth memorizing:

- `--pgo-generate` requires the release profile; the CLI rejects it otherwise (`--pgo-generate requires the release profile (use --release)`).
- `--pgo` and `--pgo-generate` are mutually exclusive.
- Release builds **automatically** use `.cp3/pgo/default.profdata` when it exists — unless `--no-pgo` is passed.
- PGO optimizes native decisions using observed execution data; it cannot make an unsafe transformation correct and it does not replace semantic analysis.
- Train on representative input, or you will tune your binary for a benchmark you do not run.

## Picking one

| Situation | Profile |
| --- | --- |
| Development / edit-rebuild-restart | incremental (default for `cp3 run`/`dev`) |
| Shipping, release checks | release (default for `cp3 build`) |
| Debugging with symbols | debug |
| Benchmark comparisons | release, same flags, same machine |

## Benchmark discipline

Use the same profile, toolchain, and machine for both sides of a comparison. Keep the workload and the output oracle unchanged; rerun enough times to see variation; state whether timing includes startup, compilation, or steady state only. A fast run is not the median, and a median is not a proof.

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Native toolchains](./05-native-toolchains.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Tests and verification](./08-tests-and-verification.md)
