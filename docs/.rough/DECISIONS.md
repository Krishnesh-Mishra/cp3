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
`--version` prints `cp3c 0.2.4`. The specification's `cp3` is the future
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

## D12 - Toolchain detection order. *(superseded 2026-10-25)*
Use the documented per-platform order first (Windows: MSVC `cl` → Clang
`clang++`; POSIX: `clang++` → `g++`), then a **last-resort fallback** to `g++`
then `clang++` if none of the preferred are found. This makes end-to-end `run`
work in the current environment (MinGW g++). The chosen toolchain is reported
under `--verbose`. *(revisitable.)*

**Superseded by "Beta one target toolchain is Clang-only" below.** Beta one has
one target compiler family; there is no GCC or MSVC fallback.

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
  Superseded by D80 (2026-10-19): workers are explicit `new Worker(...)` handles
  with message events; the import-bound worker call form is retired.

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
- **D48.** Regex literals support `.test` / `.match` / `.exec` (core, no import)
  plus `String.prototype.split(RegExp)` through the same engine (the day-2 docs
  example and the `stdlib/string_split_regex` corpus case); `array.slice` exists
  (array/string parity).
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
"not built yet" diagnostic (the retired placeholder), never a crash.

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
  active/pending statuses, the promotion workflow, `vendor/tools/run_examples.py`,
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
  **Addendum (2026-10-19, regex landing).** Change detection is now
  coherence-complete in both directions. (a) Per-entry header stamps: each
  manifest entry records the include-tree fingerprint it was built under
  (`headerFingerprintOf`: every file under the include roots, hashed in sorted
  order) besides profile + content hash, so an include-tree change retires
  every object on its next build - no translation unit is tracked back to the
  headers it includes, so the fingerprint over-approximates deliberately.
  (b) Executable **link stamps**: `.cp3/build/<exe>.link-stamp` records the
  identity of the object set (paths + content hashes), the profile, the header
  fingerprint and the native link flags; the link step reruns whenever the
  stamp differs, so a runtime object rebuilt in an earlier program's pass
  relinks every executable that contains it instead of leaving a stale binary.
  Manifest format is `cp3c-manifest-v2` (pre-v2 manifests recompile once).
  Motivated by the stale-artifact hazards recorded in
  WAVE-M2-OPERATORS-REPORT section 4 and WAVE-STD-R3-NETWORK-REPORT ("Cold
  artifact crash root cause"), both of which needed manual `.cp3` deletes; the
  regex landing hit the relink direction during `Regex::split` verification
  (a green runtime object next to an executable still linked against the
  previous one). Worked example: a comment-only edit to
  `cp3/core/regex.hpp` recompiles all 22 objects and relinks all 13
  executables of that family, with no manual deletes.
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
  6. **`async` arrows** (D40) remained on the retired placeholder diagnostic
     until the arrows-as-values cluster (wave 12) landed; at the time only
     `async function` declarations were implemented.
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
     root-hygiene rule), single entry point `benchmarks/runner/run_benchmark.py`, and
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

### D67 addendum - multi-core rows must beat cpp; the two server rows (user directive, 2026-10-19)

The 5% band is the single-core rule, and the same directive tightens its
trigger: plain WARN now fires above 1.05x cpp/rust (was 1.10x), so anything
past the D64 band is a fix-me line; BIG WARN stays at 1.20x and at any loss to
go/js/python. Rows whose cp3 entry is deliberately parallel carry a stricter
rule still: they must be **faster** than the cpp comparator, because cp3 ships
TBB and the parallel runtime is the thing being measured. The suite tags them
(`MULTICORE_TASKS`: `compute-multi` today, plus the new `health-server-multi`):
a time ratio above 1.00x cpp prints `WARN multi-core`, above 1.20x prints BIG
WARN, and `--strict` fails on either.

The server surface gains two rows so the comparison is honest in both shapes:
the existing single-core `health-server` and the multi-loop
`health-server-multi` (D79 `createServer({ threads })`), each against
go/express/cpp/rust in their idiomatic modes. Single-core rows keep the ~5%
target versus cpp/rust; noise explains small tilts either way. Rows slower
than go/js/python remain BIG WARN without exception.

## D68 - Standard-module architecture: native packages behind mechanical tables (user architecture note, 2026-10-18)

Clarifies the answer to "are the std modules codegen things?":

1. A std module's behavior lives in the runtime library as ordinary C++ units
   (headers declare, `.cpp` defines), one folder per module family; codegen
   holds no module semantics beyond a one-line lowering row per member.
2. The compiler carries exactly three mechanical tables per module: the
   registry (specifier + exported names, `src/modules`), the typed signature
   view (`src/sema`), and the lowering rows plus include flags
   (`src/codegen`). Each table grows as its own per-module file; nothing is
   appended to a shared dump.
3. Where a module needs compiler knowledge (for example `fetch` returning
   `Promise` under the event loop), it rides the existing core machinery
   (promise/event-loop lowering), not bespoke codegen.
4. Revisit trigger: once classes and object literals land (waves 2/9),
   pure-logic modules may move to cp3-source implementations shipped with the
   compiler; perf-critical families (crypto, network, JSON, strings) stay
   native under the no-runtime-overhead rule and the D67 gate.

## D69 - JS-compat typed arrays accepted (JS0 proposal, 2026-10-18)

Accepts `docs/rough/JSC-TYPED-ARRAYS.md` (JS0) as the surface to specify and
implement, with the five open questions resolved as recommended there:

1. `BigInt64Array`/`BigUint64Array` land now over i64/u64 storage with the
   documented 2^53 number-domain caveat (JS would return `BigInt`; the surface
   upgrades in place if a BigInt primitive lands later).
2. `SharedArrayBuffer` lands now as a distinct class with
   ArrayBuffer-identical semantics; true sharing and any synchronization
   surface defer to the worker/parallel ruling.
3. `DataView` defaults to **big-endian** when the `littleEndian` argument is
   omitted (JS parity; this corrects the wave brief, which had it backwards).
   Typed arrays themselves use platform byte order, as in JS.
4. `Array(n)` is dense with element-type zero/default values; no hole state
   exists (resolves the parked `dense_array_holes` marker when the wave
   lands).
5. Typed-array index reads type as `number | undefined` (JS-exact) with the
   in-range literal refinement; out-of-range writes are ignored.

Fixed-size backing stays as recorded (never a resizing vector). Spec
amendments and the ~90-case corpus list land corpus-first in the typed-array
wave, queued after the std runtime waves; the wave owner first closes whatever
array indexing path it needs (ranked wave 3). The marker promotions listed in
the proposal ride that wave.

## D70 - Third-party backends: vendored static builds packed with cp3c (user directive, 2026-10-18)

The spec names the backing library for each native-backed family (README SS20,
SS13, SS14): libsodium for `std/crypto`, libuv plus uWebSockets/uSockets for
`std/network` (fetch on the client side, server and WebSocket on uWebSockets),
TBB under `parallel.*` (tbb::parallel_for / parallel_reduce / parallel_invoke,
SS13). Rulings and staging:

