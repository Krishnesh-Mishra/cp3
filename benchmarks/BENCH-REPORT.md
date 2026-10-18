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
  `DIR/run.txt` with `DIR/meta.txt` beside it (machine, argv, toolchains).
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

## 5. Tier-1 cp3 status (pending rows)

- `health-server`: `main.cp3:24:13: error: CP3002: codegen: CallExpr ('call to function 'createServer' not declared in this unit') is not implemented in M1`
- `compute-single-int`: `main.cp3:25:10: error: CP3002: codegen: Identifier ('returned value outside M1') is not implemented in M1`
- `compute-multi`: `main.cp3:34:15: error: CP3002: codegen: CallExpr ('parallel.*') is not implemented in M1`
- `word-frequency`: `main.cp3:27:1: error: CP3302: cannot find type name 'Map'`
- `object-churn`: `main.cp3:17:1: error: CP3002: not implemented yet: class declarations`
- `array-growth`: `main.cp3:15:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1`
- `closure-capture`: `main.cp3:20:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1`
- `interface-dispatch`: `main.cp3:18:1: error: CP3207: syntax error: expected ';'`
- `generics-mono`: `main.cp3:14:1: error: CP3207: syntax error: expected ';'`

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
