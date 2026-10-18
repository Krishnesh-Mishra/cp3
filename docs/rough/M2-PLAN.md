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

Parallel lanes: the **C1 CLI + package-manager lane** landed 2026-10-18 (record
below): D61 exe → `.cp3/out/` layout + `clean`, `cp3 init` spec parity,
`.cp3pak` pack/install + `.cp3/lock.json`; scope was `src/cli/**` minus
`src/cli/src/util.cpp` (perf-owned). The **D56
runtime-embedding lane** (single-binary guarantee) is queued behind the perf
wave so the embedded runtime TU set finalizes once. The **benchmark expansion
(B1)** landed 2026-10-18 (record below; Tier-1 set, ratio/RSS/size columns);
the **D67 tiered gate + auto-run** (B1b) landed 2026-10-18 (`905acf6`; 10 ms
noise floor so hello-world spawn jitter cannot flap the gate). The **source-comment
dash sweep** (D66.4 leftovers): part 1 landed (`dbc1c96`): everything the perf
wave does not own, minus the two `tests/runner/run_tests.py` fixes that ride
with the B1b landing; part 2 (src/sema, src/codegen, src/runtime,
`src/cli/src/util.cpp`, the `tests/cases/README.md` prose) is queued behind the
perf wave. The **source-tree structure sweep** (D66.5/D68; behavior-neutral, corpus
byte-identical) landed 2026-10-18 (`173e762`, wave S2; record below) ahead of
the post-perf std waves, so the new module families land in the final layout
as runtime units behind per-module tables. The spec-named native backends
(libsodium for crypto; libuv + uWebSockets for network; oneTBB for
`parallel.*`) are vendored as pinned static builds packed with cp3c (D70,
landed `b7c8883`; record below), so the std waves link `third_party/`
directly. Post-perf runtime waves: the roadmap was revised by the user
directive of 2026-10-18 (latest): (1) the **structure sweep** landed
(2026-10-18); (2) the **three parallel implementation waves** run next, each
with its own dedicated agent, because
server alone is a big topic: **server** (`std/network`: fetch, createServer,
WebSocket; lights the health-server row), **crypto** (`std/crypto`), and the
**JS-compat typed arrays** (D69 + `docs/rough/JSC-TYPED-ARRAYS.md`;
corpus-first, needs the array indexing path); each agent skips whatever is
already done and reports missing vs landed per path ownership; the parallel
`worker` wave and the Date/Buffer/fs/env leftovers follow the server wave;
(3) the lead then runs a full verification audit ("nothing missing") over the
merged result; (4) the performance program resumes (string-churn first, then
every flagged row, across server, crypto and codegen). Backing storage stays
fixed-size (an owned heap block + stored length for runtime sizes; the native
fixed-array mapping when sizes are compile-time known), never a resizing
vector: typed arrays have no delete and no resize, and `const` keeps its JS
meaning (no rebinding; contents mutable). The reviewed typed-array surface and
the five resolved open questions are **D69** plus `docs/rough/JSC-TYPED-ARRAYS.md`.
All of these sit in perf-owned paths today.

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
12. **parallel.* / worker lowering wave requirements (D71 clarification)** -
    when the lowering wave lands, an `async` closure or an `await` inside a
    closure passed to `parallel.for`/`reduce`/`run` must become a clean
    compile-time diagnostic, never a silently broken program: pool threads are
    loopless and `parallel.*` is synchronous by contract (spec §13 Rule 1), so
    an async closure breaks the borrow-safety foundation and its `await`s
    would have no loop to drive them. `worker` keeps its own-thread single loop
    and must hand results back with `postFromThread` (contract already written
    in `cp3/parallel/worker.hpp`). Add the corpus cases with that wave:
    async-closure-into-parallel rejection, worker cross-thread result, worker
    async body awaiting on its own loop.

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

## C1 landing record (2026-10-18)

Committed `4556017` (src/cli only; `src/cli/src/util.cpp` untouched). Scope:

