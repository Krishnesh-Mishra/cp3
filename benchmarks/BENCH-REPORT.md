# BENCH-REPORT - wave B1: benchmark-suite overhaul (D66)

Date: 2026-10-18. Author: the benchmark-suite writer (sole writer of
`benchmarks/**`, benchmark sections of the root README, the benchmarks pointer
in `examples/README.md`, and the `--bench` addition to
`tests/runner/run_tests.py`).

This report records what landed, what the audit found, where the raw evidence
lives, and what is deliberately left for later tiers.

## 1. What landed

- **Single entry point.** `benchmarks/run_benchmark.py` runs the whole suite
  with one command. It is a thin forwarding wrapper: every argument goes
  straight to the engine `bench/run_suite.py`, which remains the only
  implementation. Documented flags: `--only`, `--reps`, `--cp3c`, `--strict`,
  plus `--hello-reps`, `--churn-scale`, `--results` and the server load
  controls.
- **New columns.** Every compute table and the server table now carries a
  `peak_rss_mb` column (kernel `PeakWorkingSetSize`, sampled from Python while
  the process lives; honest `-` where not measurable) and a `binary_kb` column
  (size of the built executable after stripping a copy, where the toolchain
  strip applies; `-` for Node/Python and for rows that never built). Compute
  tables and the server table carry `cp3/cpp` and `cp3/rust` ratio columns on
  the cp3 row, and a final summary table prints one ratio pair per task with a
  geomean footer over every runnable pair.
- **Performance gate.** The runner prints `WARN` for every runnable row where
  cp3 is more than 1.2x slower than cpp or rust (time ratio above 1.2); the
  D64 target is cp3 within about 5% of the native pack for the native-type
  tiers. `--strict` makes the run exit 1 when any WARN printed.
- **Raw results, committed.** `--results DIR` tees the verbatim output to
  `DIR/run.txt` with `DIR/meta.txt` beside it (machine, argv, scale, and
  the cp3c SHA-256 since the post-P1 refresh).
  Both labeled runs are committed under `bench/results/`.
- **Six Tier-1 tasks, authored in full** for all five runnable languages plus
  spec-written cp3 pending entries: `compute-single-int`, `object-churn`,
  `array-growth`, `closure-capture`, `interface-dispatch`, `generics-mono`.
  Every task README states the exact expected output; every value is
  re-derived by the independent oracle.
- **Finished tasks:** `hello-world` (best and median over 15+ runs, raw run
  list printed), `string-churn` (default and stress scales; cp3 scale is a
  source constant with only that constant substituted for non-default scales),
  `word-frequency` (hash-map shaped workload).
- **Independent oracle.** `bench/oracle.py` re-implements every workload in
  plain Python. It is the source of every embedded expected value and is run
  by hand, never during timed measurement.
- **Harness integration.** `tests/runner/run_tests.py --bench` runs the
  conformance corpus first, then invokes `benchmarks/run_benchmark.py`,
  streams its output, and re-prints the gate WARN lines at the end. It is
  off by default: without `--bench` the harness behaves exactly as before.
  `--bench-args "..."` forwards extra arguments for cheap targeted reruns.
- **Documentation.** `benchmarks/README.md` rewritten (methodology, fairness
  contract, how to run, task table, table-reading guide, gate, labeled runs,
  refresh policy). Root `README.md` benchmark section refreshed from the same
  labeled run. `examples/README.md` now points at `../benchmarks/`.
  Nine per-task READMEs added.

## 2. Audit findings

- **Naming and location.** The suite lived at `examples/competitions/` and
  called itself a "competition" everywhere. It is now `benchmarks/` with
  benchmark naming throughout; all references were swept: root README,
  `examples/README.md`, per-task READMEs, runner banners, and the Express
  package name/description (`cp3-competition-health-server-express` ->
  `cp3-benchmark-health-server-express`).
- **Stale numbers.** The root README carried a 2026-10-17 sample covering
  three tasks. Now both READMEs draw from a single dated labeled run covering
  all twelve tasks, and the refresh policy forbids hand-editing numbers.
