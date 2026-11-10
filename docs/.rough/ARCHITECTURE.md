# cp3c architecture

`cp3c` is the reference compiler for **C+++** (`.cp3`). It transpiles `.cp3`
source to C++20 (one `.cpp`/`.hpp` pair per `.cp3` file) and drives a system C++
toolchain. This document is the map of the repository: what exists, who owns it,
how to build and run it, what is frozen, and what is still open.

> The language and tooling specifications live in [`docs/`](../).
> The key ones: [`docs/language-spec/`](../language-spec/) and
> [`docs/cli-tooling/`](../cli-tooling/). This file does not restate them; it
> says how *this* codebase is put together.

---

## 1. Module map

The compiler is a set of small static libraries plus one executable. Dependencies
flow downward only; there are no cycles.

| CMake target | Source | Include root | Role | Status |
| --- | --- | --- | --- | --- |
| `cp3_common` | `src/common/src/` | `src/common/include/` | **Frozen interfaces**: source/positions, tokens, AST, visitor, diagnostics, diagnostic codes, module graph, options, version. | Complete |
| `cp3_lexer` | `src/lexer/` | `src/lexer/include/` | `.cp3` text → `TokenStream`. | Implemented (full token set) |
| `cp3_parser` | `src/parser/` | `src/parser/include/` | `TokenStream` → AST (`Program`). | Implemented (full grammar) |
| `cp3_modules` | `src/modules/` | `src/modules/include/` | Module discovery, §18 prelude injection, D4 import resolution + validation (`cp3::ModuleLoader`). | Complete (M1 scope) |
| `cp3_sema` | `src/sema/`, `src/std/*/sema*.cpp` | `src/sema/include/` | AST → diagnostics + nodeId-keyed side tables. | Implemented (language waves; remaining gaps tracked per wave) |
| `cp3_codegen` | `src/codegen/`, `src/std/*/codegen*.cpp` | `src/codegen/include/` | Checked AST → generated C++20 (+ source map). | Implemented (language waves; remaining gaps tracked per wave) |
| `cp3_runtime` | `src/runtime/`, `src/std/*/runtime*.cpp` | `src/runtime/include/`, `src/std/*/include/` | Generated-code runtime support (std family TUs included, D72). | Minimal |
| `cp3_cli` | `src/cli/`, `src/std/*/native.hpp` | `src/cli/include/` | Command surface, argument parsing, all rendering. | Scaffold |
| `cp3c` | `src/main.cpp` | - | Executable; forwards `argv` to `runCli`. | Complete |

