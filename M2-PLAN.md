# M2 plan — living roadmap

Opened 2026-10-17 at M1 close: corpus **295 passed / 1 failed / 547 skipped /
110 ambiguous** (953 found) at commit `075bc20`. Source: the read-only M2
reconnaissance pass (all 547 skips attributed to their first blocking feature).
**This file is a roadmap, not a contract** — `DECISIONS.md` wins on any
conflict. Waves update it as they land.

**Status after wave 4** (async/await + event loop + timers — landed; evidence:
`build/scratch/async/WAVE4-REPORT.md`): **349 passed / 0 failed / 494 skipped /
110 ambiguous** (953 found); 54 flips (1 failed→passed, 53 skipped→passed),
0 regressions; runtime unit tests 80/80 checks green; lint 0 errors / 42 soft
warnings. Corpus growth since: **+142** (`arrays-strings/` +74, `stdlib/` +68;
both binaries agree — arrays-strings 47/0/109/10) and **+141** core-semantics
(`variables`, `operators`, `control-flow`, `functions-closures`, `syntax`,
`types`; 115 pass / 21 skip / **5 spec-pinned failures**). Promotions per D58:
`async/microtask_macrotask_order` → real passing case; `str_length_utf8`
marker retired (D63). Running total (pre-wave-1): **1236 found — 516 passed /
6 failed / 606 skipped / 108 ambiguous**; all 6 reds are wave-1 targets (§5–6).

## Critical path (revised — see D59/D60)

Examples are showcase-only now (D59): no auto-run, no statuses — the corpus in
`tests/cases/` is the only automated oracle. The wave order below is the plan;
`examples/**` and the new cross-language `examples/competitions/` suite (D60:
health-server + compute benchmarks) become fully runnable as their features
land (servers wait on `std/network`; multi-thread entries on `parallel.*`; the
games on wave 1 — wave 4 already landed).

## Ranked waves (cases-unblocked × dependency-criticality)

| # | Wave | First-blocker cases (latent) | Key dependency |
|---|---|---:|---|
| 1 | Method-call codegen + per-receiver runtime (string, array, Math, Date/Buffer, JSON, fs/path/os/process, crypto/env) — **IN FLIGHT** | 132 (223) | arrays for array methods; classes for Date/Buffer/Map receivers |
| 2 | Classes + object model (§15, incl. generic classes, `memory-weak`) | 63 (68) | sema `declarations.cpp` wall first, then codegen |
| 3 | Array literals + indexing + array `.length` (§09) | 52 + 19 | prerequisite for `parallel.for` |
| 4 | **Async/await + event loop + timers (§14)** — **LANDED** (D62; 54 flips, 0 regressions) | ~35 | flips the only failing case; unblocks workers + network |
| 5 | Map/Set/EventEmitter (§20, insertion order pinned by D44) | 32 | wave 2 |
| 6 | Enums (§04) — self-contained | 27 | — |
| 7 | Try/throw/finally (§07) | 26 | — |
| 8 | Non-primitive locals/returns (unions, closures) (§03/§10) | 37 (~60) | shares machinery with 2/3 |
| 9 | Object literals + structural interfaces | 29 | — |
| 10 | Generic functions (§16) | 18 | wave 2 for the 5 class-mixed cases |
| 11 | Module local function linkage (codegen emits local functions around calls) | 6 (14) | closes the remaining `modules/` skips |
| 12 | Grammar tail sweep (switch 7, untyped params 7, arrows 6, compound assign 6, rest 5, bitwise 5, destructuring 5, for-of 4, console methods 4, labels 3, `parseInt` 3, `parseFloat` 2, `typeof` 2, …) | ~95 | mostly independent, S each |

Parallel lanes: the **D61 CLI lane** (exe → `.cp3/out/`, `clean` semantics;
`src/cli` + `tests/cases/cli` only) runs alongside wave 1; the **D56
runtime-embedding lane** (single-binary guarantee) is queued behind wave 1 so
the embedded runtime TU set finalizes once.

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
retired — covered by `arrays-strings/str_length_accent_bytes`, which flips with
wave 1). Rule each remaining marker when its wave starts (context at hand);
promotion per D58. The other ~53 markers are already answered by binding
decisions (D15–D49) and are promotable into real cases; ~23 are CLI/tooling
markers (not `.cp3` cases); 16 are recorded as deferred.

### Candidate ambiguities parked by corpus growth (rule when the wave lands)

From the stdlib/arrays-strings growth pass (evidence in its report; no cases
written — verify-first rule):

1. `array_extra_methods` — §9's array list omits JS mutators `shift`/`unshift`/
   `reverse`/`splice` (+ `forEach`): in the language or not?
2. `array_index_out_of_bounds` — read (undefined vs panic) and write behavior.
3. `array_index_negative` — JS wrap-from-end vs `std::vector` UB.
4. `array_length_write` — assigning array `.length` (JS truncate/extend).
5. `str_charat_out_of_range` — `charAt(i)` past the end (JS returns `""`).
6. `str_index_assign` — `str[i] = …` (sema currently reports CP3401).
7. `parsefloat_invalid` — `parseFloat("abc")` / `parseInt("abc")` → NaN vs 0.
8. `math_sign` — `Math.sign` absent from the spec's Math signature list.
9. `json_stringify_undefined` — `JSON.stringify(undefined)` vs the `string`
   return signature.

From the core-semantics growth pass:

