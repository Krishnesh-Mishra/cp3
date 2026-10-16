# cp3c architecture

`cp3c` is the reference compiler for **C+++** (`.cp3`). It transpiles `.cp3`
source to C++20 (one `.cpp`/`.hpp` pair per `.cp3` file) and drives a system C++
toolchain. This document is the map of the repository: what exists, who owns it,
how to build and run it, what is frozen, and what is still open.

> The language and tooling specifications live in [`docs/`](docs/).
> The key ones: [`docs/language-spec/`](docs/language-spec/) and
> [`docs/cli-tooling/`](docs/cli-tooling/). This file does not restate them; it
> says how *this* codebase is put together.

---

## 1. Module map

The compiler is a set of small static libraries plus one executable. Dependencies
flow downward only; there are no cycles.

| CMake target | Source | Include root | Role | Status |
| --- | --- | --- | --- | --- |
| `cp3_common` | `src/common/src/` | `src/common/include/` | **Frozen interfaces**: source/positions, tokens, AST, visitor, diagnostics, diagnostic codes, module graph, options, version. | Complete |
| `cp3_lexer` | `src/lexer/` | `src/lexer/include/` | `.cp3` text → `TokenStream`. | Stub (EOF only) |
| `cp3_parser` | `src/parser/` | `src/parser/include/` | `TokenStream` → AST (`Program`). | Stub (empty `Program`) |
| `cp3_sema` | `src/sema/` | `src/sema/include/` | AST → diagnostics + nodeId-keyed side tables. | Stub (no checks) |
| `cp3_codegen` | `src/codegen/` | `src/codegen/include/` | Checked AST → generated C++20 (+ source map). | Stub (empty output) |
| `cp3_runtime` | `src/runtime/` | `src/runtime/include/` | Placeholder for generated-code runtime support. | Minimal |
| `cp3_cli` | `src/cli/` | `src/cli/include/` | Command surface, argument parsing, all rendering. | Scaffold |
| `cp3c` | `src/main.cpp` | — | Executable; forwards `argv` to `runCli`. | Complete |

Dependency edges (declared in `CMakeLists.txt`):

