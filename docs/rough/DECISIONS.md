# cp3c - Lead decisions

Binding resolutions to `ARCHITECTURE.md` §13 (open questions), plus scope calls for
Milestone 1. These are **the contract**: agents implement against them and do not
re-litigate them. Anything marked *(revisitable)* may be overridden later, but not
unilaterally by an agent.

Owner: lead. Status: active as of Milestone 1.

---

## D1 - No IR. AST is the only lowering form.
Pipeline: `Lexer → Parser → AST → Sema (side tables keyed by nodeId) → Codegen
(AST + side tables) → C++20`. There is no separate lowered IR in M1 and no new
pass framework. `sema` and `codegen` communicate only through the frozen AST plus
a `SemaResult` side-table object. *(revisitable post-M1.)*

## D2 - Diagnostic machine format (authoritative).
Every diagnostic the CLI emits for machine consumption is exactly:

```
<path>:<line>:<col>: <severity>: <CODE>: <message>
```

`severity` ∈ `error | warning | note`. This is what `tests/runner/run_tests.py`
matches and what `cp3c check`/`run`/`build` print. Human-pretty rendering (colors,
source excerpts) is a later CLI layer that must not change the machine lines.

## D3 - Binary name is `cp3c`.
`--version` prints `cp3c 0.1.0-beta.1`. The specification's `cp3` is the future
end-user frontend name (a thin alias/wrapper), **not** the compiler binary. No
rename in M1.

## D4 - Module specifier classification.
- starts with `./` or `../`, or ends with `.cp3` → `Local`
- starts with `std/` → `Std`
- starts with `cpp/` → `Cpp`
- written as `import native "…"` → `Native`
- any other bare specifier (`"mathkit"`) → `Package`

## D5 - Preludes (file level).
For any unit, inject applicable preludes in the order **Same → Strong → Weak**.
`Same` (`_prelude.same.cp3`): same folder only. `Strong` (`_prelude.cp3`): same
folder plus all descendants, recursive, unstoppable. `Weak`
(`_prelude.weak.cp3`): recursive but stopped by any nested prelude ("wall").
`CompilationUnit::injectedPreludes` records ids in application order.
Cross-folder composition beyond this is deferred.

## D6 - nodeId contract.
`nodeId` is **1-based**; `0` means "unassigned". Every node including the `Program`
root receives an id. The parser assigns ids in **source (pre-order)** order.
Sema/codegen side tables are keyed by these ids.

## D7 - Template literals are lexed flat; no parser→lexer callback.
The **lexer** scans a template literal fully using an internal mode stack and
emits a flat sequence: `TemplateHead`, the interpolation's tokens,
`TemplateMiddle…`, `TemplateTail` - or a single `TemplateString` when there is no
interpolation. The parser consumes that flat sequence. The
`rescanTemplateContinuation()` note in `token.hpp` is **obsolete** and must not be
implemented.

## D8 - `+` is strict (deliberate divergence from JS).
`number + number → number`; `string + string → string` (concatenation). **Every
other** operand combination is a compile error `CP3411` (mixed-type arithmetic).
So `"x" + 1` is an error; use a template literal (`` `${x}` ``) or explicit
conversion. This follows `docs/language-spec` §23, not JS coercion.

## D9 - number → string uses ECMAScript `Number::toString`.
The runtime formatter implements the ECMAScript number-to-string algorithm:
integers print with no decimal point (`18`, and `String(-0) === "0"`),
`Infinity`/`-Infinity`/`NaN` print as those words, otherwise shortest
round-tripping decimal. `stdout` in tests is compared exactly, so this is exact.

## D10 - Entry model.
The entry unit's **top-level statements are the program**; the compiler
synthesizes `int main()` whose body runs them. An explicit `function main()` is
just an ordinary function in M1 (top-level statements still run). *(revisitable.)*

## D11 - `console.log` backing.
Implemented in the cp3 runtime with standard C++ (`<iostream>`/`<sstream>`) and
the D9 formatter. **No `fmt` dependency in M1.** `fmt` may replace it later behind
the same runtime surface.