10. `let_no_init_then_assign` — static type of an unannotated, uninitialized
    `let` (currently inferred `undefined`; later assignment rejected).
11. `function_expression` — are `let f = function(): void { … };` expressions
    in the language? (parser rejects today)
12. `iife` — parenthesized immediately-invoked function forms.
13. `nested_function_declaration` — nested declarations: sema accepts, codegen
    CP3002 today.
14. `empty_statement` — bare `;;;` legality/execution.
15. `narrowing_by_null_check` — does `x !== null` narrow `number | null`?
16. `console_log_multiarg` — variadic `console.log('a', 'b')` formatting
    (space-joined today; no doc pins it).

## Open bugs / hygiene (evidence cited; fix with the wave that touches the area)

1. **CP3504 false positive** — `if / else if / else` where every branch
   returns is reported as "may fall off the end of its body"
   (`src/sema/src/stmt_check.cpp:443`; documented with a workaround in
   `examples/README.md` and `examples/basics/fizzbuzz/main.cp3`). Still open
   after wave 4; fix with waves 1/2.
2. ~~**CP3301 initializer self-reference**~~ — **fixed in wave 4**
   (`src/sema/src/declarations.cpp`; `stdlib/timer_setinterval_clear` passes).
3. **Broken marker fixtures** (repair at promotion): `modules/_ambiguous/
   relative_specifier_form` (imports an absent `base.cp3`),
   `concurrency/_ambiguous/worker_call_shape` (imports an absent `x.cp3`),
   `modules/_ambiguous/prelude_strong_weak_order` (ships no `_prelude*.cp3`).
4. **Passing-but-stub-blocked audit** — `concurrency/parallel_field_write` and
   `generics/decorator_banned` pass on a real diagnostic but also carry a
   `CP3002: class declarations`; re-audit when wave 2 lands.
5. **Six red corpus cases — wave-1 targets (spec-pinned bug fuel):**
   - `variables/var_hoist_use_before_declaration` — `var` must hoist
     (`undefined` read before assignment; JS/§8 parity); currently CP3301.
   - `variables/module_let_read_from_function` — module-level `let` read from a
     function body emits raw C++ (`'g' was not declared in this scope`);
     expected `5`.
   - `operators/nullish_chain_literals` — `null ?? undefined ?? 5` prints
     `null`; JS skips both → expected `5`.
   - `functions-closures/default_param_undefined_arg` — `f(undefined)` must
     trigger the default (`7`); currently CP3401.
   - `types/nullable_three_way_null_identity` — `number | null | undefined`
     conflates null/undefined under `===`.
   - `async/await_settled_microtask_fifo` — a settled `await` must defer to the
     microtask queue (D62.1, JS parity); current runtime resumes inline.
6. **Suspected bugs S1–S6 (no cases yet; dropped verify-first by the growth
   pass; wave 1):** module-`let` write side also emits raw C++; `var` is not
   function-scoped; `var` redeclaration rejected (JS allows); default-param
   type mismatch crashes the C++ compile instead of a CP3xxx diagnostic;
   `const z;` without initializer accepted (JS is a syntax error); malformed
   numeric literal `1.2.3` yields off-target CP3202/CP3207 cascade.
7. **Zero-arg `.then` handler does not compile** — `g().then(() => …)` fails in
   `HandlerResult` (`invoke_result_t<F, T>`); JS allows ignoring the value.
   Wave 1 (Part C).
8. **`NaN` / `Infinity` not resolvable** — `console.log(NaN)` → CP3301 while D9
   pins NaN's rendering. Ruled in **D63**; fixed in wave 1.
9. **Std-builtin signatures unenforced in `check`** — `Math.abs("x")`,
   `Math.round()` (arity), `JSON.parse<number>(5)`, `parseInt(5)`,
   `parseFloat(5)`, `String([1])` all pass. Enforcement lands with wave 1; the
   check-mode cases land afterwards (growth pass dropped them verify-first).
10. **Wave-4 leftovers** — `async` arrows (D40; home: wave 12 arrows cluster);
    `Promise.resolve/reject` (D40; home: wave 1 Part C);
    `async/promise_all_length|promise_all_values|promise_all_preserves_order|
    allsettled_length` stay skipped pending wave 3 arrays.
11. Marker mechanics — see D58.

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
  (a late timer reading wrapper locals would dangle — not hit by the corpus).
- Lead probes post-dating the wave: settled `await` resumes inline (D62.1
  deviation, fix queued for wave 1), zero-arg `.then` handler fails to compile
  (bug 7 above).

## Corpus weight at M2 open (runnable = 843)

stdlib 190 (16 passed, 173 skipped) · types 86 (66) · arrays-strings 82 (13) ·
operators 74 (54) · control-flow 48 (17) · syntax 46 (46 — complete) ·
classes 46 (1) · functions-closures 46 (14) · variables 42 (27) ·
modules 41 (26) · generics 32 (2) · async 22 (0) · concurrency 22 (8) ·
enums 25 (0) · errors 19 (0) · memory-weak 18 (2) · cli 4 (3).

Growth since wave 4 (current, pre-wave-1): `arrays-strings/` +74 (35 pass /
39 skip) and `stdlib/` +68 (14 pass / 54 skip); core-semantics +141 (115 pass /
21 skip / 5 fail) across variables, operators, control-flow,
functions-closures, syntax, types; promotions (above). Running total:
**1236 found — 516 passed / 6 failed / 606 skipped / 108 ambiguous** (the six
reds are §5).
