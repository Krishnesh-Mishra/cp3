# M2 plan - living roadmap

Opened 2026-10-17 at M1 close: corpus **295 passed / 1 failed / 547 skipped /
110 ambiguous** (953 found) at commit `075bc20`. Source: the read-only M2
reconnaissance pass (all 547 skips attributed to their first blocking feature).
**This file is a roadmap, not a contract** - `DECISIONS.md` wins on any
conflict. Waves update it as they land.

**Status after wave 4** (async/await + event loop + timers - landed; evidence:
`build/scratch/async/WAVE4-REPORT.md`): **349 passed / 0 failed / 494 skipped /
110 ambiguous** (953 found); 54 flips (1 failed→passed, 53 skipped→passed),
0 regressions; runtime unit tests 80/80 checks green; lint 0 errors / 42 soft
warnings. Corpus growth since: **+142** (`arrays-strings/` +74, `stdlib/` +68;
both binaries agree - arrays-strings 47/0/109/10) and **+141** core-semantics
(`variables`, `operators`, `control-flow`, `functions-closures`, `syntax`,
`types`; 115 pass / 21 skip / **5 spec-pinned failures**). Promotions per D58:
`async/microtask_macrotask_order` → real passing case; `str_length_utf8`
marker retired (D63). Running total (pre-wave-1): **1236 found - 516 passed /
6 failed / 606 skipped / 108 ambiguous**; all 6 reds are wave-1 targets (§5-6).

## Critical path (revised - see D59/D60)

Examples are showcase-only now (D59): no auto-run, no statuses - the corpus in
`tests/cases/` is the only automated oracle. The wave order below is the plan;
`examples/**` and the new cross-language `benchmarks/` suite (D60:
health-server + compute benchmarks) become fully runnable as their features
land (servers wait on `std/network`; multi-thread entries on `parallel.*`; the
games on wave 1 - wave 4 already landed).

**Status: the showcase rewrite + the benchmark suite are landed (D59/D60).**
All tables in `benchmarks/README.md` are real measured runs
(2026-10-17); cp3 `compute-single` already runs (lead reproduced `131434424`
on the post-wave-4 canonical binary - ~1.2 s vs cpp 0.2 s on this machine),
and the two pending cp3 rows (`health-server`, `compute-multi`) auto-fill when
`std/network` and `parallel.*` land. `async/timers-demo` runs (wave 4).

## Ranked waves (cases-unblocked × dependency-criticality)

| # | Wave | First-blocker cases (latent) | Key dependency |
|---|---|---:|---|
| 1 | Method-call codegen + per-receiver runtime (string, array, Math, Date/Buffer, JSON, fs/path/os/process, crypto/env) - **LANDED** (d409e33; 141 flips) | 132 (223) | arrays for array methods; classes for Date/Buffer/Map receivers |
| 2 | Classes + object model (§15, incl. generic classes, `memory-weak`) | 63 (68) | sema `declarations.cpp` wall first, then codegen |
| 3 | Array literals + indexing + array `.length` (§09) | 52 + 19 | prerequisite for `parallel.for` |
| 4 | **Async/await + event loop + timers (§14)** - **LANDED** (D62; 54 flips, 0 regressions) | ~35 | flips the only failing case; unblocks workers + network |
| 5 | Map/Set/EventEmitter (§20, insertion order pinned by D44) | 32 | wave 2 |
| 6 | Enums (§04) - self-contained | 27 | - |
| 7 | Try/throw/finally (§07) | 26 | - |
| 8 | Non-primitive locals/returns (unions, closures) (§03/§10) | 37 (~60) | shares machinery with 2/3 |
| 9 | Object literals + structural interfaces | 29 | - |
| 10 | Generic functions (§16) | 18 | wave 2 for the 5 class-mixed cases |
| 11 | Module local function linkage (codegen emits local functions around calls) | 6 (14) | closes the remaining `modules/` skips |
| 12 | Grammar tail sweep (switch 7, untyped params 7, arrows 6, compound assign 6, rest 5, bitwise 5, destructuring 5, for-of 4, console methods 4, labels 3, `parseInt` 3, `parseFloat` 2, `typeof` 2, …) | ~95 | mostly independent, S each |

Parallel lanes (status 2026-10-17): the **C1 CLI + package-manager lane** is in
flight alongside the perf wave - D61 exe → `.cp3/out/` layout + `clean`,
`cp3 init` spec parity, `.cp3pak` pack/install + `.cp3/lock.json`; sole writer
`src/cli/**` minus `src/cli/src/util.cpp` (perf-owned). The **D56
runtime-embedding lane** (single-binary guarantee) is queued behind the perf
wave so the embedded runtime TU set finalizes once. The **benchmark expansion
(B1)** is in flight with the senior Tier-1 task set (compute-single-int,
object-churn, array-growth, closure-capture, interface-dispatch, generics-mono;
ratio/RSS/size columns; the 20% gate) per D66. The **source-tree structure
sweep** (D66.5: `src/codegen`, then `src/lexer`/`src/parser`; behavior-neutral,
corpus byte-identical) is queued immediately after the perf wave. std/network, parallel/worker,
crypto, Date/Buffer, and the fs/env leftovers are post-perf runtime waves - they
sit in perf-owned paths today.