1. **Downloaded once, packed with cp3c, never user-installed** (user directive).
   Each library is built as a pinned static archive with the same MinGW-w64 GCC
   toolchain cp3c drives and committed under `vendor/third_party/<lib>/` with headers,
   upstream LICENSE (and NOTICE where present), and a pinned VERSION file,
   shipped alongside cp3c exactly like a bundled binary.
2. **Reproducible manifest.** `vendor/third_party/MANIFEST.md` records upstream URL,
   pinned tag or commit, verbatim build commands, artifact SHA-256 hashes, and
   the verified link flag list; `vendor/third_party/build.ps1` recreates every
   artifact from the pinned source.
3. **Beta-one scope.** uSockets/uWebSockets build without TLS (HTTPS/TLS stays
   out of scope, SS20) and without compression. First target: Windows x64
   MinGW-W64 (ucrt, posix threads). libuv is the one event-loop library
   (SS14/SS20); on Windows uSockets rides libuv as well.
4. **Placement and linking.** Generated programs link the vendored archives
   statically; cp3c locates `vendor/third_party/` relative to its own executable (the
   D56 materialization pattern) rather than any system or environment path.
5. **std surface staging unchanged (D68).** Module behavior stays in the
   runtime behind the three mechanical tables for now; the README SS20 end
   state is the same natives bound through `import native` (SS19) from
   cp3-source std wrappers once classes/object literals land. The vendored
   archives are the identical binding target either way.
6. **`worker` unaffected.** SS13 specifies one real OS thread per worker; the
   existing std::thread runtime already matches that. Only `parallel.*` moves
   to TBB, and the existing std::thread implementation stays until that
   alignment wave lands.
7. **Event loop.** SS14/SS20 pin libuv as the async event loop; D62 landed a
   hand-rolled loop with libuv-matching semantics. The network wave decides
   in-wave between re-basing the loop on libuv (the spec path) and keeping the
   hand-rolled loop with libuv isolated inside fetch internals; either way the
   D62 corpus semantics are preserved.

Evidence: `build/scratch/vend/WAVE-VEND-REPORT.md` and
`vendor/third_party/MANIFEST.md` (artifact SHA-256s, exact build commands, verified
consumer link flags). Pins as delivered: libsodium 1.0.22 (tag 1.0.22-RELEASE),
libuv v1.53.0, uWebSockets v20.80.0 including its in-tree uSockets, oneTBB
v2023.1.0.

## D71 - One event loop per thread, never a second loop (user directive, 2026-10-18)

The user flagged the classic mistake this project must never make: a second
event loop for `std/network` (libuv/uWebSockets) running next to the language
runtime's own loop. Two loops on one thread fight each other: one blocks or
starves the other, timers fire late while the server turns, the exit rule
forks, and every cross-loop hop adds latency for nothing.

Ruling:

1. **One loop instance per thread, always.** The runtime `EventLoop`
   (cp3/async/eventloop.hpp) is the single loop of its thread. Timers,
   promises and microtasks, fetch, server and WebSockets are all driven by
   that one loop; no module may run a second loop of its own. `worker`
   threads each get their own single loop (spec SS14 is one loop per thread,
   not per process); that is one loop per thread, not two.
2. **libuv is subordinate, never a peer.** `std/network` creates one
   `uv_loop_t` per thread and installs it on the runtime loop as an external
   event source through three opaque callbacks (`setExternalSource(poll,
   waitTurn, wake)`; the loop itself never sees a libuv type, and generated
   code never includes uv.h). `run()` drives libuv with non-blocking turns
   while it has its own work, and with bounded blocking turns when it has
   none; the source arms its own wake timer so runtime timers still fire on
   time while the loop sits inside libuv, and `postFromThread` wakes libuv so
   worker results are serviced promptly.
3. **uWebSockets rides the same libuv loop.** The network module hands uWS
   its loop with `uWS::Loop::get(uvLoop)` (a borrowed alien loop uSockets
   never frees), so every server, socket and client handle uWS creates lives
   on the one libuv loop the runtime loop drives. Fetch uses that same loop
   directly.
4. **D62 semantics unchanged.** A live fetch, listening server or open socket
   is pending work (`retainExternal`/`releaseExternal`); microtask order and
   timer ordering stay exactly as the D62 corpus pins them.
5. **Zero cost without the import.** A program that does not import
   `std/network` installs no external source and never links libuv; its loop
   behaves exactly as before.

Enforcement: the network wave is currently implemented against this shape
(verified in the working tree: eventloop.hpp event source callbacks +
network_loop.hpp). The merge gate confirms the single-loop property before the
wave lands, and every future async module (fs, worker messaging, and so on)
inherits the same rule.

Clarification for `parallel.*` and `worker` (user question, 2026-10-18). A
`parallel.*` closure runs on the shared CPU pool and gets no loop at all:
`parallel.*` is synchronous by contract (spec SS13 Rule 1, the calling thread
blocks until every closure finishes), so `await` inside a parallel closure has
nothing that could drive it and is rejected as a diagnostic when the parallel
lowering wave lands. Fanning out over async work has two sanctioned shapes and
neither adds a loop: I/O concurrency inside the one loop is `Promise.all`
over fetch/timers (libuv multiplexes, no extra threads), and isolated work per
thread is `worker`, whose thread runs its own single loop per this decision
and hands its result to the caller's loop with `postFromThread` (the only
sanctioned cross-thread channel: queue a task on the target loop, then wake
it; the loop drains the queue between macrotasks, and after this decision the
wake also nudges libuv out of a blocking turn).

## D72 - Std package layout: feature folders under src/std (user directive, 2026-10-18)

The layout is phase-first (lexer, parser, sema, codegen, runtime, cli) and that
made every std module a scatter: "change crypto" means remembering five paths
across four phases (sema check, codegen lowering, runtime TU, ABI header,
native rows). The user's ruling: the compiler core stays phase-first; the std
package becomes feature-first. One folder per std module family under
`src/std/<family>/` holds every slice of that module:

- `sema.cpp` - the type surface checks (was `sema/src/checks/std_<name>.cpp`)
- `codegen.cpp` - the lowering (was `codegen/src/emit/std/<name>.cpp`)
- `runtime*.cpp` plus private headers - the implementation
- `native.hpp` - the toolchain rows (was `cli/src/detail/native_std_<name>.hpp`)
- `include/cp3/std/<name>.hpp` - the generated-code ABI header (was
  `runtime/include/cp3/std/<name>.hpp`)

Rules:

1. **ABI spelling unchanged.** Generated code still includes
   `cp3/std/<name>.hpp`; the CLI assembles per-family include roots (added by
   the modules the program actually imports). This generalizes the D68 row
   table to every std module: include root plus optional native libs plus
   optional runtime TUs.
2. **Names.** Full words (`typed-arrays`, never `tarrays`); phase-named files
   (`sema.cpp`, `codegen.cpp`, `runtime*.cpp`, `native.hpp`).