```
cp3c -> cp3_cli -> { cp3_common, cp3_lexer, cp3_parser, cp3_sema, cp3_codegen }
                        cp3_parser -> cp3_lexer -> cp3_common
                        cp3_sema   -> cp3_parser
                        cp3_codegen-> cp3_sema
                        cp3_runtime-> cp3_common   (not yet linked into cp3c)
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
| `src/sema/**` | sema owner | Replace the stub body; keep the `SemanticAnalyzer` boundary. |
| `src/codegen/**` | codegen owner | Replace the stub body; keep the `CodeGenerator` boundary. |
| `src/cli/**` | CLI/pipeline owner | Commands, flags, diagnostic rendering. |
| `src/runtime/**` | runtime owner | Runtime support surface. |
| `src/main.cpp` | scaffolding/architecture | Trivial; keep it trivial. |
| `ARCHITECTURE.md` | scaffolding/architecture | This file. |
| `tests/runner/**` | scaffolding/architecture | The harness. |
| `tests/cases/README.md` | scaffolding/architecture | The frozen case format. |
| `tests/cases/<category>/**` | that category's owner | Each `tests/cases/<category>/` directory has one owner. `variables/` is owned by the variables owner and already holds 42 cases. |

**Rule:** an owner may freely edit files under their path; they may *read*
everything else. Frozen interfaces (`src/common/include/cp3/**`) and the case
format (`tests/cases/README.md`) are not to be reshaped — extend them only by
the documented mechanisms (see §7 and §8).

---

## 3. Building

Requires CMake ≥ 3.20 and a C++20 compiler. `cp3c` itself is built with CMake;
cp3 *projects* never are (the compiler drives `cl.exe`/`clang++`/`g++` directly,
per [`docs/cli-tooling/01-compilation-pipeline.md`](docs/cli-tooling/01-compilation-pipeline.md)).

```bash
cmake -B build -S .      # configure
cmake --build build      # build all targets
```

The executable lands at `build/bin/cp3c` (`build\bin\cp3c.exe` on Windows).
In this environment the configure step selected **Ninja + GNU 16.1.0**.

If Python 3 is found at configure time, a convenience target is also wired up:

```bash
cmake --build build --target cp3c-tests
```

Warnings are enabled (`-Wall -Wextra`, or `/W4 /permissive-` on MSVC) but
**`-Werror` is deliberately never set**, so one module author's warning cannot
block another's build. (See §11 for the one known warning.)

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

`--version` prints `versionString()` (currently `cp3c 0.1.0-beta.1`).
`--help` prints the usage above. `cp3c` with no arguments prints usage and exits 0.

### Exit-code contract

| Code | Meaning |
| --- | --- |
| `0` | success |
| `1` | compile error (diagnostics were printed) |
| `2` | usage error (bad flag/command; usage printed to stderr) |
| `3` | **not implemented** — this is the scaffold build; a marker line containing `not implemented` is printed to stderr |

The `3` + marker convention is what lets `tests/runner/run_tests.py` tell "the
scaffold ran" apart from a genuine failure, so real test cases are *skipped*
rather than failed while a stage is still a stub. Keep this contract when the
compile commands come online.

---

## 5. C++ conventions

* **Language:** C++20, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
  C++20 is the floor, not C++23 — chosen for coroutines
  ([`docs/cli-tooling/01-...`](docs/cli-tooling/01-compilation-pipeline.md)).
* **No exceptions across module boundaries.** Every stage reports problems
  through a `DiagnosticSink`; it does not throw and does not print. Functions
  that can fail return `nullptr`, `bool`, or a status, never a thrown error.
  (Generated cp3 code may use C++ exceptions — that is a codegen concern and is
  invisible to the compiler's own modules.)
* **Plain structs + `unique_ptr` ABI.** AST/semantic data are plain structs with
  public fields; tree ownership is `std::unique_ptr`. There are no private
  fields or accessors on AST nodes. Types are passed across the static-library
  boundary by value or `const` reference.
* **No third-party libraries.** Standard C++20 only.
* **Namespaces:** everything is in `cp3` (`cp3::runtime` for the runtime header).
* **Headers:** one public header per module at `src/<module>/include/cp3/<name>.hpp`,
  included as `#include "cp3/<name>.hpp"`. Use `#pragma once`.
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
| `cp3/visitor.hpp` | `AstVisitor`/`ConstAstVisitor`, `dispatch()`, `AstWalker` — generated from the node registry. |
| `cp3/diagnostic.hpp` | `Severity`, `Diagnostic`, `DiagnosticSink`, `CollectingDiagnosticSink`, `NullDiagnosticSink`. |
| `cp3/diagnostic_codes.hpp` | The canonical `CP3xxx` code table, grouped by owning stage. |
| `cp3/module.hpp` | `ModuleResolution`, `PreludeKind`, `CompilationUnit`, `ModuleDependency`, `ModuleGraph`. |
| `cp3/options.hpp` | `TypeSystem`, `BuildProfile`, `CompileOptions`. |
| `cp3/version.hpp` | `kCompilerName`, `kVersion`, `kVersionStage`, `versionString()`. |
| `cp3/lexer.hpp` | `Lexer` (scan a `SourceFile` into a `TokenStream`). |
| `cp3/parser.hpp` | `Parser` (parse a `TokenStream` into a `Program`). |
| `cp3/sema.hpp` | `SemanticAnalyzer` (analyze a `Program`, report to a sink). |
| `cp3/codegen.hpp` | `CodeGenerator` + `GeneratedModule` (AST → `.hpp`/`.cpp` pair). |
| `cp3/runtime.hpp` | Runtime placeholder surface. |
| `cp3/cli.hpp` | `runCli`, the exit-code constants, and the scaffold marker. |

---

## 7. Constructs expressed by composition

Per the note in `cp3/ast.hpp`, several spec constructs have **no dedicated
node** and are modelled by composition. Every consumer must agree on these
shapes:

| Source construct | AST shape |
| --- | --- |
| `parallel.for(...)` / `parallel.reduce(...)` / `parallel.run(...)` | `CallExpr{ callee = MemberExpr{ Identifier("parallel"), "for" }, ... }` |
| `worker(data)` | `CallExpr{ callee = Identifier("worker") }` |
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
   `dispatch()` overloads pick it up automatically — there is no second list to
   update and no way for them to drift.

`<base>` is `Expr`, `Stmt`/`Decl`, `TypeNode`, `ClassMember`,
`SignatureMember`, or `Node`. Because `ast.hpp` is a frozen header, adding a
node requires **lead sign-off**; if the build fails after adding one, the cause
is almost always a switch that was hand-written instead of generated from the
registry — fix the consumer, not the registry.

---

## 9. Module graph & preludes

`ModuleGraph` owns every `CompilationUnit` (one parsed `.cp3` file) and the
directed import edges (`ModuleDependency`). `addUnit()` assigns a stable
`id` (its index) and returns a raw pointer; `unit(id)` looks one up.

`compilationOrder()` is a depth-first **post-order** from the entry unit: each
unit is emitted *after* the units it depends on, each unit appears at most once,
and cycles are tolerated (ES-module semantics,
[`docs/language-spec/18-modules-imports.md`](docs/language-spec/18-modules-imports.md)).
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
([`docs/cli-tooling/02-...`](docs/cli-tooling/02-error-diagnostics-source-mapping.md)).

For tests to assert on a diagnostic, the harness (`tests/runner/run_tests.py`)
reads the combined stdout/stderr and looks for lines in this canonical shape:

```
<path>:<line>:<col>: <severity>: <CODE>: <message>
```

e.g. `src/main.cp3:3:1: error: CP3408: cannot reassign to const 'z'`.

The case format and the `CP3xxxx` wildcard rules are frozen in
[`tests/cases/README.md`](tests/cases/README.md). Run the harness with:

```bash
python tests/runner/run_tests.py --cp3c build/bin/cp3c
```

It exits `0` when no case failed (including when everything is skipped because
the compiler is still a scaffold) and `1` if any case failed.

---

## 11. Known build warnings

* `src/common/src/token.cpp` — `tokenKindName()` returns the string literal
  `"??="`, which GCC reports as a trigraph (`-Wtrigraphs`). Harmless (GCC
  ignores trigraphs by default); it is left as-is because `token.cpp` is a frozen
  implementation. Revisit only if a toolchain is configured with `-trigraphs`.

---

## 12. Milestone 1 scope

**Goal:** a minimal but *real* end-to-end slice — take a `.cp3` file all the way
to a running native executable. Nothing fancier than that earns its keep yet.

In scope:

* **Bindings:** `let` / `const` (and `var`) with primitive initializers
  (`number`, `string`, `boolean`), type inference, `const`-reassignment is an
  error (`CP3408`). Language rule:
  [`docs/language-spec/08-variable-bindings.md`](docs/language-spec/08-variable-bindings.md).
* **Functions:** `function name(params) { ... }`, calls, `return`.
* **`console.log(...)`**: prints to **stdout** (this is what tests compare),
  including template literals (`` `${age}` ``) and multiple arguments.
* **`if` / `else`**.
* **Binary operators:** arithmetic (`+ - * / % **`), comparison
  (`== != === !== < > <= >=`), logical (`&& ||`), and string concatenation.
* **Codegen:** one `.cpp`/`.hpp` pair per `.cp3` file, targeting C++20, then
  invoke the system C++ toolchain, link, and run.

Exit criteria: the `tests/cases/variables/` cases become runnable, and the
harness flips from "42 skipped" to real pass/fail.

Out of scope for M1 (explicitly deferred): classes/interfaces/generics, async
and `await`, the memory model and `weak`, preludes and module resolution beyond
`std/console`, `parallel.*` / `worker`, native interop, and the full diagnostics
translation subsystem. The compiler must not choke on them — it should report a
"not implemented yet" diagnostic rather than crash.

---

## 13. Open questions for the lead

These are genuine ambiguities found while scaffolding. **Nothing here has been
guessed silently** — each is a place where the spec/docs do not decide a
question this code must answer, or where two parts of the frozen surface
disagree. Until a lead answers, the scaffold takes the most conservative
non-committal position.

1. **Compiler internal architecture / IR (largest open item).**
   [`docs/cli-tooling/06-open-tooling-questions.md`](docs/cli-tooling/06-open-tooling-questions.md)
   lists "Compiler implementation architecture itself (transpiler internals, IR
   if any, pass structure)" as genuinely open. Does the frontend lower to an IR,
   or go AST → C++ directly? This decides whether `sema`/`codegen` share a
   lowered form or communicate only through the frozen AST + nodeId side tables.
   *Not blocking the scaffold, but it is the first thing the stage owners need
   decided.*

2. **Canonical machine-readable diagnostic line format.**
   [`docs/cli-tooling/02-...`](docs/cli-tooling/02-error-diagnostics-source-mapping.md)
   shows human-oriented examples (`src/utils.cp3:14:8 — type mismatch: ...`) that
   carry **no error code and no severity token**, yet the frozen test schema
   asserts `{ code, line, col, contains }` and `diagnostic_codes.hpp` defines
   `CP3xxx` codes. The scaffold invents and documents the format
   `<path>:<line>:<col>: <severity>: <CODE>: <message>` — the lead must confirm
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
   `import "cpp/iostream" as io` classify — `Cpp` or `Native`? The lead should
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
   repeatedly promises) implies `"a" + "b"` — and likely `"x" + 1` — must work.
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
    [`docs/cli-tooling/01-...`](docs/cli-tooling/01-compilation-pipeline.md) fixes
    Windows preference to **MSVC → Clang**, but the only toolchain present here
    is **MinGW g++ (GNU 16.1.0)**. Should M1 implement the documented order and
    fail on this machine, or extend the Windows fallback chain to include
    `g++` (so tests can actually run end-to-end in CI)? The harness can only
    verify `run`-mode cases if a usable toolchain is found.

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

* **New AST nodes** — one line in `CP3_AST_NODE_LIST` (§8).
* **New diagnostic codes** — appended inside an existing stage range in
  `diagnostic_codes.hpp` (never renumbered).
