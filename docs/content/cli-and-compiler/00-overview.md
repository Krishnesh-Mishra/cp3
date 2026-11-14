---
title: CLI and compiler
---
# CLI and compiler

This section is the practical guide to the CP3 command line: what the commands do, which settings a project can carry, the build profiles and flags, and how to read failures. If you want to ship a CP3 program, you live here.

## Start with the task

| Task | Page |
| --- | --- |
| Create, check, run, build, test, clean, or package a project | [CLI commands](./01-cli-commands.md) |
| Understand what a build does and what each failure means | [How a build flows](./02-compilation-pipeline.md) |
| See what the compiler optimizes (and what it never does) | [Compiler optimization and guarantees](./03-optimization-pipeline.md) |
| Generics and the build | [Generics and the build](./04-generics.md) |
| Select, pin, or troubleshoot the C++ compiler | [Native toolchains](./05-native-toolchains.md) |
| Read a diagnostic and open its generated help | [Diagnostics and generated help](./06-diagnostics.md) |
| Integrate a native SDK or library | [Native packages and ABI boundaries](./07-native-interop.md) |
| Run the conformance suite or add a test | [Tests and verification](./08-tests-and-verification.md) |
| Understand `package.json`, `.cp3/`, and cache reuse | [Project settings and build cache](./09-project-settings-and-cache.md) |
| Inspect the generated C++ for a check or a performance question | [Generated C++ inspection](./10-generated-cpp-inspection.md) |
| Choose debug, incremental, release, or PGO | [Build profiles and PGO](./11-build-profiles-and-pgo.md) |

## Two build trees

Building the CP3 compiler and building a CP3 program are different workflows with different caches:

```text
CP3 compiler source  --CMake-->   cp3c executable
.cp3 program        --cp3 CLI-->  program executable
```

After editing the compiler, rebuild the compiler (CMake). After editing a `.cp3` program, use the `cp3` CLI on that project. The repository's CMake object cache is not the same as a project's `.cp3/` directory, and cleaning one does nothing for the other.

## Everyday commands

- `cp3 check` — parse and type-check; no C++ is produced.
- `cp3 run` — build (incrementally, cached) and execute.
- `cp3 build` — build an optimized release binary, without running it.
- `cp3 dev` — watch and rebuild on save.
- `cp3 warm` — build every artifact ahead of time (nothing runs).
- `cp3 test` — build and run test files.

Start with [CLI commands](./01-cli-commands.md); pick your profile in [Build profiles and PGO](./11-build-profiles-and-pgo.md).

## If you are new here

1. Install the toolchain: [Native toolchains](./05-native-toolchains.md).
2. Make a project, run the example: [Quick start](../quick-start.md).
3. Give your project a manifest and understand caching: [Project settings and build cache](./09-project-settings-and-cache.md).
4. When something fails, match the output to its stage: [Diagnostics](./06-diagnostics.md) and [How a build flows](./02-compilation-pipeline.md).

## Pages in this section

- [CLI commands](./01-cli-commands.md)
- [How a build flows](./02-compilation-pipeline.md)
- [Compiler optimization and guarantees](./03-optimization-pipeline.md)
- [Generics and the build](./04-generics.md)
- [Native toolchains](./05-native-toolchains.md)
- [Diagnostics and generated help](./06-diagnostics.md)
- [Native packages and ABI boundaries](./07-native-interop.md)
- [Tests and verification](./08-tests-and-verification.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Generated C++ inspection](./10-generated-cpp-inspection.md)
- [Build profiles and PGO](./11-build-profiles-and-pgo.md)