3. **Scope.** All std modules (console, json, math, os, path, process, string,
   crypto, network) plus the typed-arrays family in one sweep. `tests/cases/**`
   does not move; the corpus categories stay exactly as pinned.
4. **Mechanics.** `git mv` plus include-path fixes; CMake source lists;
   `src/cli/src/toolchain/util.cpp` include roots and runtime discovery;
   `vendor/tools/lint_layout.py`, `ARCHITECTURE.md`, `STYLE.md` and every doc path
   reference in the same sweep. No behavior change, no new libraries.
5. **Timing.** Executed after the running feature waves land and are committed
   (never move files under a running agent), before the nothing-missing audit
   and the perf work. Acceptance: canonical rebuild, lint 0 errors, cold full
   corpus 0 failed, crypto and network native smoke programs still link.

## D73 - std/network transport and request-error rulings (network wave, 2026-10-18)

The network wave implemented `std/network` against D71 and the frozen std docs.
Four spots the spec leaves to the implementation, decided in-wave, recorded in
code comments and pinned by run cases:

1. **Request error isolation.** A synchronous handler that throws is caught by
   the route dispatch and answers `500` + `internal server error` for that
   request only; the server keeps serving. This is the spec's deliberate
   carve-out from the global unhandled-rejection rule, pinned by
   `network_handler_throw_500` (500, then a second request still 200).
2. **Fetch transport.** One TCP connection per request (`Connection: close`),
   the body fully buffered before the promise settles so `text()`/`json<T>()`
   never suspend, plus a per-request timeout. Redirects, keep-alive pooling and
   request streaming are missing surface (the round 4 network completion), not
   policy.
3. **`https://` fetch and `wss://`** reject with a clear runtime message
   instead of hanging or half-working (TLS is off, D70); no TLS backend is
   vendored.
4. **WebSocket handles.** The `on("connection")` listener receives a
   `WebSocket&` referencing stable per-server storage (a runtime-side
   deviation from a by-value reading of `(ws) => {}`, invisible in cp3
   source); client masking keys come from `std::mt19937_64` and are not
   crypto-grade (follow-up, not correctness-critical for beta one). One
   connection listener per server and one handler per event per socket,
   matching the documented examples.

Evidence: `build/scratch/network/WAVE-NETWORK-REPORT.md` sections 4-6; run
cases `network_fetch_local`, `network_fetch_404`, `network_websocket_echo`,
`network_handler_throw_500` in `tests/cases/stdlib/`.

## D74 - EventEmitter is a language builtin, not a std module (user directive, 2026-10-18)

`EventEmitter` joins `Promise`, the timer globals and the other core runtime
types: always in scope, no import, and no `std/events` module at all -
`import { EventEmitter } from "std/events"` is an error (the loader reports
that the module is not importable and points at the builtin). Rationale: the
type needs no OS/native surface (it is a data structure over listener
callbacks), and event handling is core to the language's own event loop the
same way timers are, so gating it behind an import would sit oddly next to
`async`/`await`, `Promise` and `setTimeout`. Docs: taught in the
language-spec async chapter next to the event loop material, listed under
"built into the language - no import needed", removed from the
imported-std-modules doc and the std overview list. Implementation: no
`std/events` registry row, an import-illegal diagnostic, and the six pinned
`events_*` cases flipped - all landing with the std completion round that
implements the builtin (R2, the date/json round).

## D75 - console gains continuous event listening: on/off (user directive, 2026-10-18)

`console.on(event, listener)` / `console.off(event, listener)` join the
console surface so a program can keep listening instead of asking once:
`"keypress"` (same vocabulary as `readKey`: named keys like
`"ArrowUp"`/`"Enter"`, or the literal character), `"line"` (each completed
line, terminator removed), and `"close"` (once, with `""`, at stdin EOF).
Listeners persist until removed and use the same on/off listener model as the
language's built-in `EventEmitter` (D74); the input pump starts with the first
listener and stops with the last, never blocking the event loop (D71). Lines
and keys fan out: a pending `input()`/`readKey()` and the matching listeners
both see the same line or key. Docs: surface block and prose in
`docs/standard-library/04`, member list in the day-2 guide. Implementation:
the round-1 console wave owns the stdin pump; the listener bookkeeping is
console-internal, so there is no round dependency on the events wave.

## D76 - console.hide()/console.show() hide and restore the console window (user directive, 2026-10-18)

`console.hide()` hides the console/terminal window the program runs in and the
program keeps going: the event loop is untouched and printing continues into
the hidden window's buffer. `console.show()` restores it. Windows
implementation: `GetConsoleWindow()` plus `ShowWindow(SW_HIDE/SW_SHOW)`,
guarded and never throwing. Off Windows, and on Windows when the process has
no console window (piped streams, a service), both are documented no-ops -
Linux/macOS terminal windows belong to the terminal emulator, not to the
process, so there is nothing portable to hide. `.clear` stays the
contents-clearing member; `.hide`/`.show` act on the window itself. Docs: 04
signature + example + prose, day-2 member list. Implementation: the round-1
console wave (console runtime TU, Windows-guarded; add `-luser32` to the
console native row only if `ShowWindow` needs it).

## D77 - RequestInit.headers is Map<string, string> (lead ruling, 2026-10-19)

The signatures say `headers?: Map<string, string>` and that is the answer: a
request init's `headers` (like `FetchResponse.headers` and
`HttpRequest.headers`) is the collections `Map<string, string>`. The round-2
imported-std-modules fetch example that showed a plain object literal was the
doc side of the disagreement and is rewritten to build a `Map` (fetch
example in `docs/standard-library/04-imported-std-modules.md`). A plain object
literal in that position is off the documented surface; it currently stops at
the retired codegen placeholder ("fetch headers as an object literal"), a
typed-shape error candidate for the audit phase. Pinned by
`tests/cases/stdlib/network_headers_map_field.cp3` (promoted out of
`_ambiguous` with this ruling).

## D78 - parseInt/parseFloat invalid input yields NaN (lead ruling, 2026-10-19)

The two global parsers return `NaN` when their input contains no parsable
digits of the selected form (`parseInt("abc")`, `parseInt("0x")`,
`parseFloat("abc")`), matching JS rather than the bare `strtol`/`strtod`
zero. STDLIB docs section 3 says they are "backed directly by
`std::strtol`/`std::strtod`" - that sentence pins the implementation family,
not the observable result of a failed parse, and the design goal (day-2
onboarding for JS/TS developers) makes the JS answer the only non-surprising
one. Zero would turn every malformed input into a silent legitimate value.

`parseInt` honours a `0x`/`0X` prefix as base 16 (D46, now implemented), so
`_ambiguous/parseint_hex` is promoted to the graded
`tests/cases/stdlib/parseint_hex_prefix_upper.cp3`. The ruled behavior is
pinned by `tests/cases/stdlib/parseint_invalid.cp3` and
`tests/cases/stdlib/parsefloat_invalid.cp3`.

## Audit promotions (2026-10-19, lead)

- D44 promotion: `stdlib/_ambiguous/map_iteration_order` is now the graded run
  case `tests/cases/stdlib/map_iteration_order.cp3` (out-of-order insertion,
  `keys()` prints `b,a`).
