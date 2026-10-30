---
title: Native toolchains
chapter: Chapter 4 · Compiler and tools
---
# Native toolchains

CP3 emits C++ and relies on a native toolchain to produce the final executable. The installed C++ compiler therefore affects compile time, optimization, diagnostics, and platform compatibility.

## Inspecting the configured toolchain

Use `cp3 toolchain` to inspect or select a supported toolchain. The project compiler is built with CMake, while an application build uses the configured native C++ compiler. These are related but distinct operations.

## Optimization levels and generated code

The application build settings determine the native compiler flags. Optimization can improve generated code, but it cannot always recover facts hidden behind runtime helpers, aliasing, or control-flow checks. For hot loops, inspect the generated C++ first and then the compiler's optimization report.

## Debugging native build failures

Separate CP3 diagnostics from C++ compile and link errors. If a generated file fails in C++, preserve the generated source and full compiler command. If linking fails, inspect missing runtime libraries and platform dependencies. A source-level type error should be fixed in CP3 code before trying to edit generated C++.

## Reproducibility

Record compiler version, target architecture, build mode, and native flags when reporting performance. C++ compilers may produce materially different code across releases and targets; benchmark comparisons are meaningful only when their conditions are understood.