## Marker rulings owed (the 16 genuinely open)

`operators/eq_fixed_mismatch` (spec §3 vs §5 contradiction) ·
`types/type_alias_keyword` · `arrays-strings/array_mixed_infer` ·
`enums/{autonumber_value, numeric_value_via_index}` ·
`control-flow/{dense_array_holes, forin_array_indices,
spread_number_array_into_fixed_params}` ·
`concurrency/{parallel_capture_diagnostic_text,
parallel_run_mixed_result_types}` · `errors/{catch_binding_scope,
uncaught_throw_behavior}` · `memory-weak/{weak_literal_wrapper_typing,
weak_modifier_optional_type, weak_on_value_type}` ·
`stdlib/json_parse_untyped_typeof_narrow`.

Ruled this revision: `async/microtask_macrotask_order` → **D62** (promoted to a
real passing case); `arrays-strings/str_length_utf8` → **D63** (bytes; marker
retired - covered by `arrays-strings/str_length_accent_bytes`, which flips with
wave 1). Rule each remaining marker when its wave starts (context at hand);
promotion per D58. The other ~53 markers are already answered by binding
decisions (D15-D49) and are promotable into real cases; ~23 are CLI/tooling
markers (not `.cp3` cases); 16 are recorded as deferred.

### Candidate ambiguities parked by corpus growth (rule when the wave lands)

From the stdlib/arrays-strings growth pass (evidence in its report; no cases
written - verify-first rule):

1. `array_extra_methods` - §9's array list omits JS mutators `shift`/`unshift`/
   `reverse`/`splice` (+ `forEach`): in the language or not?
2. `array_index_out_of_bounds` - read (undefined vs panic) and write behavior.
3. `array_index_negative` - JS wrap-from-end vs `std::vector` UB.
4. `array_length_write` - assigning array `.length` (JS truncate/extend).
5. `str_charat_out_of_range` - `charAt(i)` past the end (JS returns `""`).
6. `str_index_assign` - `str[i] = …` (sema currently reports CP3401).
7. `parsefloat_invalid` - `parseFloat("abc")` / `parseInt("abc")` → NaN vs 0.
8. `math_sign` - `Math.sign` absent from the spec's Math signature list.
9. `json_stringify_undefined` - `JSON.stringify(undefined)` vs the `string`
   return signature.

From the core-semantics growth pass:

10. `let_no_init_then_assign` - static type of an unannotated, uninitialized
    `let` (currently inferred `undefined`; later assignment rejected).
11. `function_expression` - are `let f = function(): void { … };` expressions
    in the language? (parser rejects today)
12. `iife` - parenthesized immediately-invoked function forms.
13. `nested_function_declaration` - nested declarations: sema accepts, codegen
    CP3002 today.
14. `empty_statement` - bare `;;;` legality/execution.
15. `narrowing_by_null_check` - does `x !== null` narrow `number | null`?
16. `console_log_multiarg` - variadic `console.log('a', 'b')` formatting
    (space-joined today; no doc pins it).

## Open bugs / hygiene (evidence cited; fix with the wave that touches the area)

1. **CP3504 false positive** - `if / else if / else` where every branch
   returns is reported as "may fall off the end of its body"
   (`src/sema/src/stmt_check.cpp:443`; documented with a workaround in
   `examples/README.md` and `examples/basics/fizzbuzz/main.cp3`). Still open
   after wave 4; fix with waves 1/2.
2. ~~**CP3301 initializer self-reference**~~ - **fixed in wave 4**
   (`src/sema/src/declarations.cpp`; `stdlib/timer_setinterval_clear` passes).
3. **Broken marker fixtures** (repair at promotion): `modules/_ambiguous/
   relative_specifier_form` (imports an absent `base.cp3`),
   `concurrency/_ambiguous/worker_call_shape` (imports an absent `x.cp3`),
   `modules/_ambiguous/prelude_strong_weak_order` (ships no `_prelude*.cp3`).
4. **Passing-but-stub-blocked audit** - `concurrency/parallel_field_write` and
   `generics/decorator_banned` pass on a real diagnostic but also carry a
   `CP3002: class declarations`; re-audit when wave 2 lands.