- `stdlib/_ambiguous/date_getter_timezone` is promoted to the graded run case
  `tests/cases/stdlib/date_getter_utc.cp3`: Date getters read the UTC
  calendar (`2020-01-01T20:00:00Z` prints `1,20`), matching the pinned UTC
  semantics in the stdlib date section; the timestamp is chosen so a
  local-time regression fails on any machine.
- D47 promotion: `stdlib/_ambiguous/buffer_ascii_encoding` is now the graded
  check case `tests/cases/stdlib/buffer_ascii_encoding.cp3` (CP3401 at 3:1,
  `encoding "ascii" is not supported`).
- `arrays-strings/_ambiguous/forin_map_order` stays a marker: `for (let k in
  m)` over a Map hit the retired placeholder diagnostic for this statement; the
  pin waits for the `for...in` lowering gap (tracked in AUDIT-FOLLOWUPS.md).

## Audit promotions, second batch (2026-10-19, lead)

Rulings pinned by canonical-build probes; docs are silent on each, so they
follow the standing JS-parity design goal (see D78).

- errors: there is no built-in `Error` type (`throw` takes any value);
  the catch binding is block-scoped (CP3301 after the block); `catch { }`
  without a binding is allowed; `try { } finally { }` without catch is
  allowed; an out-of-bounds index is a hard runtime RangeError that
  terminates (exit 1, not catchable by a cp3 catch); an uncaught top-level
  throw terminates with exit 1.
- enums: raw numbers are not assignable to enum types (CP3401);
  auto-increment after an explicit initializer is previous + 1 (prints 6);
  a member without an initializer after a string-backed member is CP3401;
  mixed backing is CP3706; `console.log` of a number-backed member prints
  the underlying number.
- async: `await` on a non-Promise unwraps the value (JS); `Promise.resolve`
  is present; `.then` returns a chainable Promise; an unhandled rejection
  terminates with exit 1.
- Still open (deferred until the touching waves land, tracked in
  AUDIT-FOLLOWUPS.md): `finally_return_overrides`,
  `compare_member_to_number`, `numeric_value_via_index`,
  `async_arrow_function` (B16).

## CLI/tooling marker retirement (2026-10-19, lead)

The 23 `cli/_ambiguous` markers are retired: they record CLI, packaging and
tooling behavior that is not observable from a .cp3 program (the deferred list
already classed them "not testable cases"). The behavior they describe is
covered where it is testable: the `cli` corpus family (run/check/exit-code
cases), the packaging acceptance scripts (install/TESTMODE/path handling,
doctor, init), the intel formatting checks (`cp3 fmt` byte parity), and the C1
lane's pack/install round trip. Post-beta-one CLI features (GitHub repo
installs, registry-style specs, `build.cacheBinary`/`excludeSource`) stay
explicitly deferred with named diagnostics per the M2 plan, not a scaffold
exit.

## The `.cp3pak` binary cache (2026-10-22, cache-binary lane)

`build.cacheBinary` / `build.excludeSource` are no longer deferred: they are
implemented, with the following rulings.

- **Platform ids are normalized explicitly.** A platform id is a family name
  (`windows`, `linux`, `mac`) or an arch-qualified name (`windows-x64`,
  `linux-x64`, `mac-arm64`). The case is folded, the family aliases (`win`,
  `macos`, `osx`, `darwin`, ...) and the architecture aliases (`x86_64`,
  `amd64`, `aarch64`, `armv7`, ...) fold to their canonical spelling, and an
  unknown id is a **manifest error** at build time - never a silent ignore.
- **`cacheBinary: true` means "the platform this build runs on".** It resolves
  to the current machine's canonical id at build time, and the resolved id is
  recorded in the archive (`cp3pak.json`), so the archive means the same thing
  on every machine. An array lists the platforms the package claims; a family
  claim is satisfied by any artifact of that family, an arch-qualified claim
  only by that exact platform.
- **The archive layout is `pak/<platform>/cache.json` + `pak/<platform>/<module>.o`**,
  where `<platform>` is always the canonical arch-qualified id. The container
  manifest gains the optional `cachePlatforms` and `cacheClaims` arrays; an
  archive without a cache keeps the manifest shape it always had, and the
  container `formatVersion` does not move (the new members are additive).
- **The artifact is a plain (non-LTO) object file** compiled from the
  package's own transpiled C++ with the ordinary toolchain driver. The
  package's entry `main` is renamed while it is compiled, so the artifact is a
  library object and never collides with the consumer program's own `main`.
  A static archive was considered and rejected: it would need `ar`/`lib.exe`
  next to the compiler driver, which the toolchain contract does not promise.
- **The source is dropped only when the archive carries every cache the
  package claims.** `excludeSource` with an empty/missing `cacheBinary` is a
  build-time validation error; `excludeSource` with a claim this machine
  cannot target keeps the source (with a warning) so `cp3 recompile` can add
  that platform from another machine, and drops it as soon as the last claimed
  platform is cached. `index.cp3` and the C++ headers are never dropped.
- **`cp3 recompile <file.cp3pak>` is idempotent and additive.** It replaces
  only the current platform's cache, preserves every other member and every
  other platform's cache, leaves an already-current cache byte-identical, and
  refuses an archive that carries no C++ source (the `excludeSource` case)
  with a clear message.
- **Cross-compilation is never forced.** Only the platform the tool runs on is
  produced, by the build and by `cp3 recompile` alike.

## Audit promotions, third batch (2026-10-19, lead)

- D49 promotion: `stdlib/_ambiguous/console_log_array_format` is now the
  graded run case `tests/cases/stdlib/console_log_array_format.cp3`
  (`console.log([1, 2, 3])` prints `[1, 2, 3]`).
- JS parity (D78-style): `json_stringify_indent_format` prints the two-space
  indented form (`{` newline two spaces `"a": 1` newline `}`); promotes as a
  graded run case.
- JS parity: `Math.round(-2.5)` is `-2` (JavaScript rounds half toward
  +Infinity; C++ `std::round` rounds away from zero - the JS answer wins);
  promotes to `tests/cases/stdlib/math_round_negative_half.cp3`.
- JS parity: the global `String(x)` conversion is present (`String(5)` prints
  `5`); promotes to `tests/cases/stdlib/string_conversion_global.cp3`.
- `env.get` argument spelling (on the deferred list): the options object
  `{ default: T }` is the surface (already pinned by `env_get_default.cp3`);
  the named-argument spelling is a syntax error (CP3201), pinned by the new
  graded check case `tests/cases/stdlib/env_get_named_arg_syntax.cp3`.
- Retired markers: `fetch_non_2xx_no_throw` (pinned by the live-server corpus
  case `network_fetch_404.cp3`; a real example.com round trip cannot be a cold
  corpus case) and `fs_readdir_order` (the deferred list; ordering is
  OS-dependent by design and corpus tests must sort).
- Resolved later: `array_slice_exists` and `binarysearch_return_value` (both
  landed in the arrays/strings wave; see the promotion record below).
