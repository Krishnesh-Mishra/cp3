---
title: Compiler optimization and guarantees
---
# Compiler optimization and guarantees

CP3 optimizes only what it can prove, and keeps the ordinary, checked path when it cannot. This page lists the optimizations that can affect your program, what controls them, and how to verify that they happened.

## What the compiler may do for you

| Optimization | What it means for you | Proof it needs |
| --- | --- | --- |
| Reserved capacity on proven append loops | A `while` loop that pushes into one array with a known bound can avoid repeated growth reallocations; `push` behavior and `length` are unchanged. | A recognized counter, bound, increment, and single push site — otherwise the hint is simply not emitted. |
| Stack storage for proven-local objects | Some short-lived class instances and local arrays are stored directly instead of through managed references; observable behavior is unchanged. | Constructor and use-shape proofs; any unsupported construct keeps the ordinary representation. |
| Non-atomic reference counts (`LocalRef`) | Eligible classes in a clean compilation unit can use a cheaper ownership count with identical alias and destruction semantics. | A unit-wide scan: no workers/parallel use, no weak references, no interfaces, no inheritance, nothing exported. One disqualifier reverts all of the unit's classes to the atomic path. |
| Counted-loop `reserve` upper bounds | Conditional pushes in a recognized loop may reserve an upper-bound capacity instead of growing; the hint never changes `length`. | The same loop shape proof plus a provable bound. |
| Release-profile machine optimizations | `-O3`, link-time optimization, `-march=native`-style ISA, loop unrolling, and PGO when a profile exists. | The native compiler's own analysis, applied to the generated C++. |

## What never happens

- No tracing garbage collector is ever inserted, and no pass waits for one.
- No allocation is guaranteed to be stack allocated — "local variable" is not a promise.
- No transformation rewrites your semantics to make a benchmark pass; an unproven proof means the safe path, silently and correctly.

## Escape analysis in one sentence

Objects whose lifetime the compiler can prove are local may be created without allocating the usual reference machinery; everything else stays on the managed path. That is all the source you need — the full model is in [Escape analysis](../internal-engineering/03-escape-analysis.md), and the memory model overall is in [No garbage collector](../internal-engineering/02-no-garbage-collector.md).

## How to check what a build actually did

- Compile with `--show-runtime` to see the generated C++ (`--show` is an alias). Any object the compiler proved local appears as a direct local, not a handle.
- For machine-level questions (vectorized or not?), ask the native compiler for an optimization report and confirm it names the loop in the generated code — then re-check with a repetition-rich benchmark.
- Compare identical workloads with the same profile and toolchain; a best run is not a median, and a median is not a proof.

## When to pick a profile

- Development loop: the default incremental profile (`cp3 run`) — fast cached rebuilds.
- Shipping: `cp3 build` (release).
- Debugging: `--debug`.
- Performance work: fix the profile to release; mixing profiles makes comparisons meaningless.

See [Build profiles and PGO](./11-build-profiles-and-pgo.md) for the flags and [Tests and verification](./08-tests-and-verification.md) for the measurement checklist.

## Related pages

- [Escape analysis](../internal-engineering/03-escape-analysis.md)
- [Build profiles and PGO](./11-build-profiles-and-pgo.md)
- [Generated C++ inspection](./10-generated-cpp-inspection.md)
- [Optimization and arrays, in the language guide](../language/04-values-and-objects/04-arrays.md)
