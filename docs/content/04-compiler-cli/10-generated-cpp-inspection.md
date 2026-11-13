---
title: Generated C++ inspection
chapter: Chapter 4 · Compiler and tools
---
# Generated C++ inspection

Generated C++ is an important debugging artifact, but it is not CP3 source and not the language's normative contract. Use it to determine what the compiler emitted for a particular build, then trace unexpected behavior back to the owning CP3 compiler or runtime layer.

## Preserve the right build output

For application builds, pass `--show-runtime` or its `--show` alias to copy the runtime headers and translation units used by the build into `.cp3/build/_runtime/`, laid out exactly like the repository. The mirror is a show-only aid: it is created only when missing, never re-copied or rewritten by later builds, and the build never compiles from it — the runtime objects it links live in `.cp3/cache/_runtime/`. A `README.md` in the mirror states that editing those files changes nothing; delete it (or run `cp3c clean`) and pass `--show-runtime` again to regenerate it from the current sources. Keep the exact output alongside the compiler version and build profile when reporting a code-generation issue.

The generated source may be split across module translation units and support files. Start with the module containing the source function, then follow included helpers and runtime calls. Do not search only one generated file and conclude that no helper exists elsewhere.

## Map source constructs to generated code

A useful inspection table is:

| CP3 construct | What to locate |
| --- | --- |
| `new Class()` | Constructor call, allocation helper, and resulting handle or local object |
| Array literal / `push` | Vector creation, element type, capacity hint, growth helper |
| Closure | Closure object, captured fields, mutable capture cell, call form |
| Typed-array/DataView access | Offset calculation, helper call, range check, byte-order conversion |
| `await` | Promise continuation and event-loop scheduling path |
| Standard module call | Codegen intrinsic or runtime function and its error path |

The exact generated C++ names can change. Follow the expression to its call target and inspect that implementation rather than depending on a generated spelling in application code.

## Determine whether an optimization fired

A source-level optimization or semantic fact only matters if the emitter consumes it. Look for the expected change in the hot code: a removed allocation, one reserve before a loop, a direct memory access, or eliminated helper calls. If the output still contains the original checked helper, the transformation did not reach that use.

When a pass is proof-gated, inspect both positive and negative examples. The positive case should contain the fast form; a case with aliasing, mutation, unknown calls, or unsupported syntax should retain the safe fallback.

## Match optimization reports to generated source

Pass backend report flags through the same native toolchain command that builds the generated program. Keep the report's filename and source line. A diagnostic pointing to `shared_ptr` internals, a runtime header, or a support function is not evidence that a benchmark's numeric loop was vectorized.

For vectorization, require a report tied to the generated loop and inspect the reported vector width. For escape analysis, inspect the transformed object storage. For array reservation, inspect the vector reserve call and confirm its placement before the loop.

## Debug compile and link failures

If generated C++ fails, record the full native command, first compiler error, generated source path, and CP3 source location map. Determine whether the failure comes from a compiler bug, a missing include, an unsupported target feature, or invalid CP3 reaching codegen. Fix the owning CP3 source where possible; do not patch an ephemeral generated file as the final fix.

A link failure is different from a C++ compile failure. Check undefined symbols, library order, target architecture, runtime objects, and toolchain selection. A successful `cp3 check` says nothing about either native stage.

## Generated artifacts are disposable

Build output can be deleted and regenerated. Commit compiler source, tests, runtime code, or intentionally maintained generated assets only when the repository's workflow requires them. Do not commit transient benchmark executables, temporary reports, or generated application C++ unless explicitly requested.

## Related pages

- [Compilation pipeline](02-compilation-pipeline.md)
- [Optimization pipeline](03-optimization-pipeline.md)
- [Tests and compiler verification](08-tests-and-verification.md)
- [Inspecting runtime costs](../02-runtime/11-inspecting-runtime-costs.md)