## D12 - Toolchain detection order.
Use the documented per-platform order first (Windows: MSVC `cl` → Clang
`clang++`; POSIX: `clang++` → `g++`), then a **last-resort fallback** to `g++`
then `clang++` if none of the preferred are found. This makes end-to-end `run`
work in the current environment (MinGW g++). The chosen toolchain is reported
under `--verbose`. *(revisitable.)*

## D13 - Object-property shorthand.
`{ a }` sets `ObjectProperty::value` to an `Identifier("a")`, **not** `nullptr`.
`nullptr` is reserved for malformed/recovery nodes only.

## D14 - Module boundaries may be extended by the contracts pass only.
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
  (enum values must come from the enum). *(revisitable - stricter than TS.)*

**Classes & interfaces**
- **D21.** Accessors are TS-style: `get name() { … }` / `set name(v) { … }`, with
  no `function` keyword.
- **D22.** `super(...)` and `super.method()` are supported. This needs a new AST
  node `SuperExpr` - **lead sign-off granted** under ARCHITECTURE §8. Until
  classes land, the parser may compose `Identifier("super")` but must report it.
- **D23.** `extends A, B` (multiple inheritance) is a **parse** error (`CP3205`);
  sema additionally guards `CP3703`.
- **D24.** Static members are reachable through an instance (`c.staticX`) as well
  as the class (`C.staticX`) - JS/TS parity.
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
rendering (`[1, 2, 3]` / `{ a: 1 }`, JS-ish - to be pinned with the array/Map
work), `find`/`indexOf` not-found results, `reduce` without an initial accumulator,
`slice`/`substring`/`binarySearch` edge semantics, `Map` iteration order,
`Iterator<T>` definition, and `handleSuper`/`super` codegen.

---

## Resolved ambiguities - async / concurrency / errors / stdlib (lead review)

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
  M1 - any thrown value binds to the catch parameter. *(revisitable.)*

**Async**
- **D40.** `async () => {}` arrow functions are supported, and `Promise.resolve` /
  `Promise.reject` exist.
- **D41.** `await` on a non-Promise value yields that value (JS unwrap).
- **D42.** `.then(...)` returns a new chained Promise (JS semantics).
- **D43.** An unhandled rejection terminates the process with a nonzero exit code
  (exact code deferred).

**Stdlib**
- **D44.** `Map` / `Set` preserve **insertion order** (JS parity) - implementation
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
  `{ a: 1 }` - `, ` between elements, `: ` after keys, property/insertion order.

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
- **Process - commits.** Agents no longer run `git`. The lead commits with **scoped**
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
  pass - the parser owner is editing that file now). (Gap found by the lexer
  owner: JS `>>>=` was not representable.)
- **Doc cleanup.** The obsolete `rescanTemplateContinuation()` note in `token.hpp`
  is replaced by a pointer to D7 (templates are lexed flat).

- **D54.** Front-end diagnostic anchoring is **line-granular**. The frozen case
  format (`tests/cases/README.md`) documents `col` with the example `:3:1:`, and
  **97** `expect.json` error cases assert `"col": 1` while **none** asserts any
  other column. `sema` and `parser` already report at column 1; the **lexer must
  match** (it currently reports the literal's real column - e.g. `CP3105` at
  `2:13`, `CP3102`/`CP3103` at `2:9` - which fails `syntax/invalid_hex`,
  `syntax/unterminated_string`, `syntax/unterminated_template`). Rule: every
  diagnostic is emitted at **column 1** of its (unchanged) line. Separately, a
  **syntax** (lexical/parse) error's message always contains the literal word
  `"syntax"` - the 5 `syntax/*` cases assert `contains: "syntax"`, and `parser`
  already prefixes `"syntax error: "`; the lexer adopts the same prefix. Specific
  `CP31xx`/`CP32xx` codes and detail text are preserved. *(Revisitable post-M1:
  once precise columns/spans are trustworthy suite-wide, the corpus `col` fields
  can be tightened and this rule relaxed.)*