Dependency edges (declared in each module's `src/<module>/CMakeLists.txt`):

```
cp3c -> cp3_cli -> { cp3_common, cp3_lexer, cp3_parser, cp3_modules,
                     cp3_sema, cp3_codegen }
                        cp3_parser  -> cp3_lexer -> cp3_common
                        cp3_modules -> cp3_parser (-> cp3_lexer -> cp3_common)
                        cp3_sema    -> cp3_parser
                        cp3_codegen -> cp3_sema
                        cp3_runtime -> cp3_common   (not yet linked into cp3c)
```

---

## 2. Directory ownership

Parallel work happens in this repository, so **every path has exactly one
owner**. Touching another owner's path is how parallel agents collide. If you
need a change across a boundary, ask the owner.

| Path | Owner | Notes |
| --- | --- | --- |
| `CMakeLists.txt` | scaffolding/architecture | Only edit if the build is broken. |
| `src/common/include/cp3/**` | **lead (frozen)** | The frozen interface headers. Changes require lead sign-off. |
| `src/common/src/**` | scaffolding/architecture | Implementations *behind* the frozen headers. |
| `src/lexer/**` | lexer owner | Replace the stub body; keep the `Lexer` boundary. |
| `src/parser/**` | parser owner | Replace the stub body; keep the `Parser` boundary. |
| `src/modules/**` | modules owner | Module discovery, §18 prelude injection, import resolution + validation; keep the `ModuleLoader` boundary. Does not link sema/codegen. |
| `src/sema/**` | sema owner | Replace the stub body; keep the `SemanticAnalyzer` boundary and the frozen `src/sema/include/**` shape. |
| `src/codegen/**` | codegen owner | Replace the stub body; keep the `CodeGenerator` boundary and the frozen `src/codegen/include/**` shape. |
| `src/cli/**` | CLI/pipeline owner | Commands, flags, diagnostic rendering. |
| `src/runtime/**` | runtime owner | Runtime support surface; the core ABI in `src/runtime/include/**` is frozen (std families: `src/std/<family>/include/**`, D72). |
| `src/std/<family>/**` | that std family | Every phase slice of one std module (D72): `sema*.cpp`, `codegen*.cpp`, `runtime*.cpp`, `native.hpp`, `include/cp3/**`. |
| `src/main.cpp` | scaffolding/architecture | Trivial; keep it trivial. |
| `docs/rough/**` | scaffolding/architecture | Internal process docs and original spec sources: `ARCHITECTURE.md` (this file), `DECISIONS.md`, `M2-PLAN.md`, `STYLE.md`. The root stays lean (`README.md`, `.gitignore`, `CMakeLists.txt`). |
| `tests/runner/**` | scaffolding/architecture | The harness. |
| `tests/cases/README.md` | scaffolding/architecture | The frozen case format. |
| `tests/cases/<category>/**` | that category's owner | Each `tests/cases/<category>/` directory has one owner. `variables/` is owned by the variables owner and already holds 42 cases. |

**Rule:** an owner may freely edit files under their path; they may *read*
everything else. Frozen interfaces (`src/common/include/cp3/**`) and the case
format (`tests/cases/README.md`) are not to be reshaped - extend them only by
the documented mechanisms (see §7 and §8).

**Frozen by the contracts pass (DECISIONS D14):** the lead owns
`src/common/include/cp3/**`, and the *public interface shape* of the stage
headers - `src/sema/include/**`, `src/codegen/include/**`,
`src/runtime/include/**` - was extended and frozen by the contracts pass. Stage
owners implement *against* that shape (fill in the `.cpp` bodies and the
algorithms) and do not reshape the headers. If a stage needs a different shape,
that is an open question for the lead (§13), not a unilateral header edit.

---

## 3. Building

Requires CMake ≥ 3.20 and a C++20 compiler. `cp3c` itself is built with CMake by
any host C++ compiler; cp3 *projects* never use CMake (the compiler drives
`clang++` directly, per
[`docs/cli-tooling/01-compilation-pipeline.md`](../cli-tooling/01-compilation-pipeline.md)).

```bash
cmake -B build -S .      # configure
cmake --build build      # build all targets
```

The executable lands at `build/bin/cp3c` (`build\bin\cp3c.exe` on Windows).
In this environment the configure step selected **Ninja + the host C++ compiler**.

**One build tree, always under `build/` (D55).** There is exactly one canonical
tree - `cmake -S . -B build`, the directory the test harness defaults to
(`build/bin/cp3c`). A parallel author who needs an isolated tree configures a
private scratch tree *inside* it:

```bash
cmake -S . -B build/scratch/<name>    # private, ignored, deleted at will
```

Root-level `build-*` directories are **not allowed**; the old `build-*/` ignore
pattern was removed on purpose, so a stray one shows up as untracked in
`git status`. (This is a location rule only - module source lists stay
per-module, §3.1, which is what keeps parallel authors from colliding.)

### 3.1 Where the build is described

Module source lists live **with the module**, never in a shared file: adding a
`.cpp` to a module means editing **only that module's `CMakeLists.txt`**
(STYLE.md §3). That is what lets module owners refactor in parallel without
colliding in the repository root.

| File | Owns |
| --- | --- |
| `CMakeLists.txt` (root) | CMake floor, `project()`, the C++20 settings, the output directories, the `cp3_set_warnings()` helper, `find_package(Python3)`, `add_subdirectory(src)`, and the two repository-level convenience targets (`cp3c-tests`, `lint`). It lists **no module sources**. |
| `src/CMakeLists.txt` | `add_subdirectory()` of every module in *dependency order*, then the `cp3c` executable (`src/main.cpp`, linking `cp3_cli`). |
| `src/<module>/CMakeLists.txt` | That module's library target, its **source list**, its include directory, and its links. |

```
CMakeLists.txt                # root: toolchain floor, warnings, convenience targets
src/CMakeLists.txt            # modules in dependency order + the cp3c executable
src/common/CMakeLists.txt     # cp3_common  (links nothing)
src/lexer/CMakeLists.txt      # cp3_lexer   -> cp3_common
src/parser/CMakeLists.txt     # cp3_parser  -> cp3_lexer  (-> cp3_common)
src/modules/CMakeLists.txt    # cp3_modules -> cp3_parser (preludes, imports)
src/sema/CMakeLists.txt       # cp3_sema    -> cp3_parser
src/codegen/CMakeLists.txt    # cp3_codegen -> cp3_sema
src/runtime/CMakeLists.txt    # cp3_runtime -> cp3_common (+ Threads; not in cp3c yet)
src/cli/CMakeLists.txt        # cp3_cli     -> common, lexer, parser, modules, sema, codegen
```

Each module file has the same shape - declare the list, create the target, add
the include root, link the dependencies, apply the shared warnings:

```cmake
set(cp3_parser_sources
    src/parser.cpp
    src/statements.cpp
    # ... one line per unit (STYLE.md §8); this is the only file to edit
)
add_library(cp3_parser STATIC ${cp3_parser_sources})
target_include_directories(cp3_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cp3_parser PUBLIC cp3_common cp3_lexer)
cp3_set_warnings(cp3_parser)
```

The resulting dependency edges are exactly the ones in §1. `add_subdirectory`
order in `src/CMakeLists.txt` is what enforces "link only modules already
added", so the map above and that file must stay in sync. A module may add
opt-in unit-test targets of its own (e.g. `cp3_runtime`'s
`-DCP3_RUNTIME_TESTS=ON`, which builds `src/runtime/tests/*`); those are off by
default so the normal build stays lean.

### 3.2 Convenience targets

```bash
cmake --build build --target cp3c-tests   # Python 3 required (see below)
cmake --build build --target lint         # layout + zero-placeholder checks (see 3.4)
```

If Python 3 is found at configure time, `cp3c-tests` runs the conformance
harness over `tests/cases/`:

```bash
cmake --build build --target cp3c-tests
```

### 3.3 Warnings

Warnings are enabled (`-Wall -Wextra` for the host compiler) but
**`-Werror` is deliberately never set**, so one module author's warning cannot
block another's build. (See §11 for the one known warning.)

### 3.4 Layout lint and zero-placeholder gate

The `lint` target runs `python vendor/tools/lint.py`, which runs the STYLE.md
§2 hard budgets (file, function, type, line length, parameters, nesting) and the
§4 naming spot-checks (file names, macro names) over `src/**`. It is **advisory
by design**: it is not part of the default build (not in `ALL`), never depends
on `cp3c`, and never sets `-Werror` - a violation in one module must not block
another module's build (STYLE.md §9). The script exits non-zero when a hard
budget is exceeded and prints a per-file report with line numbers; run it
directly for the same result, or `--warn-only` to report without failing:

```bash
python vendor/tools/lint_layout.py
python vendor/tools/lint_layout.py --warn-only
```

Hard violations must be fixed before a module is considered done; the
suppression marker and the documented allow-list are described in the script
header.

The wrapper then runs `python vendor/tools/check_cp3002.py`, a hard gate over `src/`, `tests/`,
`docs/` and `dist/packaging/`. The gate prints every retired placeholder marker as
`path:line` and exits 1 while any remain. Run it on its own with
`cmake --build build --target lint-cp3002` or directly:

```bash
python vendor/tools/check_cp3002.py
python vendor/tools/check_cp3002.py --report
```

The full usage and the temporary baseline mode are documented in STYLE.md
section 9.

---

## 4. Running `cp3c`

```
cp3c <command> [options] [file]

Commands:
  run <file.cp3>     Build then execute a program
  build [file.cp3]   Transpile + link without executing
  check <file.cp3>   Frontend only: parse + type-check (no C++ generation)
  dev                Watch mode: re-check/rebuild on save
  fmt <file.cp3>     Zero-config formatter
  clean              Remove the .cp3/build cache (--all also clears .cp3/packages)
  init               Scaffold a new project (package.json + src)
  exec <pkg> [args]  Run a package's bundled tool
  version            Print the compiler version
  help               Print this help

Options:
  -h, --help            Print this help
  -v, --version         Print the compiler version
  --release             (build/run) whole-program build with LTO
  --debug               (build/run) -O0 plus debug info
  --max-memory <MB|N%>  Cap codegen-phase memory (default: 70% of system RAM)
```

`--version` prints `versionString()` (currently `cp3c 0.2.2`).
`--help` prints the usage above. `cp3c` with no arguments prints usage and exits 0.

### Exit-code contract

| Code | Meaning |
| --- | --- |
| `0` | success |
| `1` | compile error (diagnostics were printed) |
| `2` | usage error (bad flag/command; usage printed to stderr) |

There is no separate "not built yet" exit code: a construct the compiler cannot
lower is a compile error with a specific `CP3xxx` diagnostic, so tests never
mistake a stub for a skipped case.

---

## 5. C++ conventions

* **Language:** C++20, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
  C++20 is the floor, not C++23 - chosen for coroutines
  ([`docs/cli-tooling/01-...`](../cli-tooling/01-compilation-pipeline.md)).
* **No exceptions across module boundaries.** Every stage reports problems
  through a `DiagnosticSink`; it does not throw and does not print. Functions
  that can fail return `nullptr`, `bool`, or a status, never a thrown error.
  (Generated cp3 code may use C++ exceptions - that is a codegen concern and is
  invisible to the compiler's own modules.)
* **Plain structs + `unique_ptr` ABI.** AST/semantic data are plain structs with
  public fields; tree ownership is `std::unique_ptr`. There are no private
  fields or accessors on AST nodes. Types are passed across the static-library
  boundary by value or `const` reference.
* **No third-party libraries.** Standard C++20 only.
* **Namespaces:** everything is in `cp3` (`cp3::runtime` for the runtime header).
* **Headers:** one public header per module at `src/<module>/include/cp3/<name>.hpp`,
  included as `#include "cp3/<name>.hpp"`. The std families' generated-code ABI
  headers live in each family's tree (`src/std/<family>/include/cp3/**`, D72)
  and keep their established include spellings (`cp3/std/<name>.hpp`,
  `cp3/typed_array.hpp`). Use `#pragma once`.
* **`noexcept`** on trivial accessors and helpers; the `Node` hierarchy has a
  virtual destructor (so `dynamic_cast`/`static_cast` by `kind` both work).
* **Determinism:** iteration orders that affect output are sorted or
  insertion-ordered on purpose (e.g. the harness sorts discovered cases;
  `ModuleGraph::compilationOrder()` is a deterministic post-order).
* **Warnings** go through the single `cp3_set_warnings()` helper; never add
  `-Werror`.

---

## 6. Frozen interface inventory

These headers are the stable contract every stage codes against. One line each:

| Header | What it freezes |
| --- | --- |
| `cp3/source.hpp` | `SourcePos`, `SourceSpan`, `SourceFile`, `SourceManager`; 1-based line/col, 0-based byte offsets. |
| `cp3/token.hpp` | `TokenKind` (the complete token set), `Token`, `TokenStream`. |
| `cp3/ast.hpp` | The complete Beta One AST and the `CP3_AST_NODE_LIST` node registry. |
| `cp3/visitor.hpp` | `AstVisitor`/`ConstAstVisitor`, `dispatch()`, `AstWalker` - generated from the node registry. |
| `cp3/diagnostic.hpp` | `Severity`, `Diagnostic`, `DiagnosticSink`, `CollectingDiagnosticSink`, `NullDiagnosticSink`. |
| `cp3/diagnostic_codes.hpp` | The canonical `CP3xxx` code table, grouped by owning stage. |
| `cp3/module.hpp` | `ModuleResolution`, `PreludeKind`, `CompilationUnit`, `ModuleDependency`, `ModuleGraph`. |
| `cp3/options.hpp` | `TypeSystem`, `BuildProfile`, `CompileOptions`. |
| `cp3/types.hpp` | `TypeKind`, `Type` (the cp3 value-type model) + `typeKindName`/`describe`/`isPrimitive`/`isAssignable`/`unify`. |
| `cp3/analysis.hpp` | `SymbolInfo`, `FunctionInfo`, `SemaResult` - sema's nodeId-keyed side tables. |
| `cp3/version.hpp` | `kCompilerName`, `kVersion`, `kVersionStage`, `versionString()`. |
| `cp3/lexer.hpp` | `Lexer` (scan a `SourceFile` into a `TokenStream`). |
| `cp3/parser.hpp` | `Parser` (parse a `TokenStream` into a `Program`). |
| `cp3/sema.hpp` | `SemanticAnalyzer` (analyze a `Program` into diagnostics + a `SemaResult`). |
| `cp3/codegen.hpp` | `CodeGenerator` + `GeneratedModule` (`Program` + `SemaResult` → `.hpp`/`.cpp` pair). |
| `cp3/runtime.hpp` | The generated-code ABI: `Undefined`, `toString`/`print`/`println`, `cp3::Error`, plus `name()`/`version()`. |
| `cp3/cli.hpp` | `runCli`, the exit-code constants, and the scaffold marker. |

---

## 7. Constructs expressed by composition

Per the note in `cp3/ast.hpp`, several spec constructs have **no dedicated
node** and are modelled by composition. Every consumer must agree on these
shapes:

| Source construct | AST shape |
| --- | --- |
| `parallel.for(...)` / `parallel.reduce(...)` / `parallel.run(...)` | `CallExpr{ callee = MemberExpr{ Identifier("parallel"), "for" }, ... }` |
| `new Worker("path")` (file or inline arrow) | `NewExpr{ callee = Identifier("Worker") }` |
| `0..n` (range) | `RangeExpr` |
| `{ weak: value }` (object-literal weak link) | `WeakExpr` |
| `Object.keys(o)` / `.values(o)` / `.entries(o)` | `CallExpr{ callee = MemberExpr{ Identifier("Object"), ... } }`, expanded at compile time |

---

## 8. Extending the AST (policy)

`CP3_AST_NODE_LIST` in `cp3/ast.hpp` is the **single source of truth** for "which
nodes exist". It generates `NodeKind`, the forward declarations, and (in
`visitor.hpp`) the exhaustive visitor dispatch.

To add a node:

1. Add **one line** `X(MyNewNode)` to `CP3_AST_NODE_LIST` in `cp3/ast.hpp`.
2. Define `struct MyNewNode : <base> { ... }` whose constructor calls the base
   with `NodeKind::MyNewNode`.
3. That is all. `nodeKindName()`, `AstVisitor`, `ConstAstVisitor`, and both
   `dispatch()` overloads pick it up automatically - there is no second list to
   update and no way for them to drift.

`<base>` is `Expr`, `Stmt`/`Decl`, `TypeNode`, `ClassMember`,
`SignatureMember`, or `Node`. Because `ast.hpp` is a frozen header, adding a
node requires **lead sign-off**; if the build fails after adding one, the cause
is almost always a switch that was hand-written instead of generated from the
registry - fix the consumer, not the registry.

---

## 9. Module graph & preludes

`ModuleGraph` owns every `CompilationUnit` (one parsed `.cp3` file) and the
directed import edges (`ModuleDependency`). `addUnit()` assigns a stable
`id` (its index) and returns a raw pointer; `unit(id)` looks one up.

`compilationOrder()` is a depth-first **post-order** from the entry unit: each
unit is emitted *after* the units it depends on, each unit appears at most once,
and cycles are tolerated (ES-module semantics,
[`docs/language-spec/18-modules-imports.md`](../language-spec/18-modules-imports.md)).
It is implemented iteratively so long import chains cannot overflow the stack.

Prelude files inject imports into every file in scope. The three variants
(`PreludeKind`) and their stacking order are specified in §18:

| File | `PreludeKind` | Reach |
| --- | --- | --- |
| `_prelude.same.cp3` | `Same` | same folder only |
| `_prelude.cp3` | `Strong` | same folder + all subfolders, recursively |
| `_prelude.weak.cp3` | `Weak` | recursive, but stopped by any nested prelude |

When several apply, injection order is **same → strong → weak**; strong and weak
coexist independently. `CompilationUnit::injectedPreludes` records the ids of the
preludes actually injected into a unit, in application order.

---

## 10. Diagnostics & the machine-readable test contract

All stages report through `DiagnosticSink`; the CLI owns rendering. `cp3`
never shows a raw C++ error to the user
([`docs/cli-tooling/02-...`](../cli-tooling/02-error-diagnostics-source-mapping.md)).

For tests to assert on a diagnostic, the harness (`tests/runner/run_tests.py`)
reads the combined stdout/stderr and looks for lines in this canonical shape:

```
<path>:<line>:<col>: <severity>: <CODE>: <message>
```

e.g. `src/main.cp3:3:1: error: CP3408: cannot reassign to const 'z'`.

The case format and the `CP3xxxx` wildcard rules are frozen in
[`tests/cases/README.md`](../../tests/cases/README.md). Run the harness with:

```bash
python tests/runner/run_tests.py --cp3c build/bin/cp3c
```

It exits `0` when no case failed (including when everything is skipped because
the compiler is still a scaffold) and `1` if any case failed.

---

## 11. Known build warnings

* `src/common/src/token.cpp` - `tokenKindName()` returns the string literal
  `"??="`, which some compilers report as a trigraph (`-Wtrigraphs`). Harmless;
  it is left as-is because `token.cpp` is a frozen implementation. Revisit only
  if a compiler is configured with `-trigraphs`.

---

## 12. Milestone 1 scope

**Goal:** a minimal but *real* end-to-end slice - take a `.cp3` file all the way
to a running native executable. Nothing fancier than that earns its keep yet.

In scope:

* **Bindings:** `let` / `const` (and `var`) with primitive initializers
  (`number`, `string`, `boolean`), type inference, `const`-reassignment is an
  error (`CP3408`). Language rule:
  [`docs/language-spec/08-variable-bindings.md`](../language-spec/08-variable-bindings.md).
* **Functions:** `function name(params) { ... }`, calls, `return`.
* **`console.log(...)`**: prints to **stdout** (this is what tests compare),
  including template literals (`` `${age}` ``) and multiple arguments.
* **`if` / `else`**.
* **Binary operators:** arithmetic (`+ - * / % **`), comparison
  (`== != === !== < > <= >=`), logical (`&& ||`), and string concatenation.
* **Codegen:** one `.cpp`/`.hpp` pair per `.cp3` file, targeting C++20, then
  invoke the system Clang toolchain, link, and run.

Exit criteria: the `tests/cases/variables/` cases become runnable, and the
harness flips from "42 skipped" to real pass/fail.

Out of scope for M1 (explicitly deferred): classes/interfaces/generics, async
and `await`, the memory model and `weak`, preludes and module resolution beyond
`std/console`, `parallel.*` / `worker`, native interop, and the full diagnostics
translation subsystem. The compiler must not choke on them - it should report a
clear diagnostic rather than crash.

---

## 13. Open questions for the lead

These are genuine ambiguities found while scaffolding. **Nothing here has been
guessed silently** - each is a place where the spec/docs do not decide a
question this code must answer, or where two parts of the frozen surface
disagree. Until a lead answers, the scaffold takes the most conservative
non-committal position.

### Resolved by lead (see DECISIONS.md)

All thirteen items below were resolved by the lead in
[`DECISIONS.md`](DECISIONS.md). The numbered list is kept for provenance; the
resolution for each item is the linked decision, which is binding and is not
re-litigated by stage agents.

| # | Question (short) | Decision |
| --- | --- | --- |
| 1 | Compiler internal architecture / IR | **D1** - no IR; AST + nodeId side tables only. |
| 2 | Canonical machine-readable diagnostic line | **D2** - `<path>:<line>:<col>: <severity>: <CODE>: <message>`. |
| 3 | Command name `cp3` vs `cp3c` | **D3** - binary is `cp3c`; `cp3` is a future frontend alias. |
| 4 | Module-specifier classification | **D4** - `./`,`../`,`.cp3`→Local; `std/`→Std; `cpp/`→Cpp; `native`→Native; else Package. |
| 5 | Prelude graph semantics | **D5** - file-level Same→Strong→Weak; cross-folder composition deferred. |
| 6 | `nodeId` allocation contract | **D6** - 1-based, pre-order, `Program` root numbered, `0` = unassigned. |
| 7 | Template-literal re-lexing hook | **D7** - lexer scans templates flat; `rescanTemplateContinuation()` is obsolete. |
| 8 | `+` overloading | **D8** - strict; only `number+number`/`string+string`; else `CP3411`. |
| 9 | `number` printing format | **D9** - ECMAScript `Number::toString`, exact. |
| 10 | Entry point / top-level execution | **D10** - top-level statements become a synthesized `int main()`. |
| 11 | `console.log` backing | **D11** - cp3 runtime with standard C++; no `fmt` in M1. |
| 12 | Toolchain order vs. environment | **D12** - superseded: Clang-only target, newest usable Clang, explicit version/path override. |
| 13 | Object-property shorthand representation | **D13** - `{ a }` stores `value = Identifier("a")`; `nullptr` is recovery-only. |

### Original questions (provenance only)

1. **Compiler internal architecture / IR (largest open item).**
   [`docs/cli-tooling/06-open-tooling-questions.md`](../cli-tooling/06-open-tooling-questions.md)
   lists "Compiler implementation architecture itself (transpiler internals, IR
   if any, pass structure)" as genuinely open. Does the frontend lower to an IR,
   or go AST → C++ directly? This decides whether `sema`/`codegen` share a
   lowered form or communicate only through the frozen AST + nodeId side tables.
   *Not blocking the scaffold, but it is the first thing the stage owners need
   decided.*

