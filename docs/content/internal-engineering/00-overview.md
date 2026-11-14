---
title: Internal Engineering
---
# Internal Engineering

This section explains how CP3 works under the hood: how the compiler builds your program, how values live and die, and what the runtime is made of. Read it if you contribute to the compiler, investigate performance, or want to know exactly what your code becomes.

The user guides describe what CP3 does. This section describes why and how — with the implementation map (files in `src/`, decisions, and invariants) attached to every claim. When this text and the source disagree, the source is right; please file the correction.

## The five chapters

| # | Chapter | The question it answers |
| --- | --- | --- |
| 1 | [Compiler design](./01-compiler-design.md) | What happens between your `.cp3` file and a native executable? |
| 2 | [No garbage collector](./02-no-garbage-collector.md) | How are objects kept alive and released without a tracing collector? |
| 3 | [Escape analysis](./03-escape-analysis.md) | When can the compiler skip the heap and reference counting entirely? |
| 4 | [Async non-blocking I/O](./04-async-non-blocking-io.md) | How does `async`/`await`, the event loop, and the I/O reactor actually work? |
| 5 | [Runtime libraries](./05-runtime-libraries.md) | Which native libraries CP3 stands on, and why each one is there |

## How to read these pages

Every chapter follows three rules:

1. **Claims are anchored to the implementation.** File paths like `src/codegen/src/support/core/escape_analysis.cpp` name real sources. Behavior is verified against generated C++ (`--show-runtime`) and the test suite — not against intuition.
2. **Optimizations are never promises.** If the compiler cannot prove a fact, the ordinary, checked path is kept. "The compiler may do X" means "the compiler does X for the proven cases, and nothing else changes."
3. **Costs are named.** Allocation, atomic reference-count traffic, bounds checks, and container growth are separate costs. Fixing one does not fix another.

For exact command behavior, see the [CLI and compiler](../cli-and-compiler/00-overview.md) section; for language semantics, see the [language](../language/00-overview.md) section; for the standard library, see the [standard library](../standard-library/00-overview.md) section.