- D61 artifact layout verified end to end (`build` -> `.cp3/build/`
  intermediates, exe at `.cp3/out/<entry>.exe`, `clean` / `clean --all`
  semantics, exit codes 0/1/2/3, diagnostic machine line byte-stable).
- `init` spec parity: `src/index.cp3`, full package.json (dev/build/start/check
  scripts + build block), package flavor with a spec-shaped export entry +
  README, `--yes` non-interactive, `--type application|package` flag added for
  scripted use (report deviation).
- Package manager core: `pack` -> `.cp3/out/<name>.cp3pak` (own DEFLATE/gzip/
  tar/SHA-256/CRC-32; deterministic; bsdtar interop verified both directions);
  `install` (local `.cp3pak`, unpacked dir, `file:` deps) -> `.cp3/packages/` +
  `.cp3/lock.json` (`sha256:`/`dir-sha256:` integrity, `engines` warnings,
  reinstall overwrites). Lead re-verified: pack + install round trip, archive
  listing, lock integrity == Get-FileHash.
- Scripts: `pre<name>`/`post<name>` hooks for run/build/check/dev/fmt/clean/
  install with a `CP3_SCRIPT_GUARD` re-entry guard; four scenarios smoke-tested.
- Gates: `--filter cli` 27 found / 4 passed / 0 failed / 23 ambiguous; lint
  0 errors. Report: `build/scratch/cli/WAVE-C1-REPORT.md`.
- Deferred with named errors (exit 3, per-dep failure): GitHub repo installs,
  registry-style specs, `build.excludeSource`, `build.cacheBinary` (warning
  only); `exec` stays a stub until `bin` resolution lands.
- Blocker outside the lane: the spec-shaped package scaffold
  (`export default { add }`) hits codegen's missing `ObjectLiteral` (CP3002)
  until the classes/object-literal waves land; the pack pipeline itself is
  proven with `export default add;` (lead reproduced both).

## B1 landing record (2026-10-18)

Committed `f23e728` (104 files): suite moved to the repo root `benchmarks/`
(D66.1) and overhauled per D66.2/D66.3. Scope:

- Single entry point `benchmarks/run_benchmark.py` forwarding to the engine
  `bench/run_suite.py` (`--only/--reps/--cp3c/--strict/--results`, plus
  `--hello-reps`, `--churn-scale`).
- Columns: per-row cp3/cpp + cp3/rust time ratios, `peak_rss_mb` (Windows
  `PeakWorkingSetSize`, honest `-` elsewhere), stripped `binary_kb`; summary
  table with a geomean footer; 1.2x regression tripwire prints WARN (D66.2)
  and `--strict` exits 1.
- Six Tier-1 tasks authored for all five runnable languages
  (compute-single-int, object-churn, array-growth, closure-capture,
  interface-dispatch, generics-mono); cp3 entries are spec-written and their
  pending rows show the compiler's own CP3xxx diagnostic verbatim, filling in
  when the waves land. hello-world (best + median of 15+), string-churn
  (default + stress), word-frequency finished; independent oracle
  `bench/oracle.py` derives every embedded expected value.
- Harness: `tests/runner/run_tests.py --bench` / `--bench-args` (additive, off
  by default; `--json` skips with a note). Lead-smoked end to end.
- Labeled runs committed: `bench/results/2026-10-18T0705-local` (12 tasks,
  every runnable row oracle-matched; hello 0.73x cpp / 1.00x rust,
  string-churn 1.07x cpp, compute-single 7.59x / 8.60x pre-perf-wave; 3
  designed WARNs; geomean 1.80x cpp / 3.99x rust) and
  `2026-10-18T0712-local-stress` (churn 300k: 1.04x cpp / 10.90x rust). One
  compiler binary pinned by SHA-256 in both `meta.txt`.
