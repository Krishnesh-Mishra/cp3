---
title: Compiler design
---
# Compiler design

CP3 is always built the same way: **your `.cp3` source is translated to C++20, and that C++ is handed to Clang.** There is no intermediate representation, no bytecode, no interpreter, and no second backend. This page walks the whole path: from the project folder on disk, through the frontend, code generation, and the native build, to the executable that lands in `.cp3/out`.

## The short version

```text
main.cp3 ──► module graph ──► semantic analysis ──► C++20 text ──► clang++ ──► .cp3/out/main[.exe]
```

Every stage is mandatory. A failure in an earlier stage prevents later stages from running: a syntax or type error never reaches Clang, and Clang is never asked to compile invalid CP3 semantics. The generated C++ is an implementation artifact — you can read it, but it is not the language specification.

## Stage 1 — project discovery and the `.cp3` folder

Commands find the project root by walking up from the entry file to the nearest `package.json` (`src/cli/src/pkg/cache/cache.cpp` `findProjectRoot`). The first `cp3c build` (or `run`) creates a `.cp3` directory at that root. `cp3c init` itself only scaffolds `package.json` and `src/index.cp3` — `.cp3/` appears on the first build.

What lives in `.cp3/`:

| Entry | Holds |
| --- | --- |
| `.cp3/build/` | Generated C++ only (D61): one `.cpp`/`.hpp` pair per unit (`src/index.cpp`), plus `cp3_program.hpp` for multi-unit builds, and PCH artifacts (Incremental profile only). For packages, staging for cached objects. |
| `.cp3/cache/` | Native object files mirroring the source tree (`src/index.o`), stock runtime objects under `_runtime/`, `build-manifest.txt` (the v3 incremental manifest), `incremental-program-cache` (per-unit fragment cache), the `<exe>.link-stamp`, and the link-arguments response file (`link-objects.rsp`) when it would exceed the command-line limit. |
| `.cp3/out/` | The linked executable (`<entry-stem>` or `<entry-stem>.exe`), or a `.cp3pak` for package projects. |
| `.cp3/packages/<name>/` | Installed CP3 packages (unpacked from `.cp3pak` archives). |
| `.cp3/native/<id>/` | Native SDK bindings installed by `cp3c native-install` (`native.json` + a generated `.cp3` surface + the copied libraries). |
| `.cp3/link/<name>/` | Extracted `.cp3pak` link caches for artifact links (directory links are used in place). |
| `.cp3/lock.json` | Package lockfile with content hashes. |
| `.cp3/pgo/default.profdata` | The auto-detected PGO profile used by release builds when present. |

`cp3 clean` removes `.cp3/build`, `.cp3/cache`, and `.cp3/out`. `cp3 clean --all` additionally removes `.cp3/packages`, `.cp3/lock.json`, and `.cp3/native`. Link caches under `.cp3/link/` are removed only by `cp3c unlink`.

The generated translation units are never compiled from anywhere but `.cp3/build`. When you pass `--show-runtime`, the CLI copies the runtime headers and the runtime translation units selected for this program into `.cp3/build/_runtime/` — a **show-only mirror** for inspection (it is never fed back to the compiler, is created only once, and is wiped by `clean`).

## Stage 2 — module loading and semantic analysis

The CLI loads the entry point and walks the import graph (`loadModuleGraph`, `src/cli/src/toolchain/runtime/pipeline.cpp`). Standard modules resolve from the compiler's registry; local files and installed packages enter the same graph. A missing import is a front-end error — it never reaches the linker.

Then semantic analysis runs (`analyzeProgramGraph`): names are resolved, types are checked, and diagnostics carry stable codes and spans. Before any code is emitted, per-unit and entry semantic results are assembled into the inputs the generator consumes (`FrontendResult`: module graph, entry `SemaResult`, per-unit side tables).

`cp3 check` stops here: no C++ is written and the toolchain is never invoked.

## Stage 3 — code generation: CP3 becomes C++20

The code generator (`src/codegen`, the `cp3_codegen` static library) takes the checked AST and `SemaResult` and emits **C++20 source text**. There is deliberately no IR between the AST and the emitters (DECISION D1); analyses are whole-program passes over the checked tree, and emission walks it once.

The generator's pipeline per unit (`CodeGenerator::runUnit`, `src/codegen/src/code_generator.cpp`):

1. **Pre-passes** — captures analysis (closure cells), enum/collection/JSON-shape collection, array reference analysis, worker-site collection, model registry, then the **escape analysis** (`analyzeEscapeAllocations`) and the DataView-loop scan. All of these run *before* any C++ spelling is produced.
2. **Emission walk** — top-level statements emit into three sections: declarations, definitions, and `main`'s body. Two separate declaration sections exist for classes (forward declarations first), so emit order never depends on source order.
3. **Assembly** — `decls`, `defs`, and the main body are assembled into a `.cpp`/`.hpp` pair, with `#include` lines selected by a usage scan (`applyUsageIncludes`) — a program that never mentions math pays nothing for `cp3/std/math.hpp`.

Three output shapes exist:

