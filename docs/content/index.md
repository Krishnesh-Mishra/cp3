---
title: CP3 Documentation
chapter: Home
---

# CP3 Documentation

**Familiar syntax. Static checks. Native executables.**

CP3 is a statically typed language with a JavaScript- and TypeScript-shaped surface. It compiles `.cp3` programs to C++ and uses a native toolchain to produce the executable you run. This documentation covers the language, runtime model, standard library, compiler, and command-line tools.

[Create your first project](00-welcome/04-first-project.md){ .md-button .md-button--primary }
[Why CP3?](00-welcome/01-why-cp3.md){ .md-button }
[GitHub repository](https://github.com/Krishnesh-Mishra/cp3){ .md-button }

## Browse the guide

| Chapter | What it covers |
| --- | --- |
| [Welcome to CP3](00-welcome/00-welcome.md) | Where to start, what CP3 is designed for, and how to read the guide. |
| [Chapter 1 · The language](01-language/00-overview.md) | Bindings, types, functions, control flow, classes, generics, modules, errors, and native scalar types. |
| [Chapter 2 · Runtime model](02-runtime/00-overview.md) | Values, references, ownership, closures, allocation, async execution, and thread safety. |
| [Chapter 3 · Standard library](03-standard-library/00-overview.md) | The supported `std/...` modules, from console and filesystem APIs to networking and typed arrays. |
| [Chapter 4 · Compiler and tools](04-compiler-cli/00-overview.md) | CLI commands, compilation and optimization pipelines, diagnostics, native toolchains, and build profiles. |

## Quick links

- New to the language? Read [For JavaScript and TypeScript developers](00-welcome/03-javascript-typescript-developers.md).
- Ready to build something? Follow [Create and run your first project](00-welcome/04-first-project.md).
- Looking for a module? Start at the [standard library overview](03-standard-library/00-overview.md).
- Debugging a build? See [CLI commands](04-compiler-cli/01-cli-commands.md) and [Diagnostics and generated help](04-compiler-cli/06-diagnostics.md).
- Evaluating performance? Read [Performance, measured carefully](00-welcome/02-performance.md).

## About this site

This site is generated from the Markdown sources in `docs/content` using [MkDocs](https://www.mkdocs.org/) and the [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/) theme. The documentation is maintained alongside the CP3 compiler and runtime in the [CP3 repository](https://github.com/Krishnesh-Mishra/cp3).