- Fairness fixes: the cpp/go string-churn entries no longer compare locale or
  library tables. Docs: `benchmarks/README.md` (methodology, fairness
  contract, gate, refresh policy), root README tables from the same run,
  `examples/README.md` pointer, per-task READMEs, `benchmarks/BENCH-REPORT.md`
  (audit, Tier-1 pending diagnostics, Tier-2 staging, deviations).
- Refreshed tables land after the perf wave: rerun, paste verbatim, never
  hand-edit (refresh policy in `benchmarks/README.md`).

## P1 landing record (2026-10-18)

Committed `5c88ead` (14 files; `src/codegen` + `src/runtime` only). Scope: the
three D64 deliverables.

- `%` lowering without libm: `cp3/arith.hpp` (`mod`, `modConst`, guard
  helpers); `std::fmod` owns every edge (negatives, zero divisor,
  `|x| >= 2^53`, `-0.0`, NaN, infinities, fractions, `x % 0` signs). Codegen
  emits `modConst<K>` for literal divisors.
- Guarded int64 `number` fast path: functions that lower cleanly in integer
  mode get an extra body plus a wrapper that bails to the always-correct
  double body (`int_mode.cpp`, `int_reads.*`).
- compute-single: 182 ms entry (1.22x cpp / 1.46x rust in-suite) to 150 ms
  best (1.08x cpp / 1.21x rust on a quiet machine; 1.17x / 1.38x loaded);
  Go 192-200 ms, so the D64 floor (<= 0.20 s) holds with margin. The hot loop
  disassembly has no `call`, no libm and no division (report section 4).
- Report: `build/scratch/perf/WAVE-P1-REPORT.md`. Lead acceptance: canonical
  rebuild `build/bin/cp3c.exe` SHA-256
  `C051967809915DAE897618FC9F5CC0D5C4516329E991DC3C190DE9CC37257432`; cold
  corpus (caches deleted first) `1236 found / 657 passed / 0 failed / 471
  skipped / 108 ambiguous`, exit 0; `arith_test` 502170 checks, 0 failures;
  layout lint 0 errors.
- Deferred hygiene (report section 7): the string-literal `std::string(...)`
  wrap removal and the D56 prelude/TU pruning ride the D56 runtime-embedding
  lane.
- Scorecard: the post-P1 labeled suite refresh follows D67.4; the flagged rows
  it prints are the standing todo queue (string-churn first).

## VEND landing record (2026-10-18)

Committed `b7c8883` (306 files: `third_party/**`, `.gitignore`, D70). All
four spec-named backends vendored as pinned, static MinGW-W64 x64 builds:
libsodium 1.0.22 (crypto), libuv v1.53.0 + uWebSockets v20.80.0 including its
in-tree uSockets (network), oneTBB v2023.1.0 (`parallel.*`). Smoke matrix
15/15 across dynamic, static-libgcc/stdc++ and fully static links; static exes
re-run with a scrubbed PATH. `third_party/MANIFEST.md` carries pins, exact
build commands, artifact SHA-256s and the verified consumer flag lists;
`third_party/build.ps1` reproduces every artifact from the pinned source.
Lead re-verification: all six archive SHA-256s recomputed (match); the uWS and
sodium smokes re-linked and re-run from a clean directory against
`third_party/` only (exit 0). Caveats: do not gate AES-256-GCM on
`is_available()` (returns 0 on this build while the round-trip works); oneTBB
static is upstream-unsupported but builds and passes; TLS/compression off by
design (SS20). Report: `build/scratch/vend/WAVE-VEND-REPORT.md`.

## S2 structure sweep landing record (2026-10-18)