- **D55.** Build trees live **only** under the repository's single `build/`
  directory. Canonical: `cmake -S . -B build` → `build/bin/cp3c` (the harness's
  default resolution, unchanged). Parallel authors isolate with
  `cmake -S . -B build/scratch/<name>`, which the `build/` ignore rule covers.
  Root-level `build-*` directories are retired - the `build-*/` ignore pattern
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
  the compile-time `__FILE__` path into `src/runtime/**` - correct for
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

---

## Milestone 2 (opened 2026-10-17)

- **D57.** M2 opens with a full reconnaissance of the 547 skips: every one is
  attributed to its first blocking feature (exact counts in `M2-PLAN.md`,
  which is the living milestone roadmap - lead-owned, updated as waves land;
  `DECISIONS.md` wins on any conflict). The adopted default wave order is:
  (1) method-call codegen + per-receiver runtime, (2) classes/object model,
  (3) arrays/indexing, (4) async/event loop/timers (in flight), (5) Map/Set/
  EventEmitter, (6) enums, (7) try/throw/finally, (8) non-primitive
  locals/returns, (9) object literals/interfaces, (10) generic functions,
  (11) module local function linkage, (12) grammar tail sweep. Rank is a
  default, not a lock: the lead re-ranks as waves land. Each wave's brief
  must carry the lead's rulings for any marker its feature touches
  (the 18 genuinely open markers are listed in `M2-PLAN.md`).
- **D58.** Ambiguity markers are **inert documentation, never run as cases**:
  each marker's `expect.json` carries its question text in `error.contains`,
  so `--strict-ambiguous` (which runs them as ordinary cases) serves only as a
  crash-safety probe - its pass/fail numbers are not a signal, and it is never
  a promotion gate. **Promotion** = the lead rules (new D-entry), then an
  agent rewrites the marker into a real case in the parent category with a
  real `expect.json` and removes it from `/_ambiguous/` and
  `AMBIGUITIES.md`; fixtures with broken or missing scenario files are
  repaired at that time (three are known and listed in `M2-PLAN.md` §bugs).
- **D59.** `examples/` is **showcase-only for humans**: readable programs a
  person can open and run manually (`cp3c run main.cp3` from the example's
  folder). Examples are **never run automatically** - `expect.json` files,
  active/pending statuses, the promotion workflow, `tools/run_examples.py`,
  and the `example-checks` CMake target are all removed. Automated
  verification of program behavior lives **only** in `tests/cases/` (the
  corpus oracle). The earlier "living end-to-end checks" framing is
  superseded; examples no longer gate waves. What is not runnable yet (games,
  servers, `parallel.*`, async) is labeled in prose in `examples/README.md`
  until its wave lands.
- **D60.** `examples/benchmarks/` is the **cross-language comparison
  suite** - cp3 vs C++ vs Rust vs Go vs JS/Node vs Python, the same task
  implemented per language: health-server boot time + `/healthz` latency +
  req/s stress, single-thread compute, multi-thread compute. Every number
  shown is a real measurement from an actual run (labeled machine/date);
  invented numbers are forbidden. This is seed material for a future README
  "advertisement" section. Layout: `examples/benchmarks/<task>/` with a
  `README.md` + one folder per language, plus a suite runner and a
  dependency-free bench/load script; cp3 entries that need unlanded features
  are written to spec and marked in prose.
- **D61.** CLI artifact layout: `.cp3/build/` holds **intermediates only** -
  generated `.cpp`/`.hpp`, object files, the manifest, and (per D56) the
  materialized runtime sources. The final executable lands in
  **`.cp3/out/<entry-stem>` (`.exe` on Windows)**; `cp3 run` executes it from
  there and `cp3 clean` removes `.cp3/build/` and `.cp3/out/` (`--all` still
  also wipes `.cp3/packages/`). Supersedes `cache.cpp`'s current
  exe-in-`build/` behavior; fix queued behind the async wave because
  `src/cli` is single-writer until then.