- **Fairness bugs (fixed).** The C++ `string-churn` entry was doing
  locale-aware `std::toupper` per character (about 4.6 s at 40k iterations)
  and the Go entry was going through `strings.ToUpper`/string building (about
  4.0 s at 40k), both comparing library tables rather than language speed.
  Both now use ASCII fast paths, each annotated with an inline `Fairness
  note` comment; post-fix they land at roughly 1.3 s (C++) and 1.8 s (Go) at
  40k, next to the other fast entries.
- **cp3 has no argv surface** (verified against the beta-one compiler: no
  argv indexing, no stdin, no env). `string-churn` therefore pins its scale
  as a source constant and the runner substitutes only that constant into a
  generated copy under `bench/.build/` for any non-default scale.
- **Character hygiene (D66.4).** Em/en dashes were swept from `benchmarks/**`
  and everything written in this wave is dash-free.
- **Engine scope.** The engine's measurement core (server trials, compute
  trials, build caching, safe process handling) was kept; reporting,
  sampling, scaling and the gate were added around it, and the task registry
  grew from 3 to 12 entries.

## 3. Labeled runs and gate outcome

Main run: `bench/results/2026-10-18T0705-local/` (`run.txt` + `meta.txt`),
twelve tasks, `--reps 2`, `--hello-reps 15`, `bench/.build/` cleared
immediately before (all native builds fresh).

Stress run: `bench/results/2026-10-18T0712-local-stress/` (`run.txt` +
`meta.txt`), `--only string-churn --churn-scale stress --reps 2`, fresh
builds.

Both runs used the same compiler binary: `build/bin/cp3c.exe`, mtime
2026-10-17 16:36:22, SHA-256
`C8864FCEF7801EA102D0BA8C87A7744525FC9C9B5FAA160BD7D02344CD770D04` (checked
after both runs; no rebuild happened in between).

Gate outcome: 3 WARN lines in the main run, all three the designed tripwire
firing on honest gaps rather than failures:

```
WARN compute-single: cp3 1154 ms is 7.59x cpp (152 ms), gate 1.2x
WARN compute-single: cp3 1154 ms is 8.60x rust (134 ms), gate 1.2x
WARN string-churn: cp3 1681 ms is 7.38x rust (228 ms), gate 1.2x
gate: 3 warning(s); rerun with --strict to fail on warnings
```

In context: `hello-world` passes the gate outright (0.73x cpp, 1.00x rust),
`string-churn` is 1.07x cpp, and the summary table's geomean over the three
runnable ratio pairs is 1.80x vs cpp and 3.99x vs rust. The stress run adds
one more designed warning: at 300k churn iterations cp3 is 10.90x rust
(13819 ms vs 1267 ms) while staying at 1.04x cpp. `--strict` exits 1 on
exactly these lines today; a wave that lands codegen work should make the
list shorter, and that is the intended use of the gate.

## 4. Correctness evidence

- Every runnable row of every task printed output byte-identical to the other
  languages and identical to the independent oracle value stated in the task
  README (`match` column `ref`/`ok`; no `MISMATCH`, no `!!` oracle warnings).
- cp3 rows that run today (`compute-single`, `hello-world`, `string-churn`)
  match the oracle as well, at both churn scales.
- During authoring, `string-churn` was additionally cross-checked at 2,000
  iterations (`128576`) and 40,000 iterations (`675862`) across all six
  languages, including cp3.

## 5. Tier-1 cp3 status (snapshot 2026-10-19)

Scan method: each entry is copied to a scratch directory and built once with
`build\bin\cp3c.exe`; the quoted text is the compiler's own first diagnostic.

- `health-server`: **runs**. It builds against the landed `std/network` and
  serves the wire contract on the port it is given as `argv[1]`
  (`/healthz` -> 200 `ok`, `/` -> 200 `health-server ok`, `/nope` -> 404
  `not found`). The timed row joins the next suite run on a quiet machine.
