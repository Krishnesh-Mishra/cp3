---
title: Project settings and build cache
chapter: Chapter 4 · Compiler and tools
---
# Project settings and build cache

A CP3 project is rooted by its project metadata and source entry. The CLI searches for the nearest `package.json` when it needs project settings. The manifest influences the entry point, project type, type-name vocabulary, toolchain pin, and package build behavior; read each field as a CP3 setting, not as an npm runtime contract.

## The project root and entry point

A typical application contains a `package.json` and `src/index.cp3`. The `main` property names the entry point used by project commands when an explicit file is not supplied. `cp3 init` creates a starter manifest and source file. For package projects, the manifest's `type` selects the package flow and build metadata such as the list of included files.

Run a command from inside the project or give it a source path so the CLI can identify the correct project context. If two nested projects contain manifests, the nearest applicable root determines the configuration; check that you are editing the intended file.

## Type-system settings

`typeSystem` can be `typescript`, `cpp`, or `rust`. It changes resolution of supported unprefixed scalar names, but CP3 still generates C++. `strictNumericCasts` is a separate JSON boolean. See [Types](../language/01-variables-and-bindings/02-types.md) for examples and semantics.

## Toolchain settings

`cp3 toolchain` manages the Clang compiler choice. The CLI can inspect, detect, use, clear, or save the choice at project or global scope. A one-command `--toolchain` override takes precedence; `CP3_TOOLCHAIN`, project configuration, global configuration, and automatic detection are considered according to the resolution order in the toolchain implementation.

A project toolchain pin can become unavailable after a machine change. Use `--strict-toolchain` when a missing pinned compiler should fail rather than silently selecting another available compiler. Record the selected Clang version when reporting build or performance results.

## Build artifacts and cache

Application artifacts live below `.cp3/build` and `.cp3/out`; `.cp3/native` and package caches have separate lifetimes. `cp3 clean` removes build and output products while preserving native SDK state. `cp3 clean --all` also clears the package cache (`.cp3/packages`), the package lock (`.cp3/lock.json` — removed together with the packages it describes), and the native SDK cache (`.cp3/native`).

Incremental builds reuse prior outputs when the source, compiler identity, headers, options, and cache keys remain compatible. A source edit normally invalidates affected outputs and dependents; it should not require deleting every object file. If a result appears stale, inspect the build record and compiler path before clearing caches.

## Build profiles

`cp3 build` defaults to the release profile. The CLI also exposes `--incremental` for fast cached builds without LTO/PGO and `--debug` for `-O0` plus debug information. Build profile affects compile options and generated code, so comparisons must use the same profile.

Release options can include link-time optimization, target-native ISA, loop unrolling, and PGO when an applicable profile exists. Flags such as `--no-lto`, `--no-native`, `--no-unroll`, `--strict-fp`, `--no-pgo`, `--report` (cycle decisions, acted on by `cp3 check`), and `--max-memory` (codegen-phase memory cap) let the caller control parts of the profile. Consult `cp3 help` for the installed compiler's exact behavior.

## When a clean build is appropriate

A clean build is useful when validating packaging, diagnosing a cache bug, or verifying behavior independent of prior outputs. It is not the default response to every code edit. Prefer the normal incremental CMake build or application build and use the tool's cache invalidation path if a specific artifact is stale.

## Contributor checklist

- Confirm the manifest path and entry used by the failing command.
- Record `typeSystem`, `strictNumericCasts`, and toolchain selection where relevant.
- Distinguish CP3 compiler build artifacts from application build artifacts.
- Keep generated binaries and build directories out of source changes unless the task asks for them.
- Preserve the user's local project settings and uncommitted files.
- Do not report a clean build unless the clean operation actually occurred.

## Source locations

Project JSON parsing and compile settings live under `src/cli/src/pkg` and `src/cli/src/commands`. Toolchain detection and configuration live under `src/cli/src/toolchain`. CMake target wiring lives in the relevant `CMakeLists.txt` files. Search the implementation when a setting's precedence is material to a code change.

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Native toolchains](./05-native-toolchains.md)
- [Native type selection](../language/07-native-types/01-project-type-system.md)
- [Compilation pipeline](./02-compilation-pipeline.md)
