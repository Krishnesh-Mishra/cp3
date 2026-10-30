---
title: Compilation pipeline
chapter: Chapter 4 · Compiler and tools
---
# Compilation pipeline

CP3 compiles a source program in distinct stages. A failure in an earlier stage prevents later stages from running.

## 1. Source loading and modules

The CLI selects the entry point and loads source files and imports. Standard modules are resolved from the compiler's registry and linked runtime support. A missing import or invalid export is diagnosed before native compilation.

## 2. Lexing and parsing

The lexer turns source text into tokens. The parser builds syntax trees and reports malformed constructs with source locations. Syntax errors are not C++ compiler errors; generated C++ may not exist yet.

## 3. Semantic analysis

The analyzer resolves names, checks declared and inferred types, validates imports and members, and reports semantic diagnostics. This is where CP3 rejects operations that violate the static language rules.

## 4. C++ generation

The code generator translates checked CP3 constructs into C++ and runtime calls. The generated source is an implementation artifact. It is useful for debugging and performance inspection, but it is not the language specification.

## 5. Native compilation and linking

The selected C++ compiler compiles generated code and CP3 runtime units, then links the executable. Errors at this stage may come from the native compiler, linker, missing system dependencies, or incompatible toolchain settings.

## Build cache

Project builds store generated artifacts under the build directory and may reuse native object files when inputs and configuration allow it. A changed compiler, runtime, target, or build setting can invalidate artifacts. Use `cp3 clean` when a deliberately fresh project build is needed; rebuilding the compiler itself with CMake is a separate repository workflow.

## Finding the stage

Read the first reported failure and note whether it names a CP3 diagnostic code, a generated `.cpp` file, or a native link symbol. These point to different stages. The [diagnostics page](#04-compiler-cli/06-diagnostics) describes the generated local help link.

## The stages in order

| Stage | Main responsibility | Typical failure |
| --- | --- | --- |
| Project discovery | Select entry point, manifest, imports, and toolchain settings | Wrong root, missing file, invalid package metadata |
| Lexing | Convert source characters into tokens | Invalid token or unterminated literal |
| Parsing | Build syntax/AST nodes | Unexpected token or unsupported grammar |
| Semantic analysis | Resolve names, modules, types, signatures, and diagnostics | Unknown name, type mismatch, invalid API member |
| Code generation | Lower the accepted representation into C++ translation units | Unsupported lowering or internal codegen diagnostic |
| Native compilation | Compile generated and runtime C++ with selected Clang | C++ diagnostic, missing include, unsupported flag |
| Linking | Combine object files and native dependencies | Undefined symbol or missing library |
| Execution | Run the produced native binary | Runtime exception, environment or I/O failure |

The `check` command stops after front-end analysis. It does not run code generation or prove that the native compiler can compile the program. `build` and `run` proceed through native compilation and linking; `run` additionally executes the binary.

## Imports and project boundaries

The CLI resolves CP3 modules before code generation. Standard modules use a registry with closed exported surfaces. Local source modules and installed CP3 packages enter the dependency graph and generate translation units as required. An unresolved import is a front-end error; it is not deferred to the C++ linker.

Project manifests influence the entry file and compile settings. A nested manifest or wrong current directory can make a command load settings from a different project than expected. Confirm the root before diagnosing a type-mode or toolchain discrepancy.

## Generated translation units and native compile

Codegen emits C++ for the program and connects the selected runtime support. The native toolchain compiles those sources and links required libraries. `--show-runtime` preserves the generated C++ and runtime support snapshot under the project build tree for inspection.

A C++ error in a generated file should be traced back to the CP3 source construct and the emitter that owns it. A linker error is later and usually concerns symbols, object inclusion, library discovery, or target mismatch. Keep the first relevant native error and full compile command when filing an issue.

## Incremental builds

The CLI records build inputs and can reuse unchanged native compilation outputs. The compiler may still regenerate a translation unit when an upstream dependency or option changes. A CMake build of the CP3 compiler and an application build of a `.cp3` program have separate build trees and caches.

Use the normal build to verify an edit. Clean only when checking a clean-build property or when there is concrete evidence of stale state. See [Project settings and build cache](#04-compiler-cli/09-project-settings-and-cache).

## Source locations and diagnostics

The front end maps tokens and AST nodes to original file spans. CP3 diagnostics use those spans and stable diagnostic codes, even though generated C++ has different line numbers. A native compiler error may include a generated location; use the preserved runtime/source mapping and generated files to map it back.

## Where to trace a pipeline bug

Start at the first stage that produces incorrect behavior. Follow the owning files in `src/lexer`, `src/parser`, `src/sema`, `src/codegen`, `src/runtime`, or `src/cli`. Then inspect the corresponding CMake target and test family. Do not fix a semantic rejection by adding a codegen workaround, or a runtime bug by changing parser acceptance.

## Related pages

- [CLI commands](#04-compiler-cli/01-cli-commands)
- [Diagnostics](#04-compiler-cli/06-diagnostics)
- [Generated C++ inspection](#04-compiler-cli/10-generated-cpp-inspection)