- `compute-single-int`: `main.cp3:25:10: error: CP3002: not implemented yet: this returned value` (the native-type tier, D65)
- `compute-multi`: `main.cp3:34:15: error: CP3002: not implemented yet: parallel.*`
- `word-frequency`: `main.cp3:28:3: error: CP3002: not implemented yet: this method call` (the array surface; the Map surface itself landed and the entry uses the inference form because a `Map<...>` type annotation is a separate known gap)
- `object-churn`: `main.cp3:17:1: error: CP3002: not implemented yet: classes`
- `array-growth`: `main.cp3:19:3: error: CP3002: not implemented yet: this method call`
- `closure-capture`: `main.cp3:23:3: error: CP3002: not implemented yet: this method call`
- `interface-dispatch`: `main.cp3:20:1: error: CP3002: not implemented yet: classes` (the entry now uses the spec class syntax)
- `generics-mono`: `main.cp3:23:1: error: CP3002: not implemented yet: generic functions` (plus `classes` for `Box<T>`)

A pending row is not a failure: it is the compiler's own diagnostic, captured
verbatim, and the row fills in automatically when the feature lands, with no
change to the entry or the runner.

## 6. Tier-2 staged (deliberately NOT authored in this wave)

Per D66.7 these are staged as rows in this report only, with the prerequisite
each one is waiting for:

| task | hypothesis it would test | prerequisite |
| --- | --- | --- |
| `worker-fanout` | N workers pulling from a shared queue; coordination and scheduling cost per task | classes/collections in cp3; no new std dependency for the other five |
| `json-roundtrip` | parse -> mutate -> serialize a large nested document; string + tree cost | `std/JSON` parse side in cp3 |
| `fs-pipeline` | write/read/glob/delete a tree of files; syscall and buffering cost | `std/fs` in cp3 |
| `async-fanout` | 100k+ concurrent `await sleep(0)` tasks; event-loop scheduling throughput | none for cp3 (async landed); needs a pinned fairness rule for timer granularity |
| `regex-scan` | find-all over a large pinned text with fixed patterns; engine cost | `std/regex` in cp3 |
| `health-server` JSON-API extension | the existing server benchmark plus `POST /json` echo with body parsing and validation | `req.json<T>()` in cp3 (already exercised by `examples/servers/echo-api`) |
| `compute-multi` | already exists; its cp3 row joins the table when `parallel.*` codegen lands | `parallel.*` in cp3 |

Suggested order when the waves land: `async-fanout` first (no new std
dependency and it widens the concurrency story), then the std-gated trio
(`json-roundtrip`, `fs-pipeline`, `regex-scan`), then the class/collections
tasks (`worker-fanout`, the health-server JSON extension).

## 7. Deviations and decisions

- **hello-world is documented as best + median of 15+ runs** rather than a
  single number, because single spawn measurements at the 5-40 ms scale are
  dominated by antivirus/loader noise on this machine; the raw run list is
  printed so the spread is visible.
- **string-churn stress targets the fast pack**, not Rust: at 300k iterations
  cpp/go/js/python/cp3 run for roughly eleven to twenty-three seconds while
  Rust stays well under two seconds. That is reported as-is; deliberately
  stretching Rust further would push the other rows past any reasonable run
  time.
- **Ratio semantics:** all ratios are time ratios (cp3 time divided by
  reference time; lower is better). For servers they are derived from
  throughput (reference req/s divided by cp3 req/s). Ratios are printed only
  where both rows measured; `-` otherwise, and the gate stays silent rather
  than guessing.
- **Peak RSS and binary size are "honest n/a" columns:** `-` when the process
  is too short-lived to sample, on non-Windows platforms, and for Node/Python
  binary sizes. No fake zeros anywhere.
- **`--bench` in the test harness is additive:** one boolean plus an optional
  argument passthrough; the corpus path and exit codes are untouched when the
  flag is absent, and `--json` mode notes and skips the bench pass so machine
  consumers keep parseable output.
- **The engine stays the engine:** `run_benchmark.py` contains no measurement
  logic, only forwarding, so there is exactly one implementation of the
  methodology.

## 8. How to reproduce

```bash
# full labeled run (fresh builds)
python benchmarks/run_benchmark.py --reps 2 --hello-reps 15 --results benchmarks/bench/results/<timestamp>

# stress churn
python benchmarks/run_benchmark.py --only string-churn --churn-scale stress --reps 2 --results benchmarks/bench/results/<timestamp>-stress

# correctness oracle for every expected value
python benchmarks/bench/oracle.py

# harness-integrated run (corpus, then suite, gate warnings re-printed)
python tests/runner/run_tests.py --bench
```