Committed `173e762` (169 files). 119 files moved into concern
subdirectories: codegen `emit/` + `emit/std/` (emit_builtin split into 7
per-family units + `detail/builtin_tables.h`), `support/`, `detail/`; runtime
`include/cp3/{core,async,parallel,std}/` with mirroring `src/`
subdirectories; sema `checks/support/detail`; cli
`commands/pkg/archive/toolchain/detail`; modules `support/detail`;
parser/lexer `detail/`. The one behavior-affecting change:
`runtimeSourcePaths()` walks recursively (same 5 TUs, same order). Dash
roundup part 2: 135 comment dashes in `src/` plus 6 in
`tests/cases/README.md`; the deliberate generated-banner string stays.
STYLE.md section 8 refreshed to the delivered inventory. Lead acceptance:
canonical rebuild exit 0; cold corpus 1236/657/0/471/108 exit 0; layout lint
195 files 0 errors 53 warnings; independent dash grep and path spot checks.
Two stale boundary prose refs intentionally left (`cp3/async/eventloop.hpp`,
`cp3/lexer.hpp` comments) and queued for the next touch of those files.
Report: `build/scratch/sweep2/WAVE-SWEEP-REPORT.md`.

## D68 native wiring landing record (2026-10-18)

Committed `fb64ee8` (28 files). The std-module native backends are wired end
to end ahead of the three feature waves: `src/cli/src/detail/native_packages.hpp`
declares one row per std module with a vendored backend (include dirs, compile
flags, runtime TUs, `-L` directories, `-l` archives), the rows live
per-module in `native_std_<family>.hpp` (crypto: libsodium; network: uSockets
+ libuv + the six Windows system libraries each archive needs), and
`cmdBuildOrRun` selects rows from the module graph's resolved `std/...`
specifiers (`ModuleDependency`, any unit), so a program that imports neither
module compiles the same 5 runtime TUs and links no third-party archive.
`compileArgs`/`linkArgs` moved onto `CompileStep`/`LinkStep` and the build
entry point onto `BuildRequest` (layout-lint budgets: 0 errors). Extension
stubs that compile today and keep the previous CP3002 behavior landed for
both modules: runtime TU + header per module (runtime CMake source entries +
third_party include roots added), sema `std_check` dispatch into
`checks/std_crypto.cpp` / `std_network.cpp`, codegen `emit/std/crypto.cpp` /
`network.cpp` with `needsCrypto_`/`needsNetwork_` include wiring. Lead
acceptance: canonical rebuild exit 0; layout lint 207 files 0 errors 52
warnings; cold corpus 1236/657/0/471/108 exit 0 twice (before and after the
toolchain refactor); crypto/network scratch programs build with their native
flags, link `-lsodium` / `-luv -luSockets`, run and exit 0; a non-importing
program links neither. The three parallel feature waves (crypto, network,
typed arrays) fill the stubs; each reports in
`build/scratch/<wave>/WAVE-<WAVE>-REPORT.md`.

## crypto wave landing record (2026-10-18)

Committed `ff5dbbc` (40 files). `std/crypto` is real on the vendored libsodium
backend: all six documented members (docs 04 section crypto) implemented and
lowered, `randomUUID` (RFC 4122 v4, 36 chars), `randomBytes`,
`sha256`/`sha512` (lowercase hex), `AES256`/`AES256Decrypt` (AES-256-GCM,
base64 envelope of nonce (12) || ciphertext || tag (16), BLAKE2b-256 key
stretch, decrypt throws a catchable `Thrown` on tamper or wrong key, never
silently-wrong plaintext; AES deliberately not gated on
`crypto_aead_aes256gcm_is_available()`). `randomBytes` returns the raw-byte
`std::string` stand-in: the docs type it `Buffer` and beta one has no nominal
Buffer type yet (the pinned `.length` case is exactly the byte count; feeding
the value back into sha256/sha512 works). Sema types the surface and codegen
lowers onto `::cp3::runtime::crypto` (`needsCrypto_`), unknown members stay
CP3002. 18 new focused cases (15 run, 3 check) plus the 7 pinned run-mode
cases flipped. Lead verification: canonical rebuild exit 0, lint 207 files
0 errors 52 warnings, spec surface re-checked against docs 04, cold focused
`--filter crypto` 25/25 passed 0 failed, no dashes, no shared-file edits.
Report: `build/scratch/crypto/WAVE-CRYPTO-REPORT.md`.