- Still open: `json_parse_untyped_typeof_narrow` (waits for the runtime `unknown`
  carrier; candidate after the types wave), and
  `stdlib/_ambiguous/regex_test_method` (carried by the new regex wave, M2
  wave 19, as a lead-authorized promotion).

## Audit promotions, fourth batch: operators + the enum comparison (2026-10-19, lead)

Ruled from canonical-build probes on the landed operators surface; the docs
are silent on each, so they follow the standing JS-parity goal (D78). The
operators wave also landed the runtime basis for each ruling
(`cp3/core/bitwise.hpp` implements ECMAScript ToInt32/ToUint32 with the shift
count masked by 31).

- `bitwise_wide`: bitwise/shift are the ECMAScript int32 operations;
  `1 << 40` prints `256`.
- `eq_fixed_mismatch`: `==`/`===` between two fixed mismatched types is legal
  and false for both forms (`1 == true` and `1 === true` print `false`; D15).
- `nan_print`: printing a NaN number prints `NaN` (D9).
- `null_undefined_eq`: `null == undefined` is true, `null === undefined` is
  false (D15).
- `unary_plus_string`: unary `+` on a string is a compile error, not a
  conversion (CP3406 at 2:1, `invalid operand type 'string' for unary '+'`;
  D17).
- `unsigned_shift_negative`: `-1 >>> 28` prints `15` (ToUint32 semantics).
- enums `compare_member_to_number`: a number-backed enum member compared with
  a plain number compares the underlying value (`Color.Red === 0` is true;
  codegen casts through `static_cast<double>` in `emitEquality`).
- Union clarification (with the arrays-strings landing): loose `==` over a
  one-sided value union coerces through JS abstract equality (spec 05:
  `1 == "1"` is true for a union-held value), while a fixed non-union type
  mismatch stays false under D15. Both are pinned in the corpus
  (`union_eq_coerce`, the mixed-array cases), and the landing resolves the
  flagged D15/spec-05 clash in spec 05's favor for the union case only.

Six operators markers and the enums marker promoted (registry 58 to 51);
operators is the first family with zero ambiguous markers. Still open with
owners in AUDIT-FOLLOWUPS.md: `numeric_value_via_index` (still a raw C++
compile error), `finally_return_overrides`, `async_arrow_function`, plus the
arrays/network gaps listed in the third batch.

## D79 - `std/network` server scale mode: `createServer({ threads })` (user directive, 2026-10-19)

The single-loop server stays the default, byte-for-byte: `createServer()` with
no options behaves exactly as shipped and as net-suite pins it. Scaling a
server across cores is an explicit, opt-in option on the construction call:

```cp3
const app = createServer({ threads: os.cpus() });
```

Surface: `createServer(options?: { threads?: number }): HttpServer`; the
existing no-arg call is unchanged.

- `threads: n` starts `n` event loops, one per OS thread, each running its own
  uWS app instance. `threads` must be an integer >= 1; `1` and the absent
  option are today's single-loop server exactly.
- A connection is pinned for its whole lifetime to the loop that accepted it
  (WebSocket upgrades included), so handlers and per-connection state stay
  single-threaded and no user-facing synchronization appears.
- Module-level state is per-loop, the same isolation `worker` already has: `n`
  threads means `n` copies of every module-level binding. Cross-loop state
  goes through `worker` message passing, never through shared memory.
- Platforms: POSIX binds one `SO_REUSEPORT` socket per loop and lets the
  kernel distribute accepts. Windows has no `SO_REUSEPORT`, so the
  shared-listener path creates one listening socket and hands accepted
  connections to the loops (`WSADuplicateSocket`); the measured bench rows run
  on Windows, so this path is mandatory, not a stub.
- Non-goals: not CPU data parallelism (`parallel.*` owns that), no atomics,
  locks or shared closures exposed (§13; binding-level atomics remain rejected,
  as re-affirmed today on user review), and `WebSocketServer` broadcast stays
  per-loop with cross-loop fan-out as a documented `worker`-channel pattern.

Considered and rejected: `n` threads sharing one app with locks around handler
dispatch (reverses the no-locks guarantee and serializes exactly the work the
option exists to parallelize), and a thread pool inside the existing loop (uWS
is single-threaded by design; the loop is the unit of scaling).

Acceptance for the queued wave (full text in M2-PLAN): net-suite cases stay
byte-identical with `threads` absent; per-loop accept and module-state
isolation probes with `threads = os.cpus()`; `ws-echo` and `json-api` bench
rows at `threads=1`, `threads=N` and the Go comparator under the D60/D65
conventions.

## Audit promotions, fifth batch: arrays-strings (2026-10-19, lead)

Ruled from canonical probes on the landed arrays-strings surface; docs are
silent or already ruling, so the standing JS-parity goal (D78) applies.

- `array_mixed_infer`: a mixed-type array literal without an annotation is
  CP3401 at 2:1, `type mismatch: array literal mixes incompatible element
  types (annotate the array with a union type)` (docs 09 lines 16-31).
- `array_print_format`: `console.log([1, 2, 3])` prints `[1, 2, 3]` (D49).
- `binarysearch_unsorted`: `a.binarySearch(t)` on an unsorted array panics
  with a RangeError, exit 1 (docs 09 line 49: debug sorted-check).
- `find_not_found` / `indexof_not_found`: not-found reads are JS-shaped
  (`undefined` / `-1`).
- `slice_negative` / `substring_swap`: `"hello".slice(-2)` is `lo`;
  `"hello".substring(3, 1)` swaps to `el` (JS argument normalization).

Seven markers promoted out of `_ambiguous` (registry 51 to 44). Keepers:
`forin_map_order` (D44: `Map` iteration order is a collections bug) and
`str_index_type` (string index reads: check typed it `number`, run stopped at
the retired placeholder diagnostic; the byte-read lowering is in the follow-up
fix below).

## Index-access fix: string byte reads + enum index operands (2026-10-19, lead)

The arrays-strings wave left two index-access gaps that leaked the retired
placeholder diagnostic or a raw C++ error; both are fixed here and pinned.

- `str[i]` (docs 09: "returns a raw byte") lowers to
  `::cp3::runtime::strings::byteAt`: the UTF-8 byte at a byte position as a
  number. Out-of-range, fractional and negative positions follow the array
  index rule: a hard RangeError ("String index out of range"), exit 1, not
  catchable by a cp3 catch. `str_index_type` promotes expecting `195`
  (`"héllo"[1]` is the first byte of `é`, 0xC3; the wave report's U+01F8 note
  describes the rule, the committed marker source holds the 2-byte `é`);
  `str_index_out_of_range` pins the panic. Literal receivers normalize
  through `literalBaseType()`
  (the same step `type_map.cpp` does), which is what lets a string literal
  reach the family read at all.
- A number-backed enum member used as an array index is its underlying number
  (`a[Color.Green]` reads index 1), matching the member-vs-number comparison
  rule from batch 4; a string-backed enum index reports instead of emitting a
  non-convertible operand. `numeric_value_via_index` promotes expecting `20`.