- **D62.** Wave-4 JS-parity rulings (spec §14/§20 pin libuv/Node as the model
  but are silent on specifics; evidence: `build/scratch/async/WAVE4-REPORT.md`
  §9 - these bind future async/timer work):
  1. **Microtask/macrotask order**: microtasks drain fully in FIFO order before
     the next macrotask, and again after each fired timer. **`await` always
     defers its continuation to the microtask queue - even for an
     already-settled promise** (JS parity). The runtime currently resumes
     settled awaits inline; that deviation is a wave-1 fix (pinning case:
     `async/await_settled_microtask_fifo`). Promotes
     `async/_ambiguous/microtask_macrotask_order` → real case (passes).
  2. **`setInterval`** is fixed-deadline: each firing is due `periodMs` after
     the previous *deadline* (never after the previous firing); missed slots
     catch up back-to-back inside the same timer phase; overdue re-arms sort by
     true deadline ahead of later timers; period clamps to ≥1 ms (libuv/Node).
  3. **Timer ordering within a phase**: `(deadline, insertion id)` - a
     zero-delay `setTimeout` is due on the next turn.
  4. **Negative delays**: `sleep`/`setTimeout` clamp to 0 (due now);
     `setInterval` negative/zero periods clamp to 1 ms.
  5. **Caught rejection observation**: awaiting a rejected promise and catching
     the rejection marks it observed - a caught `await` never triggers the D43
     unhandled-rejection exit (JS-observable; mechanism may change).
  6. **`async` arrows** (D40) remain `CP3002` until the arrows-as-values
     cluster (wave 12) lands; only `async function` declarations are
     implemented today.
  7. **`Promise.all/race/allSettled`** take array-literal operands only (array
     forms land with wave 3). `Promise.resolve/reject` (D40) are wired in
     wave 1.
- **D63.** String/member rulings (wave-1 scope):
  1. **String `.length` is UTF-8 bytes** (`std::string::size`, O(1)) - the same
     byte model as `str[i]` (spec §09) and the Rust/Go convention the spec
     cites; `charAt(i)` remains the character-aware accessor. Resolves
     `arrays-strings/_ambiguous/str_length_utf8` (marker retired; coverage:
     `arrays-strings/str_length_accent_bytes`, which flips with wave 1).
  2. **`NaN` and `Infinity` resolve as `number` globals** - D9 pins NaN's
     rendering, but `console.log(NaN)` currently fails with CP3301 (wave-1
     fix).
- **D64.** Performance doctrine and the `number` fast path (evidence:
  `build/scratch/perf-probe/` - the exact compute-single workload lowered three
  ways at g++ 16.1.0 `-O2`: `std::fmod` **1080 ms** = the shape cp3c emits today
  and the sink behind compute-single's 1.16 s; inline cast-based `%` in the
  exact double domain **385 ms**; int64 **135 ms** ≈ the C++/Rust entries'
  132-147 ms):
  1. **Targets.** The `benchmarks/` suite is the judge (D60 - numbers
     refresh from real runs, never invented). Standing goal (user directive,
     2026-10-17): on every completion-time table **come within ~5% of C++/Rust**
     (parity is the aim; ≤5% behind is acceptable, more is not) and **beat
     Go/JS/CPython**. For compute-single that means **≤ C++/Rust + 5 %
     (≈0.15-0.16 s)**; ≤ Go (0.20 s) is only the floor. Matching the native
     entries on hot integer loops is the language's core promise, so it is a
     tracked requirement, not a nicety.
  2. **No libm calls in generated scalar hot paths.** `%` must lower to an
     inline exact-integer fast path with a `std::fmod` fallback (and
     literal-divisor specialization), never to a bare `std::fmod` call. Same
     rule for every libm-backed operator as it appears (`**`, etc.).
  3. **Integer fast path for `number`.** Observable semantics stay exactly IEEE
     double / JS; where static analysis proves a value integral and in range,
     codegen may carry it as int64, with a guard/fallback the moment it cannot
     prove. This is the static-language dividend JS engines buy back with
     runtime speculation; the correctness bar is the corpus staying
     byte-identical.
  4. **Generated-code hygiene** (compile-time, not runtime - hello-world is
     already DCE-clean: 6-8 ms startup = blank-process floor): emit string
     literals without `std::string(...)` wrapping, prune the per-file prelude to
     used helpers, and (with D56) link only the runtime TUs a program needs -
     today every build compiles/links all five.
