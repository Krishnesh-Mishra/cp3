---
title: Chapter 4 · Compiler and tools
chapter: Chapter 4 · Compiler and tools
---
# Chapter 4 · Compiler and tools

This chapter is the practical guide to the CP3 compiler, CLI, native toolchain, project cache, diagnostics, generated C++, and verification workflow. It is useful both for application developers diagnosing a build and for contributors changing compiler behavior.

## Start with the task

| Task | Page |
| --- | --- |
| Create, check, run, build, clean, or package a project | [CLI commands](01-cli-commands.md) |
| Understand what happens from `.cp3` to executable | [Compilation pipeline](02-compilation-pipeline.md) |
| Learn the existing optimization proofs and limits | [Optimization pipeline](03-optimization-pipeline.md) |
| Understand generic lowering | [Generics and specialization](04-generics.md) |
| Select or diagnose a native compiler | [Native toolchains](05-native-toolchains.md) |
| Read a compiler error and open its related documentation | [Diagnostics and generated help](06-diagnostics.md) |
| Integrate a native SDK | [Native packages and ABI boundaries](07-native-interop.md) |
| Find conformance test families | [Tests and verification](08-tests-and-verification.md) |
| Understand `package.json`, output paths, and cache reuse | [Project settings and build cache](09-project-settings-and-cache.md) |
| Trace a CP3 expression into its C++ output | [Generated C++ inspection](10-generated-cpp-inspection.md) |
| Choose debug, incremental, release, or PGO | [Build profiles and PGO](11-build-profiles-and-pgo.md) |

## Two workflows, two build trees

Developing a CP3 program and developing the CP3 compiler are separate workflows:

```text
CP3 compiler source --CMake--> cp3c executable
.cp3 program --cp3 CLI--> generated C++ --native compiler--> program executable
```

After editing compiler or runtime C++ source, rebuild the compiler so the executable contains that edit. After editing a `.cp3` application, use the project CLI to rebuild that application. The repository's CMake object cache is not the same cache as a project's `.cp3/build` directory.

## Development and release commands

Use `cp3 check` for fast front-end feedback. Use `cp3 run` to build and execute. Use `cp3 build` to produce an executable without launching it. Use `cp3 dev` for the watch workflow, and `cp3 fmt` for source formatting.

Project commands find settings from a project manifest and may reuse cached outputs. A `--release` build is intended for optimized execution; `--debug` is for debugging; `--incremental` favors short cached development builds. Keep the profile fixed when collecting performance results.

## What the compiler is responsible for

The CP3 compiler owns source-language behavior: tokenization, grammar, module resolution, types, diagnostics, and lowering. Generated C++ is an intermediate artifact. The native compiler then applies its own optimizations and emits machine code. A backend optimization cannot repair an incorrect CP3 lowering, and CP3 source semantics cannot be inferred from one generated C++ spelling.

When investigating a performance issue, trace all three layers: source expression, CP3-generated C++, and backend machine-code/optimization evidence. A pass being present in the source tree is not proof it applied to a particular expression.

## Source organization for contributors

The implementation is organized into focused components such as `src/lexer`, `src/parser`, `src/modules`, `src/sema`, `src/codegen`, `src/runtime`, `src/std`, and `src/cli`. Start at the stage that first behaves incorrectly, then follow the corresponding test family in `tests/cases` and build target.

Avoid fixing a front-end type issue by emitting a special C++ workaround, or changing parser acceptance to hide a runtime failure. Keep the fix at the layer that owns the observed behavior and add the smallest representative positive and negative tests for a compiler change.

## Version and reproducibility

The CLI flags and compiler implementation evolve. Use the installed compiler's `help` output as the authority for a command, and record `cp3 version` with build logs. For benchmark reports, also record the native compiler version, target, profile, repetitions, output oracle, and variation.

## Pages in this chapter

- [CLI commands](01-cli-commands.md)
- [Compilation pipeline](02-compilation-pipeline.md)
- [Optimization pipeline](03-optimization-pipeline.md)
- [Generics and specialization](04-generics.md)
- [Native toolchains](05-native-toolchains.md)
- [Diagnostics](06-diagnostics.md)
- [Native packages and ABI boundaries](07-native-interop.md)
- [Tests and verification](08-tests-and-verification.md)
- [Project settings and cache](09-project-settings-and-cache.md)
- [Generated C++ inspection](10-generated-cpp-inspection.md)
- [Build profiles and PGO](11-build-profiles-and-pgo.md)
