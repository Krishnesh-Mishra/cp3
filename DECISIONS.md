# cp3c — Lead decisions

Binding resolutions to `ARCHITECTURE.md` §13 (open questions), plus scope calls for
Milestone 1. These are **the contract**: agents implement against them and do not
re-litigate them. Anything marked *(revisitable)* may be overridden later, but not
unilaterally by an agent.

Owner: lead. Status: active as of Milestone 1.

---

## D1 — No IR. AST is the only lowering form.
Pipeline: `Lexer → Parser → AST → Sema (side tables keyed by nodeId) → Codegen
(AST + side tables) → C++20`. There is no separate lowered IR in M1 and no new
pass framework. `sema` and `codegen` communicate only through the frozen AST plus
a `SemaResult` side-table object. *(revisitable post-M1.)*

## D2 — Diagnostic machine format (authoritative).
Every diagnostic the CLI emits for machine consumption is exactly:

```
<path>:<line>:<col>: <severity>: <CODE>: <message>
```

`severity` ∈ `error | warning | note`. This is what `tests/runner/run_tests.py`
matches and what `cp3c check`/`run`/`build` print. Human-pretty rendering (colors,
source excerpts) is a later CLI layer that must not change the machine lines.

## D3 — Binary name is `cp3c`.
`--version` prints `cp3c 0.1.0-beta.1`. The specification's `cp3` is the future
end-user frontend name (a thin alias/wrapper), **not** the compiler binary. No
rename in M1.

## D4 — Module specifier classification.
- starts with `./` or `../`, or ends with `.cp3` → `Local`
- starts with `std/` → `Std`
- starts with `cpp/` → `Cpp`
- written as `import native "…"` → `Native`
- any other bare specifier (`"mathkit"`) → `Package`

## D5 — Preludes (file level).
For any unit, inject applicable preludes in the order **Same → Strong → Weak**.
`Same` (`_prelude.same.cp3`): same folder only. `Strong` (`_prelude.cp3`): same
folder plus all descendants, recursive, unstoppable. `Weak`
(`_prelude.weak.cp3`): recursive but stopped by any nested prelude ("wall").
`CompilationUnit::injectedPreludes` records ids in application order.
Cross-folder composition beyond this is deferred.

## D6 — nodeId contract.
`nodeId` is **1-based**; `0` means "unassigned". Every node including the `Program`
root receives an id. The parser assigns ids in **source (pre-order)** order.
Sema/codegen side tables are keyed by these ids.

## D7 — Template literals are lexed flat; no parser→lexer callback.
The **lexer** scans a template literal fully using an internal mode stack and
emits a flat sequence: `TemplateHead`, the interpolation's tokens,
`TemplateMiddle…`, `TemplateTail` — or a single `TemplateString` when there is no
interpolation. The parser consumes that flat sequence. The
`rescanTemplateContinuation()` note in `token.hpp` is **obsolete** and must not be
implemented.

## D8 — `+` is strict (deliberate divergence from JS).
`number + number → number`; `string + string → string` (concatenation). **Every
other** operand combination is a compile error `CP3411` (mixed-type arithmetic).
So `"x" + 1` is an error; use a template literal (`` `${x}` ``) or explicit
conversion. This follows `docs/language-spec` §23, not JS coercion.

## D9 — number → string uses ECMAScript `Number::toString`.
The runtime formatter implements the ECMAScript number-to-string algorithm:
integers print with no decimal point (`18`, and `String(-0) === "0"`),
`Infinity`/`-Infinity`/`NaN` print as those words, otherwise shortest
round-tripping decimal. `stdout` in tests is compared exactly, so this is exact.

## D10 — Entry model.
The entry unit's **top-level statements are the program**; the compiler
synthesizes `int main()` whose body runs them. An explicit `function main()` is
just an ordinary function in M1 (top-level statements still run). *(revisitable.)*

## D11 — `console.log` backing.
Implemented in the cp3 runtime with standard C++ (`<iostream>`/`<sstream>`) and
the D9 formatter. **No `fmt` dependency in M1.** `fmt` may replace it later behind
the same runtime surface.

## D12 — Toolchain detection order.
Use the documented per-platform order first (Windows: MSVC `cl` → Clang
`clang++`; POSIX: `clang++` → `g++`), then a **last-resort fallback** to `g++`
then `clang++` if none of the preferred are found. This makes end-to-end `run`
work in the current environment (MinGW g++). The chosen toolchain is reported
under `--verbose`. *(revisitable.)*