Refresh policy: rerun after every performance wave, paste the verbatim block
with a dated header, never hand-edit a number. See the tail of
`benchmarks/README.md`.

## 9. Known limitations

- Peak RSS sampling is Windows-only by design (no `psutil` dependency); other
  platforms honestly report `-`.
- The gate WARNs today on `compute-single` (7.59x cpp / 8.60x rust) and
  `string-churn` (7.38x rust in the default run, 10.90x rust at stress
  scale). Those lines are the performance wave's checklist; a wave that lands
  perf work should make them shorter or quieter, and `--strict` exists so a
  future wave can gate on them directly.
- Number noise on this machine is real (antivirus scans, first-run JIT
  behavior); every table prints all runs so the noise is visible rather than
  averaged away.

## 10. Addendum (D67): tiered gate and benchmarks on every test run

Date: 2026-10-18, same day as the labeled run above; the labeled tables stay
as recorded.

The lead superseded the single 1.2x tripwire with a tiered gate and made the
benchmark suite part of every test harness run. What changed:

- **Tiered gate (`bench/run_suite.py`).** Every runnable task row is gated
  against all five references as a time ratio (lower is better for cp3):
  - **BIG WARN** when cp3 is over 1.20x cpp or rust, or slower than any of
    go/js/python at all (ratio over 1.00), one line per reference. Lines read
    `BIG WARN (fix this): ...` and start with `BIG WARN `.
  - **WARN** when cp3 is over 1.10x cpp or rust but under the big tier, one
    line per reference, starting with `WARN `.
  - The footer counts both tiers; `--strict` exits 1 when any WARN or
    BIG WARN fired.
  - Noise floor: compute rows whose reference best is under 10 ms print a
    `note:` line ("noise-scale, ratio not gated") instead of a tier, so
    hello-world spawn jitter cannot flap the gate.
- **Auto-run (`tests/runner/run_tests.py`).** After the corpus pass the suite
  runs by default; `--no-bench` skips it; `--bench` stays as an explicit (now
  redundant) request; `--bench-args` still forwards; `--json` skips the bench
  pass with a note; the exit code stays `max(corpus, bench)`.
- **Docs.** The gate section and harness paragraph of `benchmarks/README.md`,
  the gate one-liner in the root `README.md`, and this addendum describe the
  tiered policy. The labeled tables earlier in this report are unchanged.
- The two committed labeled runs
  (`benchmarks/bench/results/2026-10-18T0705-local` and
  `benchmarks/bench/results/2026-10-18T0712-local-stress`) are untouched and
  remain the labeled record; they show the older single-tripwire format. The
  next labeled refresh, after the performance wave lands and cp3c is rebuilt,
  will show the tier format.

Evidence below is verbatim output from the current `build/bin/cp3c.exe`
(unchanged since the labeled run; see the provenance notes above).

### 10.1 Tier lines fire as designed

```
$ python benchmarks/run_benchmark.py --only compute-single --reps 2
benchmarks bench suite -- 2026-10-18T07:34:21
machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | 20 logical CPUs | Windows-11-10.0.26200-SP0
runner:  python 3.14.5 (C:\Program Files\Python314\python.exe)
  g++     g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0
  rustc   rustc 1.95.0 (59807616e 2026-04-14)
  go      go version go1.26.4 windows/amd64
  node    v24.15.0
  python  Python 3.14.5
  cp3c    cp3c 0.1.0-beta.1
every number below is a wall-clock measurement from this run; load phases use persistent keep-alive connections (one per client)
[run ] compute-single: cpp ...
[run ] compute-single: rust ...
[run ] compute-single: go ...
[run ] compute-single: js ...
[run ] compute-single: python ...
[run ] compute-single: cp3 ...

== compute-single == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp             -       151 154/151              131434424                  ref            -         -         4.0        19  build: cached
rust            -       135 141/135              131434424                  ok             -         -         3.5       122  build: cached
go              -       206 228/206              131434424                  ok             -         -         5.5      1750  build: cached
js              -      1123 1123/1134            131434424                  ok             -         -        36.8         -  
python          -      9835 9901/9835            131434424                  ok             -         -        11.4         -  
cp3           671      1171 1211/1171            131434424                  ok         7.77x     8.67x         4.1       107  

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
compute-single           7.77x      8.67x
------------------------------------------
geomean over runnable rows     7.77x (n=1)     8.67x (n=1)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
BIG WARN (fix this): compute-single: cp3 1171 ms is 7.77x cpp (151 ms), tripwire 1.20x
BIG WARN (fix this): compute-single: cp3 1171 ms is 8.67x rust (135 ms), tripwire 1.20x
BIG WARN (fix this): compute-single: cp3 1171 ms is 5.68x go (206 ms), tripwire 1.00x
BIG WARN (fix this): compute-single: cp3 1171 ms is 1.04x js (1123 ms), tripwire 1.00x
gate: 4 BIG WARN(s), 0 WARN(s); rerun with --strict to fail on warnings
```

