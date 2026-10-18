# cp3c engineering standards

**Status: binding for everything under `src/**`.** Enforced by
`tools/lint_layout.py` (run via the CMake `lint` target). This is a compiler
meant to be maintained for years by many engineers; it must read like a
professional C++ codebase, not a generated blob. Small, single-purpose units are
how defects stay findable - **structure is part of correctness.**

Specifications: `docs/`. Decisions: `DECISIONS.md`. Module map: `ARCHITECTURE.md`.

---

## 1. Language & contracts

* **C++20**, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
* **No exceptions across module boundaries.** A stage reports through
  `DiagnosticSink`; it never throws and never prints. (Generated cp3 code may use
  exceptions - that is a codegen concern, invisible to the compiler's modules.)
* **RAII only.** No raw `new`/`delete`; no owning raw pointers. Use values,
  `std::unique_ptr`, `std::vector`. Pass by value or `const&`.
* **No third-party libraries.** Standard C++20 only.
* Namespace `cp3`, nested per module (`cp3::runtime`). No `using namespace` in a
  header. Never `using namespace std;`.

## 2. Hard budgets (enforced by lint)

| Unit | Soft | **Hard** |
| --- | ---: | ---: |
| Source file (`.cpp`, private header) | 400 | **500** |
| Public header (`include/cp3/*.hpp`) | 500 | **600** |
| Function / method | 60 | **80** |
| Type (`class`/`struct`) | 250 | **400** |
| Line length | 100 | **100** |
| Parameters | 4 | **5** (else an options struct) |
| Nesting depth | 3 | **4** |

A file at the cap is split **by concern, not by line count.** A genuine exception
(e.g. a data-definition header) must carry an explicit suppression:

```cpp
// cp3-lint: allow-file-size - <required reason>
```

Suppressions are for interface/data headers only. **Logic files are never
exempt** (`lexer.cpp`, `parser.cpp`, `sema.cpp`, `codegen.cpp`, `cli.cpp` are
split, not suppressed).

## 3. File & directory layout (per module)

```
src/<module>/
  CMakeLists.txt          # this module owns its target + source list
  include/cp3/<name>.hpp  # the module's PUBLIC boundary (frozen)
  src/<unit>.cpp          # implementation - one concern per file
  src/detail/<helper>.h   # PRIVATE headers (never included cross-module)
  tests/<unit>_test.cpp   # unit tests (lightweight, no third party)
```

* Public headers: `#pragma once`; include only what is used (IWYU);
  forward-declare where possible.
* Private headers live under `src/<module>/src/` and are never included by
  another module.
* **One primary concern per file**; the file name is the unit name
  (`snake_case`). A 2,700-line translation unit is a bug in the layout.
* Adding a file means editing **only that module's `CMakeLists.txt`** - never a
  shared root file. This is what lets modules be worked on in parallel.

## 4. Naming (codified from the existing code - keep it consistent)

| Entity | Convention | Example |
| --- | --- | --- |
| Type (`class`/`struct`/`enum class`) | `PascalCase` | `SourceSpan`, `SemaResult` |
| Function / method | `camelCase` | `parseProgram`, `addUnit` |
| Local / parameter | `camelCase` | `beginOffset`, `moduleName` |
| Private data member | `camelCase_` | `errorCount_`, `sink_` |
| Constant | `kPascalCase` | `kExitSuccess`, `kVersion` |
| Macro | `CP3_UPPER_SNAKE` | `CP3_AST_NODE_LIST` |
| Enumerator | `PascalCase` | `TokenKind::TemplateHead` |
| File | `snake_case.{hpp,cpp}` | `diagnostic_codes.hpp` |
| Test | `<unit>_test.cpp` | `scanner_test.cpp` |

## 5. Comments, docs, TODOs

* Every file opens with a banner: purpose, ownership, and the spec/decision
  references it implements.
* Public headers: Doxygen `///` on every public type and function.
* No commented-out code - delete it; git remembers.
* `TODO(<owner>):` with a concrete owner, never a bare `TODO`.

## 6. Error handling & diagnostics

* Recover, don't abort: a stage reports and keeps going where the language allows
  it, returning a best-effort result. Never crash on malformed input.
* Diagnostic codes come from `cp3/diagnostic_codes.hpp`; append new codes inside
  their stage range, never renumber.

## 7. Testing

* Each module ships `src/<module>/tests/*_test.cpp` - a tiny assert-based harness
  (no third-party framework), runnable by direct compile.
* The conformance corpus `tests/cases/` (843 real cases + 110 ambiguity markers)
  is the **integration oracle**; unit tests cover internals.

## 8. Required target decomposition

Delivered by the D66.5/D68 structure sweep; keep new code in this shape.
Private headers live in `detail/`; a multi-unit concern gets its own
subdirectory (`emit/`, `checks/`, `support/`, `commands/`, `pkg/`, `archive/`,
`toolchain/`). One unit per file, the file name is the unit name.

* **common** - already small: `source.cpp`, `diagnostic.cpp`, `token.cpp`,
  `ast.cpp`, `module.cpp`, `types.cpp`, `version.cpp` (keep).
* **lexer** - `lexer.cpp` (entry/driver), `scanner.cpp` (cursor, trivia,
  positions, recovery), `numbers.cpp`, `strings.cpp`, `templates.cpp`,
  `regexp.cpp`, `keywords.cpp` (keyword table); private headers
  `detail/scanner.h`, `detail/keywords.h`.
* **parser** - `parser.cpp` (entry/orchestration), `statements.cpp`,
  `declarations.cpp`, `members.cpp`, `patterns.cpp`, `types.cpp`,
  `expressions.cpp`, `primary.cpp`, `literals.cpp`, `node_ids.cpp`,
  `tokens.cpp` (cursor + error recovery helpers); private header
  `detail/tokens.h`.
* **sema** - `semantic_analyzer.cpp` (entry); `checks/` - `type_check.cpp`,
  `assignability.cpp`, `expr_check.cpp`, `member_check.cpp`, `call_check.cpp`,
  `ops_check.cpp`, `stmt_check.cpp`, `banned.cpp`, `parallel_check.cpp`,
  `parallel_expr_check.cpp`, `std_check.cpp`; `support/` - `scopes.cpp`,
  `symbols.cpp`, `declarations.cpp`; private headers `detail/analyzer.h`,
  `detail/scopes.h`, `detail/parallel_check.h`.
* **codegen** - `code_generator.cpp` (entry); `emit/` - `expr.cpp`, `stmt.cpp`,
  `decl.cpp`, `call.cpp`, `async.cpp`; `emit/std/` - `common.cpp` (shared
  dispatch) plus one unit per std family (`math.cpp`, `os.cpp`, `path.cpp`,
  `process.cpp`, `json.cpp`, `string.cpp`); `support/` - `cpp_writer.cpp`,
  `mangle.cpp`, `type_map.cpp`, `int_reads.cpp`, `int_mode.cpp`; private
  headers `detail/generator.h`, `detail/cpp_writer.h`, `detail/mangle.h`,
  `detail/type_map.h`, `detail/int_reads.h`, `detail/builtin_tables.h`.
* **runtime** - public headers in `include/cp3/{core,async,parallel,std}/`
  (`core/`: runtime, value, error, maybe, arith; `async/`: promise,
  promise_impl, promise_combinators, eventloop; `parallel/`: parallel, worker;
  `std/`: console, json, math, os, path, process, string); implementation
  `.cpp` files under `src/{core,async,parallel,std}/` mirroring those
  families; tests stay in `tests/`.
* **cli** - `cli.cpp` (entry); `commands/` - `build.cpp`, `check.cpp`,
  `clean.cpp`, `common.cpp`, `dev.cpp`, `exec.cpp`, `fmt.cpp`, `init.cpp`,
  `install.cpp`, `pack.cpp`, `run.cpp`; `pkg/` - `cache.cpp`, `checksum.cpp`,
  `cp3pak.cpp`, `json.cpp`, `lockfile.cpp`, `package_json.cpp`, `scripts.cpp`,
  `semver.cpp`, `signatures.cpp`; `archive/` - `deflate.cpp`, `gzip.cpp`,
  `inflate.cpp`, `tar.cpp`; `toolchain/` - `diagnostics.cpp`, `pipeline.cpp`,
  `toolchain.cpp`, `util.cpp`; private headers `detail/*.hpp` (headers
  declare, never define).
* **modules** - `module_loader.cpp` (entry/orchestration); `support/` -
  `unit_loader.cpp`, `import_resolver.cpp`, `specifier.cpp`, `file_paths.cpp`,
  `export_surface.cpp`, `prelude_scan.cpp`, `std_registry.cpp`; private
  headers `detail/loader.h`, `detail/specifier.h`, `detail/file_paths.h`,
  `detail/export_surface.h`, `detail/prelude_scan.h`, `detail/std_registry.h`.

## 9. Enforcement

```
python tools/lint_layout.py            # report; exit 1 on hard violations
python tools/lint_layout.py --warn-only
cmake --build <build> --target lint
```

The lint target is advisory because module authors run in parallel and a warning
from one module must never block another; **hard violations must be fixed before
a module is considered done.**