- **D65.** Native type systems (spec §3: per-project `typeSystem` in
  `package.json`, inline `r_'u32'`/`c_'long long'`/`ts_'number'` prefixes,
  C++-style implicit widening with opt-in `strictNumericCasts`) are the
  **explicit** fast path - declared native types are C++/Rust-equal by
  construction. D64's fast path and this feature are complementary: the
  TS-default must stay fast without changing type names, and native types must
  exist for guaranteed native codegen. Competitions policy: the cp3 row stays
  TS-typed (the flagship "same readable code" contract); once this feature
  lands, a second, clearly-labeled cp3 entry (e.g. compute-single with
  `r_'u64'`) joins it - written-to-spec and pending until then, per the
  suite's normal contract.
- **D66.** Repo layout, performance gate and house style (user directives,
  2026-10-17):
  1. The cross-language benchmark suite lives at **`benchmarks/` in the repo
     root** (moved out of `examples/`; the one deliberate exception to the
     root-hygiene rule), single entry point `benchmarks/run_benchmark.py`, and
     wired into the harness: `tests/runner/run_tests.py --bench` runs the
     corpus and then the suite.
  2. **Performance gate:** every runnable row prints cp3/cpp and cp3/rust
     ratios plus a geomean footer; any row where cp3 is **more than 20% slower
     than cpp or rust** prints a WARN so the hot spot gets fixed (the standing
     target stays within ~5%, D64; `--strict` turns warnings into a nonzero
     exit). Peak RSS (MB) and stripped binary size (KB) join the columns where
     measurable, honestly marked `n/a` where not.
  3. **Match oracle extended:** every benchmark entry prints exactly one
     integer/short line whose value is stated in the task README.
  4. **House style:** no em dashes (U+2014) or en dashes (U+2013) anywhere in
     repo text: docs, code comments, agent instructions. Write `-` or
     rephrase.
  5. **Source-tree structure sweep:** a behavior-neutral restructure of
     `src/codegen` first, then `src/lexer`/`src/parser` and other modules as
     needed (folder structure, private-header layout, nomenclature per
     STYLE.md), queued immediately after the perf wave; the corpus stays
     byte-identical.

## D67 - Perf-gate tiers + benchmarks on every test run (user directive, 2026-10-18)

Amends D66.2 and extends D64:

1. Every runnable benchmark row is graded against all five references:
   **BIG WARN** when cp3 is slower than any of Go, JS, or Python at all (time
   ratio > 1.00), or more than 20% slower than C++ or Rust (ratio > 1.20);
   plain **WARN** when more than 10% slower than C++ or Rust (ratio > 1.10).
   BIG WARN lines read as fix-me items; `--strict` fails the run on any tier,
   and the summary footer counts both. The D64 "within ~5%" target stays the
   aspiration, not the trigger.
2. `tests/runner/run_tests.py` runs the benchmark suite automatically after
   the corpus pass (default on; `--no-bench` opts out; `--json` still skips
   with a note) and re-prints the gate warning block so it cannot scroll away.
3. Rationale: perf is the product; the warning list is the todo list. Rows
   behind Go/JS/Python are never acceptable; 10% and 20% versus the native
   pack are the staged fix thresholds.
4. Every perf wave closes with the scorecard step: canonical rebuild, full
   labeled suite run, and a verbatim dated paste into `benchmarks/README.md`
   plus the root README tables (never hand-edit). The table diff is the
   wave's report card, kept per wave.