Exit code: 0, because warnings do not fail a default run. compute-single is
slower than cpp (7.77x), rust (8.67x), go (5.68x) and js (1.04x), so four
BIG WARN lines fire; python (0.12x) stays silent. Those four lines are the
performance wave's checklist.

A follow-up strict run of the same task exited 1 as designed:
`python benchmarks/run_benchmark.py --only compute-single --reps 1 --strict`
printed three BIG WARN lines (js landed at or above cp3's time in that single
run) and `EXITCODE=1`.

### 10.2 Bench pass runs by default

```
$ python tests/runner/run_tests.py --filter cli --bench-args "--only hello-world --hello-reps 3"
cp3 test harness
  compiler : C:\Users\Krishnesh\dev\cp3\build\bin\cp3c.exe
  cases dir: C:\Users\Krishnesh\dev\cp3\tests\cases
  found    : 27 case(s)

  amb  cli/_ambiguous/build_debug_flag: the --debug build profile is a toolchain flag with no .cp3-observable semantics.
  amb  cli/_ambiguous/build_release_flag: the --release build profile (whole-program LTO) has no observable effect inside a .cp3 program.
  amb  cli/_ambiguous/clean_all_wipes_packages: cp3 clean --all deleting .cp3/packages/ cannot be expressed as a .cp3 case.
  amb  cli/_ambiguous/clean_wipes_build: cp3 clean deleting .cp3/build/ cannot be expressed as a .cp3 case.
  amb  cli/_ambiguous/dev_watch_mode: cp3 dev's watch/rebuild behavior is not expressible as a .cp3 case.
  amb  cli/_ambiguous/dot_cp3_layout: the generated .cp3/ directory layout is a project-tooling artifact, not observable from a .cp3 program.
  amb  cli/_ambiguous/exec_package_bin: cp3 exec resolving and running a package's bin tool is not expressible as a .cp3 case.
  amb  cli/_ambiguous/fmt_formatter: cp3 fmt's output and the package.json "format" overrides are not expressible as a .cp3 case.
  amb  cli/_ambiguous/init_generates_layout: the generated project layout from cp3 init is not observable through `cp3c run/check <file>`.
  amb  cli/_ambiguous/init_prompts: cp3 init's four interactive prompts and generated files cannot be expressed as a single .cp3 case.
  amb  cli/_ambiguous/install_auto_flag: the --auto compatibility-selection behavior is not expressible as a .cp3 case.
  amb  cli/_ambiguous/install_dependencies: cp3 install's network/registry behavior is not expressible as a .cp3 case.
  amb  cli/_ambiguous/install_github_url: installing from a GitHub release URL (single/multiple/none .cp3pak assets) is not expressible as a .cp3 case.
  amb  cli/_ambiguous/lifecycle_postbuild_hook: npm-style pre*/post* lifecycle hooks run around CLI commands, not inside a .cp3 program.
  amb  cli/_ambiguous/lifecycle_prebuild_hook: npm-style pre*/post* lifecycle hooks run around CLI commands, not inside a .cp3 program.
  amb  cli/_ambiguous/max_memory_flag: the --max-memory codegen cap is a CLI flag with no .cp3-observable semantics.
  amb  cli/_ambiguous/recompile_cp3pak: recompiling an existing .cp3pak is a tooling operation, not a .cp3 case.
  amb  cli/_ambiguous/run_vs_check: the distinction between `cp3 run` and `cp3 check` is a CLI-invocation property, not a property of a .cp3 program.
  amb  cli/_ambiguous/spinner_rule: the spinner rule is a CLI UX property with no .cp3-observable semantics.
  amb  cli/_ambiguous/static_link_single_binary: the static-link output shape is a build-artifact property, not a .cp3 case.
  amb  cli/_ambiguous/toolchain_fallback: toolchain selection/fallback and multi-toolchain error grouping are CLI behaviors, not .cp3 cases.
  amb  cli/_ambiguous/type_package_build: producing a .cp3pak from a package project is an artifact/tooling behavior, not a .cp3 case.
  amb  cli/_ambiguous/version_flag: the version command's output is a CLI property, not a .cp3 case.

27 found, 4 passed, 0 failed, 0 skipped, 23 ambiguous.

== benchmarks ==
  command: C:\Program Files\Python314\python.exe C:\Users\Krishnesh\dev\cp3\benchmarks\run_benchmark.py --only hello-world --hello-reps 3

benchmarks bench suite -- 2026-10-18T07:35:12
machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | 20 logical CPUs | Windows-11-10.0.26200-SP0
runner:  python 3.14.5 (C:\Program Files\Python314\python.exe)
  g++     g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0
  rustc   rustc 1.95.0 (59807616e 2026-04-14)
  go      go version go1.26.4 windows/amd64
  node    v24.15.0
  python  Python 3.14.5
  cp3c    cp3c 0.1.0-beta.1
every number below is a wall-clock measurement from this run; load phases use persistent keep-alive connections (one per client)
[run ] hello-world: cpp ...
[run ] hello-world: rust ...
[run ] hello-world: go ...
[run ] hello-world: js ...
[run ] hello-world: python ...
[run ] hello-world: cp3 ...

== hello-world == expected = Hello, world! | spawn -> exit wall time, best and median of 3 runs
lang     build_ms   best_ms median_ms all_runs_ms          output           match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp             -       6.8       7.2 12/7/7               Hello, world!    ref            -         -         4.5        19  build: cached
rust            -       5.0       5.1 9/5/5                Hello, world!    ok             -         -         4.0       122  build: cached
go              -       6.1       6.6 10/7/6               Hello, world!    ok             -         -         2.0      1750  build: cached
js              -      40.2      42.5 48/43/40             Hello, world!    ok             -         -        35.1         -  
python          -      25.6      26.3 28/26/26             Hello, world!    ok             -         -        11.4         -  
cp3            24       6.6       7.1 12/7/7               Hello, world!    ok         0.97x     1.33x         4.6       106  

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
hello-world              0.97x      1.33x
------------------------------------------
geomean over runnable rows     0.97x (n=1)     1.33x (n=1)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
note: hello-world: cp3 6.6 ms vs rust 5.0 ms is noise-scale, ratio not gated
note: hello-world: cp3 6.6 ms vs go 6.1 ms is noise-scale, ratio not gated
gate: no warnings

  bench gate: no warnings
```

