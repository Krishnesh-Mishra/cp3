---
title: How a build flows
---
# How a build flows

You don't need to know the internals to use CP3 — but when a build fails, knowing *where* it failed makes the fix obvious. This page is the user-level map: what each command does, where the files go, and how to recognize each kind of failure.

## The flow, end to end

```text
.cp3 source ──► front end ──► code generation ──► clang++ ──► .cp3/out
```

| Command | What happens |
| --- | --- |
| `cp3 check` | Front end only — parse, resolve modules, type-check. Stops before C++ is generated. |
| `cp3 run` | Full flow, evaluated incrementally (cached), then the program executes. |
| `cp3 build` | Full flow, release settings; the executable is never run. |
| `cp3 warm` | Full flow; artifacts are cached, nothing executes. |
| `cp3 dev` | The flow in a watch loop; saves trigger rebuild + restart. |

## Where the files go

Everything a build produces lives under `<project-root>/.cp3/` (the project root is the nearest directory containing `package.json`):

| Entry | Holds |
| --- | --- |
| `.cp3/build/` | Generated C++ for your program (and C++ for installed packages' sources). |
| `.cp3/cache/` | Compiled objects for your sources and runtime units, the build manifest, and the link stamp. Incremental builds reuse these when nothing relevant changed. |
| `.cp3/out/` | The linked executable (`<entry-name>[.exe]`), or a `.cp3pak` for package projects. |
| `.cp3/packages/`, `.cp3/native/`, `.cp3/link/`, `.cp3/lock.json` | Installed and linked CP3 packages, native SDK bindings, and the lockfile. |

`cp3 clean` deletes `.cp3/build`, `.cp3/cache`, and `.cp3/out` (and keeps `.cp3/native`). `cp3 clean --all` also clears `.cp3/packages`, `.cp3/native` and `.cp3/lock.json`.

## Reading a failure

Match the output to its stage:

| Output clue | Stage | What it means |
| --- | --- | --- |
| `CP3xxxx` code with a `.cp3` file and span | Lex, parse, module resolution, type check | CP3 rejected the source. Fix the code; the target binary was never built. |
| A `.cpp` file with a C++ diagnostic | Compile | The generated or runtime C++ failed to compile — a compiler bug or an incompatible toolchain; report it with the reproducer. |
| `undefined symbol` / missing library | Link | A native dependency is missing, or the toolchain/target doesn't match. Check the toolchain selection. |
| Stack trace / thrown value after launch | Run | The program built and ran; the failure is in the program. |

Two things worth remembering:

1. A green `cp3 check` does not prove a native build will succeed; it's only the front-end stage.
2. A C++ error in a generated file always has a CP3 source construct behind it — attach that source to the report, not just the generated file.

## Which command for which job

- Fast feedback while editing: `cp3 check` (front end only, no C++) or `cp3 run` (cached, real execution).
- Watch mode: `cp3 dev`; pre-warm the loop with `cp3 warm` before a long session.
- Shippable binary: `cp3 build` (release).
- Fast test pass: `cp3 test` (builds and runs your test files).
- After changing toolchains, clearing a stale cache: `cp3 clean`; when package caches are suspect: `cp3 clean --all`.

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Diagnostics and generated help](./06-diagnostics.md)
- [Compiler design](../internal-engineering/01-compiler-design.md) — how it works under the hood