## D13 — Object-property shorthand.
`{ a }` sets `ObjectProperty::value` to an `Identifier("a")`, **not** `nullptr`.
`nullptr` is reserved for malformed/recovery nodes only.

## D14 — Module boundaries may be extended by the contracts pass only.
Where a stub boundary is insufficient (most importantly: `SemanticAnalyzer` must
expose its side tables to codegen), the **contracts pass** extends the public
header and freezes it. After that, stage owners implement against the frozen
shape and do not reshape it. This supersedes the earlier "implement the body
only" wording where the shape was provably inadequate.

---

## Resolved test-corpus ambiguities (lead review)

The four spec-test agents placed genuinely undecided cases under
`tests/cases/<category>/_ambiguous/`. These are the resolutions. They are
binding for the corresponding feature when it lands; cases in `_ambiguous/` that
a resolution settles should be moved into their parent category with a concrete
expectation (a follow-up task, not urgent).

**Types & operators**
- **D15.** Loose `==`/`!=` match only same-type values; the sole exception is
  `null == undefined → true` and `null === undefined → false`. No `"1" == 1`.
- **D16.** `typeof <expr>` is allowed on any expression and yields a `string`
  (`"number" | "string" | "boolean" | "undefined" | "object" | "function"`).
- **D17.** No implicit string→number coercion. Unary `+` on a string is an error
  (consistent with D8); use explicit conversion.
- **D18.** Bitwise operators have 32-bit semantics (JS parity): operands coerce to
  int32, results are int32, `>>>` is an unsigned 32-bit shift.

**Enums**
- **D19.** A number-backed enum member prints its underlying number; a
  string-backed member prints its string (TS/JS parity). No reverse mapping.
- **D20.** Mixing number- and string-backed members is `CP3706`. A member with no
  initializer after a string-backed member is an error. Assigning a raw number to
  an enum-typed binding, or comparing an enum member to a raw number, is an error
  (enum values must come from the enum). *(revisitable — stricter than TS.)*

**Classes & interfaces**
- **D21.** Accessors are TS-style: `get name() { … }` / `set name(v) { … }`, with
  no `function` keyword.
- **D22.** `super(...)` and `super.method()` are supported. This needs a new AST
  node `SuperExpr` — **lead sign-off granted** under ARCHITECTURE §8. Until
  classes land, the parser may compose `Identifier("super")` but must report it.
- **D23.** `extends A, B` (multiple inheritance) is a **parse** error (`CP3205`);
  sema additionally guards `CP3703`.
- **D24.** Static members are reachable through an instance (`c.staticX`) as well
  as the class (`C.staticX`) — JS/TS parity.
- **D26.** Generic interfaces (`interface Box<T>`) are supported.

**Generics**
- **D25.** Generic **classes** require explicit type arguments at `new`
  (`new Box<number>()`); generic **functions** infer type arguments from their
  arguments where possible.

**Modules**
- **D27.** Local specifiers may be written `./x.cp3` or bare `x.cp3`; both resolve
  locally. A bare non-`.cp3` specifier is a package (per D4).
- **D28.** `export default function name() {}` and `export default class …` are
  valid, as is `export default <expression>`.
- **D29.** Prelude injection order is Same → Strong → Weak; on a name collision the
  **later** injection shadows the earlier (weak > strong > same). *(revisitable.)*

**Functions & closures**
- **D30.** `for (let i …)` creates a **fresh binding per iteration** (JS parity):
  closures created inside capture that iteration's value (`0, 1, 2`, not `3, 3, 3`).
- **D31.** Function declarations are hoisted and may be called before their
  textual position (JS parity).
- **D32.** Falling off the end of a non-`void`/non-`undefined` function is an error
  (`CP3504`). `return <value>;` in a `void` function is `CP3402`.
- **D33.** `break`/`continue` outside a loop/switch are `CP3502`/`CP3503`.

**Deferred to when the feature lands (no M1 impact):** array/object `console.log`
rendering (`[1, 2, 3]` / `{ a: 1 }`, JS-ish — to be pinned with the array/Map
work), `find`/`indexOf` not-found results, `reduce` without an initial accumulator,
`slice`/`substring`/`binarySearch` edge semantics, `Map` iteration order,
`Iterator<T>` definition, and `handleSuper`/`super` codegen.