Exit code: 0. The corpus ran first, then the suite, with no `--bench` flag
anywhere. Note the noise floor doing its job: with three reps, rust's best
was 5.0 ms and go's 6.1 ms, so cp3's ratios against them (1.33x rust, 1.08x
go) printed as noise-scale notes, not tiers.

### 10.3 Opt-out

```
$ python tests/runner/run_tests.py --filter cli --no-bench
cp3 test harness
  compiler : C:\Users\Krishnesh\dev\cp3\build\bin\cp3c.exe
  cases dir: C:\Users\Krishnesh\dev\cp3\tests\cases
  found    : 27 case(s)

  amb  cli/_ambiguous/build_debug_flag: the --debug build profile is a toolchain flag with no .cp3-observable semantics.
  amb  cli/_ambiguous/build_release_flag: the --release build profile (whole-program LTO) has no observable effect inside a .cp3 program.
  amb  cli/_ambiguous/clean_all_wipes_packages: cp3 clean --all deleting .cp3/packages/ cannot be expressed as a .cp3 case.
  amb  cli/_ambiguous/clean_wipes_build: cp3 clean deleting .cp3/build/ cannot be expressed as a .cp3 case.
  amb  cli/_ambiguous/dev_watch_mode: cp3 dev's watch/rebuild behavior is not expressible as a .cp3 case.
  amb  cli/_ambiguous/dot_cp3_layout: the generated .cp3/ directory layout is a project-tooling artifact, not observable from a .cp3 program.
  amb  cli/_ambiguous/exec_package_bin: cp3 exec resolving and running a package's bin tool is not expressible as a .cp3 case.
  amb  cli/_ambiguous/fmt_formatter: cp3 fmt's output and the package.json "format" overrides are not expressible as a .cp3 case.
  amb  cli/_ambiguous/init_generates_layout: the generated project layout from cp3 init is not observable through `cp3c run/check <file>`.
  amb  cli/_ambiguous/init_prompts: cp3 init's four interactive prompts and generated files cannot be expressed as a single .cp3 case.
  amb  cli/_ambiguous/install_auto_flag: the --auto compatibility-selection behavior is not expressible as a .cp3 case.
  amb  cli/_ambiguous/install_dependencies: cp3 install's network/registry behavior is not expressible as a .cp3 case.
  amb  cli/_ambiguous/install_github_url: installing from a GitHub release URL (single/multiple/none .cp3pak assets) is not expressible as a .cp3 case.
  amb  cli/_ambiguous/lifecycle_postbuild_hook: npm-style pre*/post* lifecycle hooks run around CLI commands, not inside a .cp3 program.
  amb  cli/_ambiguous/lifecycle_prebuild_hook: npm-style pre*/post* lifecycle hooks run around CLI commands, not inside a .cp3 program.
  amb  cli/_ambiguous/max_memory_flag: the --max-memory codegen cap is a CLI flag with no .cp3-observable semantics.
  amb  cli/_ambiguous/recompile_cp3pak: recompiling an existing .cp3pak is a tooling operation, not a .cp3 case.
  amb  cli/_ambiguous/run_vs_check: the distinction between `cp3 run` and `cp3 check` is a CLI-invocation property, not a property of a .cp3 program.
  amb  cli/_ambiguous/spinner_rule: the spinner rule is a CLI UX property with no .cp3-observable semantics.
  amb  cli/_ambiguous/static_link_single_binary: the static-link output shape is a build-artifact property, not a .cp3 case.
  amb  cli/_ambiguous/toolchain_fallback: toolchain selection/fallback and multi-toolchain error grouping are CLI behaviors, not .cp3 cases.
  amb  cli/_ambiguous/type_package_build: producing a .cp3pak from a package project is an artifact/tooling behavior, not a .cp3 case.
  amb  cli/_ambiguous/version_flag: the version command's output is a CLI property, not a .cp3 case.

27 found, 4 passed, 0 failed, 0 skipped, 23 ambiguous.
```