5. **Six red corpus cases - wave-1 targets (spec-pinned bug fuel):**
   - `variables/var_hoist_use_before_declaration` - `var` must hoist
     (`undefined` read before assignment; JS/§8 parity); currently CP3301.
   - `variables/module_let_read_from_function` - module-level `let` read from a
     function body emits raw C++ (`'g' was not declared in this scope`);
     expected `5`.
   - `operators/nullish_chain_literals` - `null ?? undefined ?? 5` prints
     `null`; JS skips both → expected `5`.
   - `functions-closures/default_param_undefined_arg` - `f(undefined)` must
     trigger the default (`7`); currently CP3401.
   - `types/nullable_three_way_null_identity` - `number | null | undefined`
     conflates null/undefined under `===`.
   - `async/await_settled_microtask_fifo` - a settled `await` must defer to the
     microtask queue (D62.1, JS parity); current runtime resumes inline.
6. **Suspected bugs S1-S6 (no cases yet; dropped verify-first by the growth
   pass; wave 1):** module-`let` write side also emits raw C++; `var` is not
   function-scoped; `var` redeclaration rejected (JS allows); default-param
   type mismatch crashes the C++ compile instead of a CP3xxx diagnostic;
   `const z;` without initializer accepted (JS is a syntax error); malformed
   numeric literal `1.2.3` yields off-target CP3202/CP3207 cascade.
7. **Zero-arg `.then` handler does not compile** - `g().then(() => …)` fails in
   `HandlerResult` (`invoke_result_t<F, T>`); JS allows ignoring the value.
   Wave 1 (Part C).
8. **`NaN` / `Infinity` not resolvable** - `console.log(NaN)` → CP3301 while D9
   pins NaN's rendering. Ruled in **D63**; fixed in wave 1.
9. **Std-builtin signatures unenforced in `check`** - `Math.abs("x")`,
   `Math.round()` (arity), `JSON.parse<number>(5)`, `parseInt(5)`,
   `parseFloat(5)`, `String([1])` all pass. Enforcement lands with wave 1; the
   check-mode cases land afterwards (growth pass dropped them verify-first).
10. **Wave-4 leftovers** - `async` arrows (D40; home: wave 12 arrows cluster);
    `Promise.resolve/reject` (D40; home: wave 1 Part C);
    `async/promise_all_length|promise_all_values|promise_all_preserves_order|
    allsettled_length` stay skipped pending wave 3 arrays.
11. Marker mechanics - see D58.

## Wave 4 landing record

Async/await + event loop + timers landed: 54 flips (1 failed→passed, 53
skipped→passed), 0 regressions; design, rulings and limitations in
`build/scratch/async/WAVE4-REPORT.md` (§9 rulings ratified as **D62**; §10
limitations documented, not corpus-asserted). Durable facts:

- Runtime TUs linked by the CLI driver: `runtime.cpp`, `error.cpp`,
  `eventloop.cpp`, `promise.cpp`, `console.cpp` (MinGW g++ 16, `-std=c++20`,
  no extra libs); `parallel.cpp`/`worker.cpp` deliberately excluded (CP3002).
  Wave 1 must extend this `kRequired` list when it adds runtime TUs.
- Codegen always appends a trailing `co_return` to coroutine bodies (a body
  with no `co_return` silently swallows a `throw`).
- `.catch(h)` lowering: `catchError(h)`; codegen preserves the frozen ABI.
- Known limitations: D38 `return`-in-`finally`; throw-in-`finally` while
  unwinding terminates; top-level-await wrapper frame destroyed at completion
  (a late timer reading wrapper locals would dangle - not hit by the corpus).
- Lead probes post-dating the wave: settled `await` resumes inline (D62.1
  deviation, fix queued for wave 1), zero-arg `.then` handler fails to compile
  (bug 7 above).

## Wave 1 landing record

Committed **d409e33** (source). Part A: builtin method/member codegen +
header-only runtime - `Math` 48 flips, strings 53, `path` 6, `os` 7,
`JSON.stringify` scalars 8, `process.exit/argv/env.get` 4, `String(x)` 3,
module imports 4, `truthy_nan` 1. Part B: six reds green (var hoisting,
module-level `let`/`const` globals, statically-nullish short-circuit,
default-param `undefined`, `Maybe<T>` keeps `null`/`undefined` distinct,
settled-await microtask FIFO per D62.1). Part C: `Promise.resolve/reject`
statics, zero-arg `.then()` handler, settled-await deferral. Runtime additions
header-only; required runtime TU set unchanged. Lead verification: canonical
rebuild, six reds + category sample fresh-run green, lint 157 files/0 errors,
runtime units 82/82. Report + evidence: `build/scratch/methods/`. Known
limitations (report §9): ASCII-only case mapping, slice/substring/charAt
clamp out-of-range, `JSON.stringify` scalars-only, `[…].includes` literal-set
stopgap, minimal `process.argv()`; fs/env, `JSON.parse`, Date/Buffer/crypto,
arrays/objects, Map/Set remain with their waves.

