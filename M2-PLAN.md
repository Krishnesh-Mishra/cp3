# M2 plan — living roadmap

Opened 2026-10-17 at M1 close: corpus **295 passed / 1 failed / 547 skipped /
110 ambiguous** (953 found) at commit `075bc20`. Source: the read-only M2
reconnaissance pass (all 547 skips attributed to their first blocking feature).
**This file is a roadmap, not a contract** — `DECISIONS.md` wins on any
conflict. Waves update it as they land.

## Critical path to the pending examples

`method calls (wave 1) + arrays (wave 3)` → `async/event loop (wave 4, in
flight)` → `collections (wave 5)` → `std/network` → all 6 pending
`examples/**` flip to READY (`parallel-sum` additionally needs wave 3 +
`parallel.*`; the two games need waves 1+4).

## Ranked waves (cases-unblocked × dependency-criticality)

| # | Wave | First-blocker cases (latent) | Key dependency |
|---|---|---:|---|
| 1 | Method-call codegen + per-receiver runtime (string, array, Math, Date/Buffer, JSON, fs/path/os/process, crypto/env) | 132 (223) | arrays for array methods; classes for Date/Buffer/Map receivers |
| 2 | Classes + object model (§15, incl. generic classes, `memory-weak`) | 63 (68) | sema `declarations.cpp` wall first, then codegen |
| 3 | Array literals + indexing + `.length` (§09) | 52 + 19 | prerequisite for `parallel.for` |
| 4 | **Async/await + event loop + timers (§14)** — IN FLIGHT | ~35 | flips the only failing case; unblocks workers + network |
| 5 | Map/Set/EventEmitter (§20, insertion order pinned by D44) | 32 | wave 2 |
| 6 | Enums (§04) — self-contained | 27 | — |
| 7 | Try/throw/finally (§07) | 26 | — |
| 8 | Non-primitive locals/returns (unions, closures) (§03/§10) | 37 (~60) | shares machinery with 2/3 |
| 9 | Object literals + structural interfaces | 29 | — |
| 10 | Generic functions (§16) | 18 | wave 2 for the 5 class-mixed cases |
| 11 | Module local function linkage (codegen emits local functions around calls) | 6 (14) | closes the remaining `modules/` skips |
| 12 | Grammar tail sweep (switch 7, untyped params 7, arrows 6, compound assign 6, rest 5, bitwise 5, destructuring 5, for-of 4, console methods 4, labels 3, `parseInt` 3, `parseFloat` 2, `typeof` 2, …) | ~95 | mostly independent, S each |

## Marker rulings owed (the 18 genuinely open)

`operators/eq_fixed_mismatch` (spec §3 vs §5 contradiction) ·
`types/type_alias_keyword` · `arrays-strings/{array_mixed_infer,
str_length_utf8}` · `enums/{autonumber_value, numeric_value_via_index}` ·
`control-flow/{dense_array_holes, forin_array_indices,
spread_number_array_into_fixed_params}` ·
`concurrency/{parallel_capture_diagnostic_text,
parallel_run_mixed_result_types}` · `errors/{catch_binding_scope,
uncaught_throw_behavior}` · `memory-weak/{weak_literal_wrapper_typing,
weak_modifier_optional_type, weak_on_value_type}` ·
`stdlib/json_parse_untyped_typeof_narrow` · `async/microtask_macrotask_order`.

Rule each marker when its wave starts (context at hand); promotion per D58.
The other ~53 markers are already answered by binding decisions (D15–D49) and
are promotable into real cases; ~23 are CLI/tooling markers (not `.cp3` cases);
16 are recorded as deferred.

## Open bugs / hygiene (evidence cited; fix with the wave that touches the area)

1. **CP3504 false positive** — `if / else if / else` where every branch
   returns is reported as "may fall off the end of its body"
   (`src/sema/src/stmt_check.cpp:443`; documented with a workaround in
   `examples/README.md` and `examples/basics/fizzbuzz/main.cp3`).
2. **CP3301 initializer self-reference** — `let id = setInterval(() => {
   clearInterval(id); }, 1)` cannot find `id`
   (`tests/cases/stdlib/timer_setinterval_clear.cp3:3`; the binding is typed
   after its initializer registers it). Kills that case *before* `await` does.
3. **Broken marker fixtures** (repair at promotion): `modules/_ambiguous/
   relative_specifier_form` (imports an absent `base.cp3`),
   `concurrency/_ambiguous/worker_call_shape` (imports an absent `x.cp3`),
   `modules/_ambiguous/prelude_strong_weak_order` (ships no `_prelude*.cp3`).
4. **Passing-but-stub-blocked audit** — `concurrency/parallel_field_write` and
   `generics/decorator_banned` pass on a real diagnostic but also carry a
   `CP3002: class declarations`; re-audit when wave 2 lands.
5. Marker mechanics — see D58.

## Corpus weight at M2 open (runnable = 843)

stdlib 190 (16 passed, 173 skipped) · types 86 (66) · arrays-strings 82 (13) ·
operators 74 (54) · control-flow 48 (17) · syntax 46 (46 — complete) ·
classes 46 (1) · functions-closures 46 (14) · variables 42 (27) ·
modules 41 (26) · generics 32 (2) · async 22 (0) · concurrency 22 (8) ·
enums 25 (0) · errors 19 (0) · memory-weak 18 (2) · cli 4 (3).