## network wave landing record (2026-10-18)

Committed `44a7c37` (one commit with the typed-arrays wave). `std/network` is
real on libuv + uWS (D73): one event loop per thread, never a second loop; the
request-isolation contract (a throwing handler is a 500 for that request only,
the next request still answers); the fetch transport (one connection per
request, buffered body, per-request timeout, clear `https://`/`wss://`
rejection with TLS off per D70); WebSocket with per-server stable `WebSocket&`
storage, the hand-rolled RFC6455 client (masking from `mt19937_64`) and close
slots. The contract header carries no uWS/libuv types, and every handle method
is `const` so the documented spellings compile. Sema models the documented
surface (marker object types, named-import inference, the `network.x` namespace
corner); codegen lowers members and calls with handler-typed lambdas; the event
loop gained the external-source contract with zero behavior change when no
source is installed (async filter identical: 31/21/0/5/5). 4 new run cases
(ports 48101-48104) plus the health-server fixture, which now builds and
serves as written. Focused: 11 found / 10 passed / 0 failed / 1 ambiguous (the
pinned `headers` ambiguity, unchanged). Spec-pinned deferrals: TLS off (D70),
no deflate (`-DUWS_NO_ZLIB`), no raw TCP/UDP. Remaining surface (fetch
headers, `req.params/query/headers`, `res.json<T>` object codecs, `next()`
statement lowering, async handlers) is scheduled in the std completion rounds.
Open item: the registry row also accepts a default import
(`import network from "std/network"`); the spec shows named imports only, so
round 3 either pins the surface or records the extension. D67: the wave's
queued health-server BIG WARNs (7275 req/s) were measured while three agent
builds ran concurrently. On an idle machine the landing bench records
0 BIG WARNs and stays ahead of go/js/python; the stable 10 s window
(`--seconds 10`; evidence `build/scratch/lead-verify/landing-bench10/`) reads
11366 req/s, 1.11x cpp, 1.07x rust, 1 WARN (the 1.10x cpp watch line). Idle
3 s-window samples on the same build span 1.09x-1.17x cpp and 1.05x-1.07x
rust, so the cpp margin sits inside the short window's noise; D67.4 scorecards
should quote the longer window. Report:
`build/scratch/network/WAVE-NETWORK-REPORT.md`.

## typed-arrays wave landing record (2026-10-18)

Committed `44a7c37` (the same commit as network). The JS-compat typed-array
family (D69) is real: the 11 element tags on fixed-size backing,
`TypedArrayData`/`TypedArray<Tag>` views (byteOffset/length/buffer, storage
shared across views, set/fill/subarray/slice/copyWithin, index coercion with
out-of-range no-ops), `ArrayBuffer`/`SharedArrayBuffer`/`DataView` (unaligned
access, big-endian default with the explicit little-endian flag), and the
regular-array extras (`Array(n)`, `.fill`, `.of`/`.from`, `.join` with the
element-text contract). Codegen emits the family block (include plus the
formatter/truthiness helpers) only for units that use the family; sema owns
the constructor grammar, the member tables and the read-only guard. 114 new
cases (94 typed-arrays, 20 arrays-strings) land with it.

Lead cold verification (the wave's 94/94 was measured on warm artifacts;
evidence `build/scratch/lead-verify/tarrays/`): the first cold run was 12/94
and three runtime-header fixes (the `typedArraySetFrom` call shape, the
`array_buffer.hpp` declaration ordering, the `arrayBufferSlice` default) took
it to 93/94 and then 94/94; the serialized cold full corpus then caught two
revealed regular-array gaps (`if (a)` truthiness of an empty array and
whole-array `console.log` rendering) - fixed as the family block's
`cp3_ts(Array<E>)` / `cp3_truthy` overloads plus a type-driven family flag in
`tsCall`/`cond` (`noteFamilyUse`). Final cold focused: typed-arrays 94/94,
arrays-strings 185/120/0, types 110/85/0, variables 58/43/0. Report:
`build/scratch/tarrays/WAVE-TARRAYS-REPORT.md`.