| Shape | When | Output |
| --- | --- | --- |
| Single-unit TU | No resolved local imports | One `.cpp`/`.hpp` for the whole program |
| Whole-program TU | Local-module graph, **Release**/**Debug** | One `.cpp`; a `cp3/compilation unit` becomes a `cp3_mod_<unit>` namespace; two-phase emission (all declarations, then all definitions) |
| Plural per-module TUs | Local-module graph, **Incremental** | One shared header `cp3_program.hpp` plus one TU per unit, compiled and cached independently; unchanged units are reused |

Programs with workers emit extra TUs for the worker entries. The only consumers of the generator are the toolchain driver and the package packer — both write `.cpp` files and invoke Clang.

## Stage 4 — native compilation: C++20 to machine code

Generated C++ and the runtime translation units selected for *this* program (via markers in the generated text, plus the runtime sources themselves) are compiled with a single, direct `clang++` invocation per translation unit (`build.cpp`, `compileArgs` in `toolchain.cpp`):

```text
clang++ --target=x86_64-w64-windows-gnu --sysroot=<mingw> \
    -std=c++20 -c .cp3/build/src/index.cpp -o .cp3/cache/src/index.o \
    -I<runtime/include> -I<common/include> -I<std-family/include>... [opt flags]
```

Key properties:

- **Clang is the only compiler.** cp3c validates every candidate by compiling, linking, and running a one-line C++20 program before accepting it. Minimum version is Clang 16. On Windows, CP3 targets **MinGW-w64 GNU** (never the MSVC triple); the target is rewritten automatically, and a sysroot is located by probing.
- **Resolution order** for the toolchain choice: `--toolchain` flag → `CP3_TOOLCHAIN` env → the project's `package.json` `"toolchain"` object → the global config (`%APPDATA%\cp3\toolchain.conf` on Windows, `$XDG_CONFIG_HOME/cp3/toolchain.conf` elsewhere) → newest usable installed Clang. A pinned version that has vanished fails hard under `--strict-toolchain`, otherwise falls back with a warning.
- **Compile fan-out** is bounded by `std::thread::hardware_concurrency()`, overridable with `CP3C_BUILD_PARALLEL=<n>`. The manifest is re-written by a single thread after the pool drains.
- **Profiles** differ in flags and in cache behavior:

| Profile | `-O` flags | Cache | PCH | Codegen |
| --- | --- | --- | --- | --- |
| Incremental (default for `run`) | `-O0` | Used | Yes | Plural per-module TUs |
| Debug (`--debug`) | `-O0 -g` | Used | No | Whole-program TU |
| Release (default for `build`) | `-O3 -flto -march=native -funroll-loops -fno-math-errno` (+ PGO when `.cp3/pgo/default.profdata` exists) | **Ignored — `.cp3/build` and `.cp3/cache` are wiped first** | No | Whole-program TU |

- **Linking** goes through the same `clang++` driver, with `-Wl,--gc-sections` (except Debug), `ld.lld` when present, `-static -lwinpthread` on Windows, and the vendored static libraries appended last (`-luSockets -luv -lssl ...` — see [Runtime libraries](./05-runtime-libraries.md)). The output is written to `.cp3/out/<stem>.exe.link-tmp` and atomically renamed, so a running dev child keeps its executable. The link is skipped entirely when nothing compiled and the **link stamp** (a hash of profile, headers, compiler identity, and every linked object + library archive) still matches.

## Incremental builds

Two reuse layers stack on top of each other:

1. **Object cache** (`.cp3/cache/build-manifest.txt`, format `cp3c-manifest-v3`): one line per translation unit — profile key, source identity, content hash (fnv1a64), include-tree fingerprint, compiler key (`clang|<version>|<path>|<target flags>`). A TU is reused only on an exact match of all four; any change to the include tree retires every object, by design (CP3 does not track per-file header dependencies yet).
2. **Fragment cache** (`cp3-incremental-cache/3`, Incremental profile only): each unit's generated text is keyed by its own source hash, the interface hashes of its dependencies, and a whole-program shape hash. A hit splices the cached fragments byte-identically — the unit's re-emission is skipped entirely, even before the C++ compiler is involved.

The PCH (Incremental only) precompiles the always-on headers; it is rebuilt only when its own stamp moves, and a PCH failure never fails the build — translation units just compile without it.

In practice: a no-change rebuild invokes no Clang and no link at all; an edit to one file recompiles one unit (plus anything whose interface hash moved) and relinks.

Useful commands around this machinery:

- `cp3c run` — incremental build + execute.
- `cp3c warm` — the same full build, but never executes; fills the cache.
- `cp3c build` — release build, no cache reuse.
- `cp3c clean` — delete `.cp3/build`, `.cp3/cache`, `.cp3/out`.
- `--show-runtime` — mirror the generated C++ and the selected runtime TUs for inspection.

## What CP3 deliberately does not do

- **No CMake for user programs.** cp3c invokes the Clang driver directly; the repository's own CMake build is only for building the compiler itself.
- **No IR or alternative backend.** "Fast to try new optimizations" would have cost every correctness property the type checker establishes up front.
- **No whole-program opaque recompilation in dev.** The plural pipeline exists precisely so edit-rebuild-restart stays local; `--release` is the explicit opt-in to global optimization.

## Related pages

- [Compilation pipeline](../cli-and-compiler/02-compilation-pipeline.md) — the user-facing stage summary and failure triage
- [Native toolchains](../cli-and-compiler/05-native-toolchains.md) — toolchain resolution, pins, and the wizard
- [Project settings and build cache](../cli-and-compiler/09-project-settings-and-cache.md) — manifest fields and cache behavior
- [Generated C++ inspection](../cli-and-compiler/10-generated-cpp-inspection.md) — reading the output this page produces
- [No garbage collector](./02-no-garbage-collector.md) — what the generated C++ says about your objects
