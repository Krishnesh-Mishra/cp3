---
title: Native toolchains
chapter: Chapter 4 · Compiler and tools
---
# Native toolchains

CP3 emits C++ and relies on a native toolchain to produce the final executable. The selected compiler affects compilation, diagnostics, optimization, linking, and platform compatibility. The toolchain used to build the CP3 compiler itself is a separate concern from the toolchain selected for a CP3 application.

## The two compiler builds

| Build | What it compiles | Where it is configured |
| --- | --- | --- |
| CP3 compiler build | CP3 front end, code generator, CLI, and runtime sources | Repository CMake build and the C++ compiler used by CMake |
| CP3 application build | Generated C++, runtime objects, native dependencies, final program | `cp3 toolchain` configuration and application build profile |

Changing an application toolchain does not rebuild CP3's lexer or semantic analyzer. Changing CP3 compiler source requires rebuilding the repository's CMake targets before a new compiler binary contains those changes.

## Inspect choices and installed versions

The toolchain command supports these read and configuration forms:

```sh
cp3 toolchain list
cp3 toolchain list --json
cp3 toolchain detect
```

`list` reports discovered compiler candidates, installed versions, and the effective choice. `--json` exposes machine-readable status for scripts. `detect` shows what automatic discovery finds. A candidate can be installed but unusable for the active target; read the reported problem instead of treating “found” as “ready.”

CP3 currently selects Clang-family toolchains for application builds. Detection can find multiple installations and versioned binaries. Record the exact version and path when comparing generated executables.

## Choose a toolchain

Use a compiler family, optional version, or explicit executable path:

```sh
cp3 toolchain use clang
cp3 toolchain use clang@18
cp3 toolchain use C:/LLVM/bin/clang++.exe
```

Use `--project` to save the selection for the nearest project, or `--global` for the user's global setting:

```sh
cp3 toolchain use clang@18 --project
cp3 toolchain use clang --global
```

Project configuration belongs to that project's `package.json`; global configuration is stored outside the repository. Choose project scope when contributors need a shared, reproducible compiler expectation. Choose global scope when the preference is specific to one developer's machine.

To clear a saved choice, use `cp3 toolchain clear --project` or `cp3 toolchain clear --global`. Bare `cp3 toolchain` opens the interactive selection flow when attached to a terminal.

## Selection precedence and strict pins

A one-command toolchain override can take precedence for that invocation. Environment, project, global, and automatic selection then participate according to the CLI's toolchain resolver. Use `cp3 toolchain list` to see the effective choice and its source instead of guessing which setting won.

A project pin can stop matching after a machine change or an installation update. By default, the resolver can attempt the applicable fallback path. `--strict-toolchain` asks the command to fail when the saved exact choice is unavailable; this is useful for CI jobs that must reject a changed compiler selection.

Strict selection checks the toolchain identity. It does not make native builds deterministic by itself: SDK versions, system headers, linker inputs, target architecture, and profile flags also matter.

## Build profile and optimization

The release profile selects optimization options for the configured native toolchain. The profile can include `-O` optimization, LTO, target-specific instruction selection, loop unrolling, and optional PGO behavior. Debug and incremental profiles have different goals. Consult [Build profiles and PGO](#04-compiler-cli/11-build-profiles-and-pgo) for the current command switches.

Native optimization operates on generated C++. It may inline or vectorize a loop when the emitted code exposes enough information, but it cannot disregard required checks or observable side effects. Use [Generated C++ inspection](#04-compiler-cli/10-generated-cpp-inspection) and backend diagnostics to establish what happened.

## Troubleshoot native compilation

When a build fails, identify the stage first:

1. A CP3 diagnostic code indicates front-end or semantic rejection.
2. A C++ file and line usually indicate native compilation of generated or runtime source.
3. An undefined symbol or missing library usually indicates link configuration or an absent dependency.
4. A runtime error occurs after a binary has been produced and launched.

For a native compiler failure, preserve the first diagnostic, full compiler invocation, generated source, target flags, and toolchain version. Later errors may be cascades from the first failure. For a linker error, preserve library search paths and the complete link command.

Do not patch an ephemeral generated `.cpp` as the lasting fix. Trace the output to the CP3 code generator or runtime implementation, then add or update a source-level test.

## Reproduce performance measurements

For a benchmark comparison, keep the CP3 build profile and toolchain version explicit. Compare the same input and output oracle, record the repetition count and variation, and note whether timing includes process startup or compilation. A compiler name alone is not enough to reproduce a result.

When investigating vectorization, request a report from the configured backend and verify that the diagnostic points to the generated benchmark loop. A remark from a standard library header is not evidence about that loop.

## Related pages

- [Project settings and build cache](#04-compiler-cli/09-project-settings-and-cache)
- [Build profiles and PGO](#04-compiler-cli/11-build-profiles-and-pgo)
- [Compilation pipeline](#04-compiler-cli/02-compilation-pipeline)
- [Generated C++ inspection](#04-compiler-cli/10-generated-cpp-inspection)