Combined landing effect: cold full corpus 1372 found / 823 passed / 0 failed /
441 skipped / 108 ambiguous (before the waves: 1236 / 657 / 0 / 471 / 108);
lint 223 files, 0 errors.

## diagnostics wave landing record (2026-10-18)

Committed `1efa7f5`. Wave 0 (user priority, landed right after network and
typed-arrays). The suggestion engine (`src/common/include/cp3/suggest.hpp` +
`src/common/src/suggest.cpp`) ranks candidates case-first with a length-scaled
threshold, an affix bonus and at most three results; the best candidate is
folded into the message text, so "did you mean 'log'?" shows on a pipe, not
only on a TTY. Reporting sites: CP3301 (undefined name: visible names, plus
the exact import statement for a bare std binding), CP3305 (member miss on the
closed families Math / path / os / crypto / console / JSON; open families keep
the pinned permissive shape), CP3601/CP3604 (import hints: the usable named
imports, or the default-import form for a default-only module), and the
CP3002 texts now read "not implemented yet: <what>" (the skip marker is
unchanged). `Math` and `JSON` leave `kGlobals` because
docs/standard-library/04 requires the explicit import; a bare use is CP3301
with the statement. Console is surface-aware: log/info/warn/error are
implemented and suggested; assert/clear/input/readKey keep their CP3002 skip
and stay out of the candidate list. `docs/cli-tooling/07-diagnostic-catalog.md`
documents all 65 live codes plus 19 reserved; `tools/check_diagnostics.py`
cross-checks the header, the catalog and `src/**` (0 findings). 8 new pins in
`tests/cases/diagnostics/` (7 pass, 1 skip marker);
`variables/undeclared_property` re-pinned to CP3305.

Lead acceptance (cold, canonical Debug build under ccache, parallel runner):
focused diagnostics 8/7/0/1/0, variables 58/43/0/15/0, console 32/27/0/4/1;
cold full corpus 1380 found / 830 passed / 0 failed / 442 skipped / 108
ambiguous (the predicted baseline exactly); layout lint 225 files, 0 errors
(64 soft); checker 65 defined / 65 documented / 224 sources / 0 findings.
Report: `build/scratch/diagnostics/WAVE-DIAGNOSTICS-REPORT.md`.

## std package restructure landing record (2026-10-18)

Committed `489e276` - stage 1 of D72. 39 whole-file moves plus the minimal
wiring, replayed onto the main tree by
`build/scratch/restructure/replay-stage1.py` (dress-rehearsed against a byte
copy of the post-diagnostics tree; every precondition asserted; idempotent;
exit 0). Every std module now keeps all its phase slices in
`src/std/<family>/`: `sema.cpp`, `codegen.cpp`, `runtime*.cpp`, `native.hpp`
and `include/cp3/std/<name>.hpp`; typed-arrays keeps two units per phase
(`sema.cpp` + `sema_methods.cpp`, `codegen.cpp` + `codegen_methods.cpp`) and
its four headers under `include/cp3/`. The compiler core stays phase-first;
`src/codegen/src/emit/std/common.cpp` remains the codegen dispatch core (the
per-family table extraction is stage 2, spec in
`build/scratch/restructure/STAGE2-DEFERRALS.md`).

Generated code still spells `#include "cp3/std/<name>.hpp"`: the quoted
include resolves against the family's `include/` root, added only when the
program imports that `std/<name>` specifier (the D68 row selection);
`std/string` and `std/typed-arrays` roots are always available. `tests/cases/**`
did not move. ARCHITECTURE/STYLE path references were updated in the same
sweep, the mapping table below and D72's ABI bullet corrected to the landed
paths, and the two empty `src/runtime/{src,include/cp3}/std/` shells removed.