Exit code: 0. The output ends with the corpus summary; no benchmark section
follows.

## 11. Post-P1 scorecard (2026-10-18)

P1 (the `%` lowering without libm plus the guarded int64 `number` fast path)
landed, the canonical compiler was rebuilt from it, and the full labeled
suite was rerun against that binary (`bench/.build/` cleared immediately
before; no `--strict`; exit 0). Raw output:
`benchmarks/bench/results/2026-10-18T0803-local/run.txt` with `meta.txt`
beside it, plus the stress run at
`benchmarks/bench/results/2026-10-18T0808-local-stress/`. Compiler:
`build/bin/cp3c.exe`, SHA-256
`C051967809915DAE897618FC9F5CC0D5C4516329E991DC3C190DE9CC37257432`, mtime
2026-10-18 07:53:27, verified identical before and after both runs. The cp3c
lines in the 0803 `meta.txt` were appended right after that run from the
verified binary (the runner now writes them inline; the 0808 `meta.txt`
shows the native format). Baseline for the deltas below: the pre-P1
`2026-10-18T0705-local` run (cp3c SHA-256 `C8864FCE...`, the
single-tripwire gate).

Gate block, verbatim (every WARN and BIG WARN line, plus the footer):

```
== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
BIG WARN (fix this): compute-single: cp3 162 ms is 1.25x rust (130 ms), tripwire 1.20x
WARN string-churn: cp3 1596 ms is 1.10x cpp (1445 ms), watch 1.10x
BIG WARN (fix this): string-churn: cp3 1596 ms is 7.46x rust (214 ms), tripwire 1.20x
BIG WARN (fix this): string-churn: cp3 1596 ms is 1.30x python (1229 ms), tripwire 1.00x
note: hello-world: cp3 5.9 ms vs go 5.8 ms is noise-scale, ratio not gated
gate: 3 BIG WARN(s), 1 WARN(s); rerun with --strict to fail on warnings
```

