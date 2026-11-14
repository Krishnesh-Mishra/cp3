---
title: Welcome to CP3
chapter: Welcome
---
# Welcome to CP3

CP3 is a statically typed language with a JavaScript and TypeScript shaped surface. It compiles `.cp3` source into C++, then uses a native toolchain to produce the executable you run.

The language is designed for developers who value familiar syntax and compiler feedback, and want to deploy a native program. CP3 is its own language: it does not run Node or a browser, and arbitrary npm packages are not available unless an explicit supported integration is provided.

## Choose a starting point

| If you want to… | Start here |
| --- | --- |
| Learn CP3 with JS/TS experience | [For JavaScript and TypeScript developers](./03-javascript-typescript-developers.md) |
| Understand the project's design | [Why CP3 exists](./01-why-cp3.md) |
| Create and run a program | [Create your first project](./04-first-project.md) |
| Learn how performance claims are measured | [Performance, measured carefully](./02-performance.md) |
| Find exact declaration and type rules | [Chapter 1 · The language](../01-language/00-overview.md) |
| Understand memory and ownership | [Chapter 2 · Runtime model](../02-runtime/00-overview.md) |
| Check a built-in module's API | [Chapter 3 · Standard library](../03-standard-library/00-overview.md) |
| Build the compiler or debug generated C++ | [Chapter 4 · Compiler and tools](../04-compiler-cli/00-overview.md) |

## What happens when you build

The front end loads source files and imports, tokenizes and parses them, and checks names and types. The code generator lowers accepted source into C++. The CLI invokes the selected native compiler and linker, then places outputs in the project build directories. `cp3 check` stops after front-end analysis; `cp3 build` compiles and links; `cp3 run` also launches the executable.

The ordinary project workflow does not require you to edit generated C++. Generated files are useful when debugging a compiler issue or understanding a performance result. The [compilation pipeline](../04-compiler-cli/02-compilation-pipeline.md) explains each stage and its failure modes.

## What values and memory mean here

Primitive values such as numbers and booleans use value representations. Arrays, class instances, closures, and other reference-shaped values may use runtime-managed references. CP3 does not ask application code to call C++ `delete`, but the representation still has lifetime and performance costs.

The runtime uses reference ownership for general references; it does not use a tracing garbage collector. Reference counting releases acyclic objects when their last strong owner goes away, but strong cycles require attention. The compiler also has narrow escape-analysis and local-lowering optimizations; do not assume all local objects become stack values. See [The value model](../02-runtime/01-value-model.md), [Escape analysis](../02-runtime/02-escape-analysis.md), and [Reference cycles](../02-runtime/09-reference-cycles-and-weak-links.md).

## How to use this guide

Chapter 1 presents language rules in a useful learning order: bindings, types, mutability and scope, then functions, control flow, values, classes, generics, modules, and errors. Chapter 2 explains runtime representation and lifetime. Chapter 3 describes CP3's supported standard modules. Chapter 4 covers the compiler and its tools.

Pages use links to connect related concepts. A code example is intended to illustrate one behavior; it is not a promise that every JavaScript syntax form or library member is supported. When the guide calls out an implementation limit, use the referenced tests and compiler diagnostics to determine the exact boundary in the current checkout.

## This guide is version-specific

The guide is built into the compiler from Markdown under `docs/content`. Its API descriptions are maintained alongside the compiler and runtime. If your installed compiler differs from this source checkout, use that compiler's `help` output, diagnostics, and test results as the final authority for available flags and behavior.

For performance, treat benchmark data as a dated observation rather than a language-wide ranking. The same program can change speed with the backend compiler, target architecture, release flags, host load, and measurement method. Read [Performance, measured carefully](./02-performance.md) before drawing conclusions from one median.