- `array.slice` and `binarySearch` markers close under the wave's landed
  surface: `array_slice_exists` promotes (D48; `[1, 2, 3, 4].slice(1, 3)` is
  `2,3`) and `binarysearch_return_value` promotes as the index, `-1` when the
  target is missing (docs 09's `binarySearch(target)`).

Four markers promoted plus one new pin (registry 44 to 40); only
`forin_map_order` remains in arrays-strings (D44 collections bug).

## D80 - Workers are explicit handles: `new Worker(module)` + message events (user directive, 2026-10-19)

User review of the landed call form (`import worker from "./x.cp3";
await worker(v)`): the surface cannot tell a worker from a plain function (the
compiler inferred "worker" from the import *binding name*, so renaming the
binding changed semantics), and a call-shaped one-shot is the wrong fit for a
long-lived background thread. Ruled: workers are explicit handles with message
events, matching the JS Web Worker model the spec already cites. This is the
worker half of the same conversation that settled D79.

Surface (spec §13):

```cp3
// main side
const w = new Worker("./sum.cp3");
w.on("message", (msg) => { ... });
w.on("error", (err) => { ... });
w.postMessage(value);
w.terminate();

// sum.cp3
export default (thread) => {
  thread.on("message", (msg) => { thread.postMessage(result); });
};
```

- `new Worker(path)`: `path` is a **static string literal** naming a `.cp3`
  file, resolved at compile time under the import rules; the file is compiled
  as a worker unit whose default export runs once at thread start with the
  worker-side `thread` handle. A computed path is a compile error. The
  binding-name inference is removed: `new Worker` is the only way to start a
  worker, and an ordinary default import of a `.cp3` file stays an ordinary
  cross-module call.
- `w.postMessage(v)` / `thread.postMessage(v)`: fire-and-forget structured
  clone (genuine deep copy both ways, per the no-shared-memory rule); messages
  queue until the other side attaches a listener; delivery is FIFO per sender.
  Nothing blocks the main loop - it keeps turning between messages.
- `w.on("message", l)` / `w.on("error", l)` on both sides; multiple listeners
  allowed. A thrown error inside a message handler arrives as an `"error"`
  event (catchable); a hard crash still takes the process down, per the
  concurrency batch ruling.
- `await w.request(v)`: optional one-shot ask/answer sugar over the same
  channel (correlation-tagged); resolves with the next `"message"`. The old
  `await worker(v)` form is removed.
- `w.terminate()`: stops the worker's loop and frees the thread; pending
  messages drop; idempotent.
- Module-level state stays per worker (isolation); no locks, no atomics, no
  shared memory. The D79 server scale mode uses this channel for cross-loop
  fan-out.

Status: the landed concurrency wave keeps the call form for now; the handle
wave (queued in M2-PLAN right after the active landing queue) implements this
surface and migrates the worker corpus cases. The markers
`worker_call_shape` and `worker_crash_takes_process_down` stay ambiguous until
that wave, then promote against this ruling (canonical handle shape; thrown
handler error becomes a catchable `"error"` event).

### D80 addendum - typed channels and the inline form (lead freeze, 2026-10-19)

Final surface freeze before the handle wave launches (user-approved; supersedes
the annotation-free reading of D80 above where the two differ).

- The worker entry annotates its handle: `thread: Thread<In, Out>`; `In` is
  main-to-worker, `Out` is worker-to-main. That annotation is the single source
  of truth: the main side infers `Worker<In, Out>` for `new Worker(...)`, so
  main-side code needs no annotations of its own. A missing annotation on the
  worker entry (file default export or inline arrow) is a compile error.
- `void` in one direction makes that direction's send surface a compile error:
  a `Thread<In, void>` worker cannot `thread.postMessage`, and `w.postMessage`
  / `w.request` on a `Worker<void, Out>` cannot send. `Thread<unknown, unknown>`
  waits for the runtime unknown carrier (opt-in, later).
- Payloads are ordinary cp3 value types and copy on send exactly like an
  assignment copy (numbers, strings, booleans, arrays, records, enums, unions
  of those). Functions, closures, promises, live handles and unresolved
  (unknown) payload types are compile errors.
- Inline form: `new Worker((thread: Thread<In, Out>) => { ... })` compiles
  through the same worker-unit pipeline as a file (synthesized unit, its own
  module state, no access to enclosing locals - pass data with messages). The
  file form stays the recommendation for anything with structure; moving from
  inline to file is moving the body and swapping the arrow for a path.
- `await w.request(v)` resolves with the next `Out` message; on the worker
  side messages arrive as `"message"` events.

## Audit promotions, sixth batch: concurrency (2026-10-19, lead)

Ruled from the wave report (section 5, "decisions where the docs were silent")
and canonical probes on the landed concurrency surface.

- Structural rulings (report 5.1-5.5, 5.7-5.10): `parallel` stays a bare
  ambient builtin (D34, no import); ranges are end-exclusive with
  `static_cast<std::int64_t>(expr)` bounds; the chunked `parallel.reduce`
  combine gate covers exactly `acc + i` from literal `0`, `acc * i` from
  literal `1` and `i > acc ? i : acc` from literal `0`; every other reduce
  keeps the exact sequential 4-argument form; `parallel.run` yields a
  homogeneous array carried by the `$parallelRun` marker; closures lower as
  `[&]` capture lambdas behind the sema capture-safety walk; worker objects
  mark through exactly one un-reassigned single-declared binding; a worker
  argument that cannot be proven copyable reports a copyability type error
  (never a wrong deep copy).
- `parallel_capture_diagnostic_text`: pinned to the existing wording; a
  captured reassignment is CP3902 at 2:1.
- `parallel_import_required`: bare `parallel.reduce(1..3, 0, (s, i) => s + i)`
  runs and prints `3`.
- `parallel_range_end_exclusive`: `0..n` is `[0, n)`; `parallel.reduce(0..5, 0,
  (s, i) => s + i)` is `10`. The old marker source tripped the capture rule
  (`n = n + 1` inside `parallel.for`), so the case carries the reduce form.
- `parallel_run_mixed_result_types`: mixed closure result types are a type
  error, not a first-closure pick. Codegen resolves every closure's result and
  reports CP3904 (`kParallelRunMixedResultTypes`, catalog row) at the
  `parallel.run` call (`2:9` in the case), exit 1 in run mode, no C++
  invocation. A closure whose result cannot be resolved reports a type error
  until codegen can type it.
- Held: `worker_call_shape` and `worker_crash_takes_process_down` stay
  ambiguous; D80 owns that surface. The handle wave promotes against its
  ruling (canonical `new Worker(path)` shape; a thrown handler error becomes a
  catchable `"error"` event; a hard crash stays process-fatal for beta one).

Four markers promoted (registry 40 to 36); concurrency keeps the two D80
markers. Probes: `build/scratch/rounds/evidence/concurrency/batch7-probe.txt`.

## Toolchain target is cp3c's problem, not the user's (2026-10-22, lead)

A stock Windows LLVM install with no MSVC was reported as `not usable`:
`clang++` defaults to the `x86_64-pc-windows-msvc` target, and with MSVC
absent that target has no C++ standard library at all, so even
`#include <cstdio>` fails. The driver was fine; the target was wrong. The
same driver compiles and links a C++20 program against the installed MinGW
GCC under `--target=x86_64-w64-windows-gnu`.

Ruling:

- **A driver is `ready` only if it can build.** The existing probe (compile,
  link, run) stays the definition of usability; nothing is reported ready on
  the strength of being on PATH.
- **The target is derived, never asked for.** cp3c asks the driver for its
  default triple (`-print-target-triple`) and rewrites a Windows MSVC triple
  to the matching GNU triple for the same architecture. The architecture comes
  from the driver, never from a guess or a hardcoded path.
- **The derived target is verified, not assumed.** The candidate target must
  compile, link and run the probe program before it is used. A target that
  fails is not used; the family falls back to the driver's own default and is
  reported `not usable` with the reason.
- **The probe and the build use the same flags.** The verified flags are part
  of the toolchain identity and are used by every real compile and link, so a
  family that reports `ready` is a family that builds. The target is part of
  the build-cache key: the same driver under a different target produces
  different objects.
- **No cross-compilation.** Only the current machine's architecture and its
  own installed toolchain are considered.
- **Windows static link under the GNU target names `libwinpthread`
  explicitly.** MinGW's `libstdc++` keeps its thread support there; the GCC
  driver adds the library itself, the Clang driver does not. Naming it keeps
  a Clang-built binary as static as a GCC-built one.
- **The user passes nothing.** No `--target`, no `--gcc-toolchain`, no include
  paths. A Clang install beside a MinGW GCC install is simply `ready`.

Evidence: `cp3c toolchain list` reports `clang ... ready` on the machine that
previously reported `not usable`; `cp3c run benchmarks/cases/hello-world/cp3/src/index.cp3
--toolchain clang` prints `Hello, world!`; the GCC path is unchanged. Two CLI
regression cases (`clang-is-usable-when-installed`, `clang-builds-and-runs`)
skip cleanly where no Clang is installed.

> Historical note: the "GCC path is unchanged" sentence describes the state
> before the 2026-10-25 Clang-only decision below. GCC is no longer a target
> compiler; the target mechanics in this entry still apply to Clang.

## Beta one target toolchain is Clang-only (2026-10-25, lead)

The earlier implementation had roughly **50% of a multi-compiler-chain design**
built: per-platform family preference, GCC/MSVC/Clang selection state, fallback
retry through another family, a compiler matrix in the test runner, and matching
CLI/package-manager docs. That work was replaced by a single target chain rather
than hidden behind a default.

Ruling:

- **Beta one compiles cp3 programs with Clang only.** The target is Clang 16 or
  newer. GCC and MSVC are not target compiler choices; a non-Clang driver is
  rejected with an explicit error.
- **Windows targets MinGW-w64 GNU** (`x86_64-w64-windows-gnu`). cp3c locates a
  MinGW-w64 sysroot through `CP3_MINGW_ROOT`, PATH, or standard WinLibs/MSYS2
  locations. It never invokes another compiler. `--gcc-toolchain=<prefix>` stays
  only as a Clang flag for locating that sysroot, never as a compiler selection.
- **Auto-detect the newest usable Clang**, with explicit `clang@<version>`,
  `clang++-<major>`, or a full path as overrides. A missing Clang prompts to
  install Clang/LLVM; build and cache paths call the same guard, so a missing
  compiler does not surface later as an opaque failure.
- **The host compiler for building cp3c is not constrained.** cp3c itself may be
  built by any C++ compiler; the Clang-only rule is about compiling cp3
  programs. Host-compiler guards such as `_MSC_VER` remain only for that reason.
- **Remove the other chains from the current product surface**: user-facing
  output, internal compiler-selection branches, tests, and current docs. Old
  commits and historical decision entries stay unchanged except for a
  supersession note.
- **The test runner is Clang-only.** It runs the corpus once against the Clang
  cp3c resolves; there is no `--toolchain` matrix and no
  `--allow-missing-toolchains`.

Evidence: `cp3c toolchain list` reports one Clang candidate (23.1.4 on this
machine); `cp3c toolchain use gcc --global` exits 2 with "not a usable Clang
compiler"; `cp3c toolchain use clang@23 --global` saves the exact policy; and
`cp3c run examples/basics/hello-world/main.cp3` prints `Hello, world!` through
`--target=x86_64-w64-windows-gnu`. The CLI regression suite covers a non-Clang
path rejection in addition to the Clang build/run cases.

## Spread arguments: two language rules (2026-10-22, lead)

The spread lane of the placeholder-retirement wave raised two questions that
are language decisions, not implementation details. Ruling:

- **A spread into a fixed-argument signature must be the last argument**
  (`f(a, ...xs)` is fine; `f(...xs, 1)` is **CP3403**). A spread fills the
  parameters it reaches, so an argument after it would have to be filled
  before the spread's elements are known. The alternative - letting the
  spread fill only the trailing parameters and leaving the later argument to
  the front - makes the arity depend on the spread's runtime length, which
  cp3 cannot check at compile time. The rule is a compile-time error.

- **A spread too short for a required parameter is a runtime out-of-range
  error, not a compile error.** The alternative - JS's `undefined` - has no
  cp3 spelling: a cp3 parameter type never admits `undefined` unless it is
  declared optional (`a?: T`), and a declared-optional parameter is a
  *different* type. So the program that cannot be typed is the program that
  reads out of range, and it fails the way any out-of-range read fails. A
  parameter the call may leave out (a default, or `?`) takes its default
  instead.

A related behaviour change, recorded because it is observable: **a class-method
call now uses the class member table's signature**, so the arity and
assignability checks see the declared optional and rest parameters. That is
what makes a class rest parameter work; it also means a class-method call is
checked the same way a function call is.

## Four spec rulings from the spec owner (2026-10-22)

The placeholder-retirement wave hit four points where the implementation could
not proceed without a spec decision. The spec owner ruled:

1. **Non-primitive unions are supported, as a runtime variant.**
   `number | string | null` lowers to a real variant value, not to a skip.
   This is the largest unblock: it retires the "this declared type", "this
   destructured binding", "env.get whose value type has no C++ spelling", "==
   between different union types", "logical operator with mixed operand
   types" and "ternary with non-uniform branch types" markers together, and
   the `==` / logical / ternary rules follow from the variant.

2. **Generic arrows are valid cp3.** `<T>(x: T) => x` is a function with its
   own type parameters, inferred at the call site. The spec extends to
   generics on arrows, and the parser grows a production for it. The
   "generic arrow functions" marker is implemented, not skipped.

3. **The rest object in a destructuring pattern follows TypeScript.**
   `let { a, ...rest } = obj` types `rest` as the source's shape minus the
   named properties - the type-level Omit. cp3's type model grows the
   operation rather than faking a same-shape rest (which would read the
   removed properties as their default value, a wrong program).

4. **The language aligns with TypeScript, 99.99% TS-like syntax, with C++
   performance.** Applied to the class question: **a class IS a first-class
   value** in cp3, like a constructor reference in TypeScript - it can be
   passed and stored.

   This CORRECTS a ruling that was merged earlier: the class-literal rule.
   TypeScript REJECTS an object literal for a class with a constructor, so
   cp3 must too. The merged behaviour - "the literal's values are the
   constructor's arguments and the literal's field values are the instance's
   state" - is a cp3-specific invention and is now wrong. The class-literal
   case and its implementation move to a TypeScript-shaped error, and the
   follow-up lane is tracked in the roadmap.

Ruling 4 is the reason the "align with our principles" note matters: where
TypeScript has an answer, cp3 takes TypeScript's answer, and only the C++
implementation differs.

## Non-blocking native I/O: two mechanisms (2026-11-03, lead)

The v0.2 runtime must not freeze the single event loop when a native library
blocks. `async` alone is not enough: wrapping a blocking C call in an async
function still runs it synchronously on the loop thread.

The rule is two mechanisms, selected per library:

- **Socket path (v0.2 phases 1-2).** A library that exposes a socket fd and a
  non-blocking step API uses `std/io`:
  `io.waitReadable(fd)`, `io.waitWritable(fd)` and `io.pollUntilDone(getSocket,
  checkProgress)`. `pollUntilDone` re-calls `getSocket()` every iteration
  because the fd may change during connect. It exports `POLL_READING`,
  `POLL_WRITING`, `POLL_DONE` and `POLL_FAILED`. This path is implemented once
  in the runtime, not per package.
- **Pool path (v0.2 phase 3, deferred).** A pure blocking C library that has no
  pollable fd uses `import native async "<lib>" as x;`. Calls return Promises
  and run on one fixed shared offload pool (default 4 threads, configurable).
  Phase 3 does not start until phases 1-2 pass their tests.

Anything networked that can use a non-blocking API MUST use the socket path,
never the pool.

### Phase 1 and 2 contract

- One event loop, one thread. `std/io` adds fd readiness to the existing
  `EventLoop`; it does not create a second loop.
- `std/network` and `std/io` share exactly one libuv loop per thread
  (`cp3/async/uv_loop.hpp`). The `EventLoop` remains authoritative and drives
  libuv from its idle phase.
- Many concurrent waiters are supported, including several on the same fd with
  different directions.
- A waiter resolves only when its direction is ready. Poll error,
  hangup/disconnect, an invalid descriptor, or a descriptor closed while
  waiting rejects the promise. One shared 100 ms descriptor-validity check
  covers every pending wait; there is never a timer or thread per descriptor.
- Phase 1 exposes no cancellation token. A pending waiter cannot be cancelled
  through the public API; a future cancellation/abort model must define it
  explicitly.
- Poller/wakeup: Linux uses libuv's epoll backend; Windows uses libuv's IOCP
  backend. Both use libuv's async handle for cross-thread wakeup (eventfd/pipe
  internally on Linux, PostQueuedCompletionStatus on Windows).