2. **Canonical machine-readable diagnostic line format.**
   [`docs/cli-tooling/02-...`](../cli-tooling/02-error-diagnostics-source-mapping.md)
   shows human-oriented examples (`src/utils.cp3:14:8 - type mismatch: ...`) that
   carry **no error code and no severity token**, yet the frozen test schema
   asserts `{ code, line, col, contains }` and `diagnostic_codes.hpp` defines
   `CP3xxx` codes. The scaffold invents and documents the format
   `<path>:<line>:<col>: <severity>: <CODE>: <message>` - the lead must confirm
   it (or replace it), because every error test depends on it.

3. **Command name: `cp3` or `cp3c`?**
   The specification and all docs use the command `cp3` (`cp3 run`, `cp3 check`),
   while this repository builds a binary named `cp3c` (and `versionString()`
   returns `cp3c ...`). Which is the user-facing name? It affects `--help`,
   `--version`, and any test that asserts on the usage banner.

4. **Module-specifier classification rules.**
   `ModuleResolution` (`Local`/`Std`/`Package`/`Native`/`Cpp`/`Unknown`) exists,
   but §18 gives no algorithm mapping a specifier to a category. In particular:
   is every bare specifier (`"mathkit"`) automatically `Package`, and how should
   `import "cpp/iostream" as io` classify - `Cpp` or `Native`? The lead should
   pin the exact rule before module resolution is implemented.

