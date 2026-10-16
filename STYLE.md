# cp3c engineering standards

**Status: binding for everything under `src/**`.** Enforced by
`tools/lint_layout.py` (run via the CMake `lint` target). This is a compiler
meant to be maintained for years by many engineers; it must read like a
professional C++ codebase, not a generated blob. Small, single-purpose units are
how defects stay findable — **structure is part of correctness.**

Specifications: `docs/`. Decisions: `DECISIONS.md`. Module map: `ARCHITECTURE.md`.

---

## 1. Language & contracts

* **C++20**, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
* **No exceptions across module boundaries.** A stage reports through
  `DiagnosticSink`; it never throws and never prints. (Generated cp3 code may use
  exceptions — that is a codegen concern, invisible to the compiler's modules.)
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
// cp3-lint: allow-file-size — <required reason>
```

Suppressions are for interface/data headers only. **Logic files are never
exempt** (`lexer.cpp`, `parser.cpp`, `sema.cpp`, `codegen.cpp`, `cli.cpp` are
split, not suppressed).

## 3. File & directory layout (per module)

```
src/<module>/
  CMakeLists.txt          # this module owns its target + source list
  include/cp3/<name>.hpp  # the module's PUBLIC boundary (frozen)
  src/<unit>.cpp          # implementation — one concern per file
  src/detail/<helper>.h   # PRIVATE headers (never included cross-module)
  tests/<unit>_test.cpp   # unit tests (lightweight, no third party)
```

* Public headers: `#pragma once`; include only what is used (IWYU);
  forward-declare where possible.
* Private headers live under `src/<module>/src/` and are never included by
  another module.
* **One primary concern per file**; the file name is the unit name
  (`snake_case`). A 2,700-line translation unit is a bug in the layout.
* Adding a file means editing **only that module's `CMakeLists.txt`** — never a
  shared root file. This is what lets modules be worked on in parallel.

## 4. Naming (codified from the existing code — keep it consistent)

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
* No commented-out code — delete it; git remembers.
* `TODO(<owner>):` with a concrete owner, never a bare `TODO`.

## 6. Error handling & diagnostics

* Recover, don't abort: a stage reports and keeps going where the language allows
  it, returning a best-effort result. Never crash on malformed input.
* Diagnostic codes come from `cp3/diagnostic_codes.hpp`; append new codes inside
  their stage range, never renumber.

## 7. Testing

* Each module ships `src/<module>/tests/*_test.cpp` — a tiny assert-based harness
  (no third-party framework), runnable by direct compile.
* The conformance corpus `tests/cases/` (843 real cases + 110 ambiguity markers)
  is the **integration oracle**; unit tests cover internals.

## 8. Required target decomposition

Each module below is split into the listed units. This is the target layout; the
refactor passes bring existing code to it.

* **common** — already small: `source.cpp`, `diagnostic.cpp`, `token.cpp`,
  `ast.cpp`, `module.cpp`, `types.cpp`, `version.cpp` (keep).
* **lexer** — `lexer.cpp` (entry/driver), `scanner.cpp/.h` (cursor, trivia,
  positions, recovery), `numbers.cpp`, `strings.cpp`, `templates.cpp`,
  `regexp.cpp`, `keywords.cpp/.h` (keyword table).
* **parser** — `parser.cpp` (entry/orchestration), `statements.cpp`,
  `expressions.cpp`, `declarations.cpp`, `types.cpp`, `patterns.cpp`,
  `tokens.cpp/.h` (cursor + error recovery helpers), `precedence.h`.
* **sema** — `semantic_analyzer.cpp` (entry), `scopes.cpp/.h`, `type_check.cpp`,
  `expr_check.cpp`, `stmt_check.cpp`, `symbols.cpp`, `banned.cpp`.
* **codegen** — `code_generator.cpp` (entry), `emit_expr.cpp`, `emit_stmt.cpp`,
  `emit_decl.cpp`, `cpp_writer.cpp/.h` (indent buffer), `mangle.cpp/.h`.
* **runtime** — already multi-file; keep the policy (headers declare, `.cpp`
  defines).
* **cli** — already multi-file; move any substantial logic out of
  `include/private/*.hpp` into `.cpp` (headers declare, never define).

## 9. Enforcement

```
python tools/lint_layout.py            # report; exit 1 on hard violations
python tools/lint_layout.py --warn-only
cmake --build <build> --target lint
```

The lint target is advisory because module authors run in parallel and a warning
from one module must never block another; **hard violations must be fixed before
a module is considered done.**