### Phase 3 preconditions (not implemented yet)

If phase 3 starts, it must obey all six rules:

1. One fixed shared pool, configurable, shared by all libraries; never one
   worker or pool per library.
2. Calls on the same handle run strictly one after another; different handles
   may run in parallel.
3. Strings and buffers are copied into owned memory before dispatch.
4. Pool threads never touch the cp3 runtime; they run only the C call and write
   plain result structs. The loop thread converts the struct into cp3 values.
5. Completion posts to the loop so an otherwise idle loop wakes immediately.
6. Handles may opt into a dedicated thread for libraries that require every
   call from the same thread.

Known limits, documented rather than hidden:

- Pool size caps concurrency; the (N+1)th blocking call queues. This is fine
  for short/CPU-bound work and bad for multi-second blocking network calls.
- A started blocking call cannot be cancelled; cancellation can only discard
  the result.

Non-goals: multiple event loops, per-library workers, and syntax changes beyond
the optional `import native async`.

### Phase 1-2 acceptance

1. A 100 ms timer fires roughly 20 times while a socket wait resolves after
   about 2 seconds.
2. 1000 concurrent waiters on distinct sockets resolve correctly with no
   thread growth.
3. Closing the fd while waiting rejects the promise with no hang or crash.
4. Windows and Linux use the documented poller and wakeup mechanism.