Suite summary, verbatim (all 12 tasks):

```
== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
health-server                -          -
compute-single           1.08x      1.25x
compute-single-int           -          -
compute-multi                -          -
hello-world              0.89x      0.99x
string-churn             1.10x      7.46x
word-frequency               -          -
object-churn                 -          -
array-growth                 -          -
closure-capture              -          -
interface-dispatch           -          -
generics-mono                -          -
------------------------------------------
geomean over runnable rows     1.02x (n=3)     2.10x (n=3)
ratios are cp3 time divided by the reference time; lower is better
```

Runnable rows, same run (the other nine tasks are pending cp3 entries with
compiler diagnostics and `-` ratios):

| task | cp3 best_ms (all runs) | cp3/cpp | cp3/rust |
| --- | --- | --- | --- |
| compute-single | 162 (180/162) | 1.08x | 1.25x |
| hello-world | 5.9 ms best, 7.7 ms median of 15 | 0.89x | 0.99x |
| string-churn | 1596 (1596/1603) | 1.10x | 7.46x |

Before/after deltas, `2026-10-18T0705-local` to `2026-10-18T0803-local`:

| metric | before | after | change |
| --- | --- | --- | --- |
| compute-single cp3 best_ms | 1154 | 162 | 7.1x faster |
| compute-single cp3/cpp | 7.59x | 1.08x | |
| compute-single cp3/rust | 8.60x | 1.25x | |
| hello-world cp3 best_ms | 6.2 | 5.9 | about 5% faster |
| hello-world cp3/cpp | 0.73x | 0.89x | still ahead of cpp |
| hello-world cp3/rust | 1.00x | 0.99x | at parity |
| string-churn cp3 best_ms | 1681 | 1596 | about 5% faster |
| string-churn cp3/cpp | 1.07x | 1.10x | cpp gained more (8.4%) |
| string-churn cp3/rust | 7.38x | 7.46x | rust gained more (6.1%) |
| geomean cp3/cpp | 1.80x (n=3) | 1.02x (n=3) | |
| geomean cp3/rust | 3.99x (n=3) | 2.10x (n=3) | |
| string-churn stress cp3 best_ms | 13819 | 13435 | 2.8% faster |
| string-churn stress cp3/cpp | 1.04x | 1.01x | |
| string-churn stress cp3/rust | 10.90x | 11.83x | rust gained more (10.4%) |

Read: P1 moved compute-single from 1154 ms to 162 ms (7.59x/8.60x to
1.08x/1.25x), and the only remaining native BIG WARN on that row is 1.25x
rust. string-churn improved in absolute time (default about 5%, stress 2.8%)
but its ratios ticked up because cpp and rust improved faster; its gate lines
are now 7.46x rust, 1.30x python (a scripted runtime ahead of cp3 in the
default run; 1.32x at stress) and 1.10x cpp. hello-world landed at 5.9 ms
best and sits under the 10 ms noise floor, so the gate prints one
noise-scale note (cp3 5.9 ms vs go 5.8 ms) instead of tiers. No reruns were
needed for machine noise: no row was wildly off its siblings, and the
first-run spikes (hello-world run 1: cpp 35 ms, go 126 ms, cp3 44 ms) are the
documented loader/AV pattern that best + median + all runs exist to absorb.