---

## Resolved ambiguities — async / concurrency / errors / stdlib (lead review)

**Concurrency & parallel**
- **D34.** `parallel` is a **core builtin** (no import required), used bare as in §13.
- **D35.** Ranges `a..b` are **end-exclusive** (`0..arr.length` covers every index).
- **D36.** `worker(x)` invokes a module's default export compiled as a worker;
  `import worker from "./x.cp3"` is the canonical form. A worker crash may take the
  process down with a nonzero exit code (exact value deferred).

**Errors**
- **D37.** `catch { }` (no binding) is allowed, and `try { } finally { }` (no catch)
  is allowed.
- **D38.** `return` inside `finally` overrides the try/catch return (JS semantics).
- **D39.** `throw` accepts any value; there is **no built-in `Error` hierarchy** in
  M1 — any thrown value binds to the catch parameter. *(revisitable.)*

**Async**
- **D40.** `async () => {}` arrow functions are supported, and `Promise.resolve` /
  `Promise.reject` exist.
- **D41.** `await` on a non-Promise value yields that value (JS unwrap).
- **D42.** `.then(...)` returns a new chained Promise (JS semantics).
- **D43.** An unhandled rejection terminates the process with a nonzero exit code
  (exact code deferred).

**Stdlib**
- **D44.** `Map` / `Set` preserve **insertion order** (JS parity) — implementation
  must be insertion-ordered, not `std::unordered_*`.
- **D45.** `Math.round` uses JS semantics (half rounds toward +∞:
  `Math.round(-2.5) === -2`).
- **D46.** `parseInt` honours a `0x` prefix (base-16 detection, JS parity);
  `String(x)` is a core builtin (no import).
- **D47.** `Buffer` accepts only `utf8` (default), `base64`, `hex`; any other
  encoding is a **compile-time** error.
- **D48.** Regex literals support `.test` / `.match` / `.exec` (core, no import);
  `array.slice` exists (array/string parity).
- **D49.** `console.log` renders arrays as `[1, 2, 3]` and plain objects as
  `{ a: 1 }` — `, ` between elements, `: ` after keys, property/insertion order.

**Deferred (genuinely environment- or artifact-dependent; no M1 impact):**
`fetch` behavior needing a live server, `fs.readdirSync` ordering, `Date`
timezone handling, `JSON.stringify` indentation details, `env.get` argument
spelling, network `headers` typing, `binarySearch` return value, `find`/`indexOf`
not-found results, uninitialized-field defaults, unbroken-cycle warning text,
out-of-bounds index semantics, and the CLI/tooling ambiguity markers (which
describe build artifacts, not `.cp3` programs, and are **not testable cases**).

---

## Harness policy for `_ambiguous/` cases

Cases under any `_ambiguous/` directory are **open-question markers, not
assertions**. The harness must:
- discover them,
- report them in a separate `ambiguous` bucket with the question text,
- **never** count them as pass or fail, and never let them affect the exit code,
- expose `--strict-ambiguous` to opt into running them as ordinary cases (for when
  a resolution lands and they are promoted into the parent category).

This keeps the conformance signal clean: the pass/fail count reflects only cases
whose expected behavior the spec actually pins.

---

## Post-contracts clarifications (from the contracts pass)

- **D50.** `Nullable(T)` is the shorthand for `T | null | undefined`
  (`unify(T, nullish) → Nullable(T)`). Where `T | null` must be distinct from
  `T | undefined`, write the explicit union.
- **D51.** Literal types: for M1, sema **widens** a literal expression to its base
  primitive in `nodeTypes` (`42` → `number`, `"x"` → `string`). `Literal` types
  remain in the model for signature-level literals (`on(event: "data", …)`).
  Codegen must accept either.
- **Process — commits.** Agents no longer run `git`. The lead commits with **scoped**
  paths. Reason: two agents racing `git add -A` merged in-flight files into
  unrelated commits (`d64f9ea` swept the contracts work). Build dirs stay
  per-agent (`build-<module>/`); nothing else changes.

- **D52.** Regex-literal token payload convention: the pattern goes in
  `Token::stringValue` and the flag letters go in `Token::rawText`. The parser and
  codegen read those fields (no dedicated regex fields exist on `Token`).