Lead verification (cold, ccache, parallel runner, canonical Debug build):
build 0 errors; layout lint 233 files, 0 errors (64 soft); filters crypto
25/25/0/0/0, typed-arrays 94/94/0/0/0, arrays-strings 185/120/0/56/9,
control-flow 75/41/0/28/6, types 110/85/0/20/5, async 31/21/0/5/5, stdlib
296/170/0/110/16 (the four port-binding network run cases included) - every
total identical to the stage-1 report's table. Report:
`build/scratch/restructure/WAVE-RESTRUCTURE-STAGE1-REPORT.md`.

## Perf wave (landed as `5c88ead`; plan below kept as written) - performance

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
   product surface first - CLI/init/package manager (lane C1, landed), then
   std/crypto/network/parallel and the rest of the wave list - before the next
   perf push; perf then continues as an ongoing loop judged by the benchmarks.

Ongoing objective after P1 (user directive, 2026-10-18): per task, match the
**fastest** reference row, not only cpp/rust - the D67 gate tiers are the todo
list and the warning rows are the queue. First target: string-churn (the
labeled run measures rust ~7x ahead of both cpp and cp3 there; profile whether
the generated string ops, the cpp entry, or both carry that gap).

Acceptance: corpus 0 failed + suite rerun meets the D64 targets.

## std package restructure wave (stage 1 landed as `489e276`; plan below kept as written) - D72

Executed immediately after the network and typed-arrays waves land and are
committed (never move files under a running agent), before the audit and the
perf work. Target: `src/std/<family>/` holds every phase slice of that module;
the compiler core (`lexer`, `parser`, `modules`, `common`, `sema`, `codegen`,
`runtime`, `cli`) stays phase-first.

Per-module mapping (crypto shown; identical shape for every module):

| today | after |
|---|---|
| `src/sema/src/checks/std_crypto.cpp` | `src/std/crypto/sema.cpp` |
| `src/codegen/src/emit/std/crypto.cpp` | `src/std/crypto/codegen.cpp` |
| `src/runtime/src/std/crypto.cpp` | `src/std/crypto/runtime.cpp` |
| `src/runtime/include/cp3/std/crypto.hpp` | `src/std/crypto/include/cp3/std/crypto.hpp` |
| `src/cli/src/detail/native_std_crypto.hpp` | `src/std/crypto/native.hpp` |

- network: same, with the runtime split preserved (`runtime.cpp`,
  `runtime_loop.hpp`, `runtime_fetch.cpp`, `runtime_server.cpp`,
  `runtime_ws*.cpp`).
- typed-arrays: `typed_array.hpp`, `array_buffer.hpp`, `data_view.hpp`,
  `array_extras.hpp`, `sema.cpp` + `sema_methods.cpp` (`tarrays_check` +
  `tarrays_methods`), `codegen.cpp` + `codegen_methods.cpp` (`emit/tarrays` +
  `tarrays_methods`).
- console, json, math, os, path, process, string: same shape, one folder each.

Steps and checks:

1. `git mv` every file; contents change only where includes or paths change.
   Full words in folder names (`typed-arrays`), phase names in file names.
2. CMake: sema/codegen/runtime targets pick up `src/std/*/` sources.
3. CLI: `compilerIncludeRoots`/`runtimeSourcePaths` (toolchain/util.cpp) and
   the native row table learn the per-family include roots; generated code
   still says `#include "cp3/std/<name>.hpp"`. A program only gets the roots
   of the families it imports.
4. Same-sweep updates: `tools/lint_layout.py`, `ARCHITECTURE.md`, `STYLE.md`,
   M2-PLAN and DECISIONS path references, native rows' repo-relative paths.
5. Corpus untouched: `tests/cases/**` stays as pinned.
6. Acceptance: canonical rebuild, lint 0 errors, cold full corpus
   1236 found / 0 failed / 471 skipped / 108 ambiguous baseline shape, both
   native smoke programs (crypto AES+sha, network health server) still build
   and run; then the landing record for this sweep.

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