## D81 - Automatic reclamation of reference cycles (user directive, 2026-11, lead)

CP3401 (`reference cycle ... has no weak edge`) stays the rule for a cycle
the compiler cannot prove is safe to collect automatically, and it is retired
for one only when it can. A cycle dies with the last strong reference to any
member, exactly like an acyclic island, without a `weak` edge from the user.

Design (docs/.rough/design/cycle-collector.md):

- **Compile-time classification.** The sema builds the whole-program
  strong-reference graph (class fields incl. inherited and statics, arrays,
  containers, tuples, unions, collections, interface implementers and
  closures at type level) and computes strongly connected components. A
  class in a component of size > 1, with a self edge, or that reaches the
  OPAQUE sink is cycle-capable; everything else is acyclic and costs
  nothing.
- **The collector-eligible fixpoint.** A cycle-capable class is eligible
  only when every strong edge is a walkable handle (a direct class handle
  or an optional of one) to another eligible class, and no edge is opaque.
  The collector runs at the event-loop safe point (never inside a
  macrotask, never on a background thread, single-threaded): a
  Bacon-Rajan trial deletion over the suspects list with a 4096-node /
  200-microsecond per-pass budget, resumable across passes, drained at
  loop exit.
- **CP3401 is retired per island, not per program.** A cycle-capable
  component whose members are all collector-eligible compiles and runs.
  Anything else keeps CP3401 (fail closed): an opaque edge, an interface or
  record field shape the visitor cannot enumerate, a type parameter, an
  ineligible member, or an unproven `auto-weak` candidate.
- **`weak` stays supported** with the same semantics; it becomes an
  optional optimization (deterministic earlier release), no longer a
  requirement. Eligible islands keep `Ref<T>` ABI; classes on the
  collector path use the `CycleRef<T>` handle with the trial bookkeeping
  in the same control block.
- **Numbers**: classes with no class-reference field are skipped
  entirely; a class with reference fields that cannot form a cycle
  (unordered, scalar-only shapes) stays plain refcounting.
- Constraints honoured: no tracing, no stop-the-world, no background
  thread, no atomic on the retain/release path, a per-pass budget, and
  collection only at safe points.
- ConcurrentMap is the one carve-out: a cycle-capable type whose values
  flow into a ConcurrentMap keeps a compile-time error asking for a `weak`
  edge (diagnostic pending).

Known limits: the collector handles cycles among classes with walkable
handle edges only; structural carriers (records, closures, type
parameters, `object`/`unknown`, interfaces without a closed implementer
world it can enumerate at the C++ level) keep the manual `weak`
requirement. The `.cp3` export/import of a cycle-capable class keeps its
eligibility analysis on the import graph the importing unit sees.