- **D53.** `TokenKind::UnsignedShiftRightAssign` (`>>>=`) is **added** to the
  token set, placed in the operators group next to `ShiftRightAssign` for
  readability. `TokenKind` integral values are explicitly **not** part of the ABI
  (nothing serializes, indexes, or orders by them), so shifting the values of
  later kinds is behaviour-preserving. The lexer emits it by maximal munch; the
  parser maps it to `AssignOp::UnsignedShiftRight` (glue lands in the integration
  pass — the parser owner is editing that file now). (Gap found by the lexer
  owner: JS `>>>=` was not representable.)
- **Doc cleanup.** The obsolete `rescanTemplateContinuation()` note in `token.hpp`
  is replaced by a pointer to D7 (templates are lexed flat).

- **D54.** Front-end diagnostic anchoring is **line-granular**. The frozen case
  format (`tests/cases/README.md`) documents `col` with the example `:3:1:`, and
  **97** `expect.json` error cases assert `"col": 1` while **none** asserts any
  other column. `sema` and `parser` already report at column 1; the **lexer must
  match** (it currently reports the literal's real column — e.g. `CP3105` at
  `2:13`, `CP3102`/`CP3103` at `2:9` — which fails `syntax/invalid_hex`,
  `syntax/unterminated_string`, `syntax/unterminated_template`). Rule: every
  diagnostic is emitted at **column 1** of its (unchanged) line. Separately, a
  **syntax** (lexical/parse) error's message always contains the literal word
  `"syntax"` — the 5 `syntax/*` cases assert `contains: "syntax"`, and `parser`
  already prefixes `"syntax error: "`; the lexer adopts the same prefix. Specific
  `CP31xx`/`CP32xx` codes and detail text are preserved. *(Revisitable post-M1:
  once precise columns/spans are trustworthy suite-wide, the corpus `col` fields
  can be tightened and this rule relaxed.)*

- **D55.** Build trees live **only** under the repository's single `build/`
  directory. Canonical: `cmake -S . -B build` → `build/bin/cp3c` (the harness's
  default resolution, unchanged). Parallel authors isolate with
  `cmake -S . -B build/scratch/<name>`, which the `build/` ignore rule covers.
  Root-level `build-*` directories are retired — the `build-*/` ignore pattern
  is removed so a stray one is visible in `git status`. `build-modules/` and
  `build-semacomp/` are grandfathered only while their agents are in flight;
  the lead deletes them at wave end and never touches them before. Supersedes
  the "(build dirs stay per-agent `build-<module>/`)" note in the D-process
  entry above, which it amends. This is a *location* rule only: module source
  lists stay per-module (STYLE.md §3).
- **D56.** **Single-binary guarantee.** The shipped artifact is exactly one
  `cp3c` executable, and it must be relocatable: copying `cp3c`/`cp3c.exe` to
  any directory and running `cp3c run hello.cp3` must work with no repository
  and no sibling files; compiling a `.cp3` file yields exactly one executable.
  Today this is violated at one point: `runtimeSourcePaths()` /
  `compilerIncludeRoots()` (`src/cli/src/util.cpp`) resolve the runtime through
  the compile-time `__FILE__` path into `src/runtime/**` — correct for
  development, not shippable. Planned fix (M2 wave, one agent owns it): a
  build-time generator embeds the runtime sources and the runtime include tree
  (whatever generated code `#include`s) as string tables inside `cp3c`; when
  compiling a user program, the driver materializes them into that program's
  `.cp3/build/` cache and points the system compiler there (embedded copy
  preferred; the repo path stays as a development fallback). Acceptance test:
  copy `cp3c.exe` alone into an empty temp directory, `cp3c run` a prelude-using
  hello `.cp3` there, expect correct stdout and no read under the checkout.

---

## Milestone 1 definition of done
A `.cp3` file containing `import console from "std/console"`, `let`/`const`
primitive bindings, functions, `if`/`else`, calls, `console.log` with template
literals, and `+ - * / % ** == != === !== < > <= >= && ||` compiles to a C++20
program that the system toolchain builds and runs, printing the exact expected
`stdout`. The `tests/cases/variables/` corpus (42 cases) flips from "skipped" to
real pass/fail. Anything in the AST beyond that must produce a clean
"not implemented" diagnostic (`CP3002`), never a crash.