5. **Prelude graph semantics across folders.**
   §18 fixes the *file-level* stacking order (same → strong → weak) and the weak
   "wall" rule, but not how nested folders' preludes compose in the
   `ModuleGraph`, nor the exact contents/order of
   `CompilationUnit::injectedPreludes` when several preludes from different
   ancestors apply. The `PreludeKind` enum is in the frozen header, so the
   semantics need deciding.

6. **`nodeId` allocation contract.**
   `ast.hpp` says `nodeId` is "assigned by the parser; stable for a compilation"
   and defaults to `0`, but does not say whether `0` is a valid id, whether ids
   are 1-based, or whether the `Program` root itself is numbered. Sema/codegen
   side tables key on `nodeId`, so the contract must be explicit.

7. **Template-literal re-lexing hook is missing from the frozen interface.**
   `token.hpp`'s comment describes `TokenStream::rescanTemplateContinuation()`,
   called by the parser to re-lex `TemplateMiddle`/`TemplateTail`, but
   `TokenStream` declares **no such method** (and no callback field). As frozen,
   there is no way for the lexer and parser to implement templated string
   interpolation as described. Either the frozen header needs the hook, or the
   docs' re-lexing model needs to change.

8. **`+` overloading: string concatenation vs mixed-type arithmetic.**
   §23 says mixed-type arithmetic "requires explicit conversion"
   (`CP3411`), but the `string_concat` case (and JS parity, which the spec
   repeatedly promises) implies `"a" + "b"` - and likely `"x" + 1` - must work.
   Is `+` with one string operand special-cased (JS semantics), or is
   `string + number` an error? This is a real semantic fork for M1's binary-op
   handling.