## Perf wave (in flight - launched after d409e33) - performance

Lead probe, 2026-10-17 (`build/scratch/perf-probe/`, g++ 16.1.0 `-O2`): the
compute-single loop lowered three ways - `std::fmod(n, 2)` **1080 ms** (= cp3c's
current shape; reproduces compute-single's measured 1.13-1.18 s, so fmod is
essentially the whole sink), inline cast-based `%` in the exact double domain
**385 ms**, int64 **135 ms** ≈ C++/Rust entries (147/132 ms). Not flags
(`toolchain.cpp` already passes `-O2`), not startup (hello-world exe 6-8 ms =
blank-process floor; the prelude is DCE'd), not the simple statements (the rest
of the loop is fine double code). Targets and binding rules: **D64**.

Ordered work items (sole writer of `src/sema/**`, `src/codegen/**`,
`src/runtime/**`, `src/cli/src/util.cpp`; acceptance baseline: 657 passed /
0 failed):
1. `%` lowering: inline helper + literal-divisor specialization; `std::fmod`
   only as out-of-range fallback. No observable change; corpus stays
   byte-identical. Expected compute-single ≈ 0.39 s (≈3x faster than Node).
2. Integral fast path: static integrality/range analysis → guarded int64
   lowering. Expected ≈ 0.15 s - beats Go (0.20 s), at C++/Rust parity.
3. Hygiene: string literals without `std::string(...)` wrap, usage-pruned
   prelude, runtime-TU pruning with D56 (cold hello-world build is 5.2 s
   because all five TUs compile every time).
4. After landing: re-run the suite, refresh the benchmark tables and the
   root README numbers with the new labeled run (D60/D64).
5. Post-landing priority (user directive, 2026-10-17): finish the remaining
   product surface first - CLI/init/package manager (lane C1, in flight), then
   std/crypto/network/parallel and the rest of the wave list - before the next
   perf push; perf then continues as an ongoing loop judged by the benchmarks.

Acceptance: corpus 0 failed + suite rerun meets the D64 targets.

## Native type systems wave (queued - spec §3)

`docs/language-spec/03-type-system.md`: TS type names stay the default;
`package.json` may set `"typeSystem": "rust" | "cpp"` so plain names (`u32`,
`i64`, `long long`) are native machine types; the non-active system stays
reachable inline by prefix (`r_'u32'`, `c_'long long'`, `ts_'number'`);
numeric literals and the `null`/`undefined` keywords never change; mixed
integer types follow C++ implicit widening, with Rust-style `strictNumericCasts`
as an opt-in flag. Scope: parser (prefixed quoted type names), sema
(project-level name mapping + native type metadata), codegen (type_map binding
to int32/int64/uint64/double/…), `cp3 init`'s fourth question, docs.

Relationship to performance (**D64**/**D65**): this is the explicit fast path -
declared native types are C++/Rust-equal by construction; D64's integral fast
path makes the TS-typed default competitive without learning types. Keep both.

Competitions (**D60**/**D65**): the cp3 entry stays TS-typed; when this wave
lands, add a second clearly-labeled cp3 entry (`compute-single` with `r_'u64'`)
that auto-fills like any pending entry.

Scheduling: queued behind the perf wave and wave 2 (classes); mid-size wave
(parser + sema + codegen + cli). Move up on request.

## Corpus weight at M2 open (runnable = 843)

stdlib 190 (16 passed, 173 skipped) · types 86 (66) · arrays-strings 82 (13) ·
operators 74 (54) · control-flow 48 (17) · syntax 46 (46 - complete) ·
classes 46 (1) · functions-closures 46 (14) · variables 42 (27) ·
modules 41 (26) · generics 32 (2) · async 22 (0) · concurrency 22 (8) ·
enums 25 (0) · errors 19 (0) · memory-weak 18 (2) · cli 4 (3).

Growth since wave 4 (current, pre-wave-1): `arrays-strings/` +74 (35 pass /
39 skip) and `stdlib/` +68 (14 pass / 54 skip); core-semantics +141 (115 pass /
21 skip / 5 fail) across variables, operators, control-flow,
functions-closures, syntax, types; promotions (above). Running total:
**1236 found - 516 passed / 6 failed / 606 skipped / 108 ambiguous** (the six
reds are §5).

Post-wave-1 (d409e33): **1236 found - 657 passed / 0 failed / 471 skipped /
108 ambiguous** (+141 flips - 6 failed→passed, 135 skipped→passed, 0
regressions; evidence: `build/scratch/methods/WAVE1-REPORT.md`).
