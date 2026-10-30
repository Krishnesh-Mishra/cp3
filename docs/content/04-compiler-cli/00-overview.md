---
title: Compiler and command line
chapter: Chapter 4 · Compiler and tools
---
# Compiler and command line

CP3 source is parsed and type-checked before C++ code generation. The CLI coordinates project configuration, compilation, native linking, formatting, diagnostics, and local documentation.

## Build and run lifecycle

The normal `cp3 run` workflow selects a source entry, checks it, generates native code in the build directory, invokes the configured C++ toolchain, and runs the resulting executable. Generated files are build artifacts; the project source remains CP3.

## Pages in this chapter

- [CLI commands](#04-compiler-cli/01-cli-commands) covers the everyday command surface.
- [Compilation pipeline](#04-compiler-cli/02-compilation-pipeline) describes stages and failures.
- [Optimization pipeline](#04-compiler-cli/03-optimization-pipeline) documents current proved transformations and their limits.
- [Generics and specialization](#04-compiler-cli/04-generics) explains generic lowering at a high level.
- [Native toolchains](#04-compiler-cli/05-native-toolchains) covers C++ compiler selection and generated artifacts.
- [Diagnostics and generated help](#04-compiler-cli/06-diagnostics) explains source locations and the documentation link.
- [Native interop](#04-compiler-cli/07-native-interop) covers the explicit C++ and Rust boundary types.
- [Tests and compiler verification](#04-compiler-cli/08-tests-and-verification) explains conformance modes and focused filters.
- [Project settings and build cache](#04-compiler-cli/09-project-settings-and-cache) covers manifests, artifacts, and reuse.
- [Generated C++ inspection](#04-compiler-cli/10-generated-cpp-inspection) explains how to trace a source construct.
- [Build profiles and PGO](#04-compiler-cli/11-build-profiles-and-pgo) documents debug, incremental, and release builds.

## Versioned behavior

Compiler behavior changes as the language and runtime evolve. The diagnostic code and exact API surface emitted by the compiler being run are authoritative. Benchmark numbers are machine- and toolchain-dependent; see [Performance, measured carefully](#00-welcome/02-performance) for dated results and interpretation.