9. **`number` printing format.**
   Existing cases expect `console.log` of a `number` to print `18`, not `18.0`.
   §23 says `number` is a 64-bit double. The exact number→string rules
   (integers, fractional values, `-0`, `Infinity`, `NaN`, exponent thresholds)
   aren't specified, yet `stdout` in tests is compared **exactly**. The lead
   should pin JS-compatible number formatting (the cases will encode it).

10. **Entry point / top-level execution model.**
    `cp3 run src/index.cp3` implies the entry file's top-level statements run,
    and §18's examples call `console.log("Hello")` at top level. But it is not
    stated whether the compiler synthesizes `int main()` from the entry unit's
    top-level statements, requires an explicit `main()` function, or both. This
    decides how M1 generates and links an executable.

11. **`console.log` backing without third-party libraries.**
    §23 and the stdlib docs say `console.log` routes into **fmt** (a bundled
    third-party library), but this compiler build is standard-C++-only. For M1,
    may `console.log` temporarily generate `std::cout` (and a small internal
    formatter) until fmt is wired in, or is fmt a hard M1 dependency?

12. **Toolchain order vs. this environment.**
    **Resolved for beta one:** the target toolchain is Clang-only. cp3c
    auto-detects the newest usable Clang, allows an explicit version or path,
    and on Windows targets MinGW-w64 GNU with a discovered sysroot. There is no
    GCC or MSVC fallback ([`docs/cli-tooling/01-...`](../cli-tooling/01-compilation-pipeline.md)).

13. **Object-property shorthand representation is self-contradictory.**
    `ast.hpp` documents `ObjectProperty::value` as "may be nullptr for shorthand
    (`{ a }` -> value = Identifier a)". Those two halves disagree: is `value`
    `nullptr` for `{ a }`, or is it an `Identifier`? The parser and every
    consumer that walks object literals need one answer.

---

## 14. What "frozen" means here

A frozen interface is *implemented against*, never reshaped. If a stage needs a
field or a function the frozen header does not have, that is an **open question
for the lead** (§13), not a license to edit `src/common/include/cp3/**`. The two
sanctioned extension mechanisms are:

* **New AST nodes** - one line in `CP3_AST_NODE_LIST` (§8).
* **New diagnostic codes** - appended inside an existing stage range in
  `diagnostic_codes.hpp` (never renumbered).
