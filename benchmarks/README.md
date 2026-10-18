# cp3 benchmarks - one suite, six languages

Cross-language comparison suite (decision D60, refreshed under D66): the same
small tasks implemented side by side in **cp3, C++, Rust, Go, JavaScript
(Node) and Python**, in near-identical readable sources, plus a
dependency-free runner that builds, starts, measures and stops everything on
this machine and prints comparison tables with per-row `cp3/cpp` and
`cp3/rust` ratios, measured peak memory, stripped binary size, and a
performance gate.

Everything is measured, nothing is estimated. Every number in every table
came from a real process the runner started and stopped itself. Toolchains
that are not on `PATH` are skipped with a printed note; cp3 entries whose
compiler features have not landed yet are attempted like everything else and
reported with the compiler's own `CP3xxx` diagnostic instead of a timing;
when a feature lands, the row fills in by itself, unchanged.

This suite is featured in the repository's root `README.md`; every number
there comes from the same labeled run as the tables below, and re-running the
suite refreshes them.

## How to run

Single entry point (Python 3 only, stdlib, no packages):

```bash
python benchmarks/run_benchmark.py                  # everything, defaults
python benchmarks/run_benchmark.py --only hello-world,string-churn
python benchmarks/run_benchmark.py --reps 3         # compute reps (best reported)
python benchmarks/run_benchmark.py --churn-scale stress
python benchmarks/run_benchmark.py --cp3c build/bin/cp3c.exe
python benchmarks/run_benchmark.py --strict         # exit 1 on any gate WARN
python benchmarks/run_benchmark.py --results DIR    # tee raw output to DIR
```

`run_benchmark.py` is a thin wrapper: it forwards every argument to the engine
`bench/run_suite.py`, which is the only implementation. From `benchmarks/bench`
in PowerShell, `.\run.ps1 --only word-frequency` does the same.

| flag | meaning |
| --- | --- |
| `--only T[,T...]` | run a subset of tasks (see the task table below) |
| `--reps N` | compute repetitions per entry; best is reported (default 2) |
| `--hello-reps N` | hello-world runs; best and median are reported (default 15) |
| `--churn-scale V` | string-churn scale: `default` (50,000), `stress` (300,000) or an integer |
| `--cp3c PATH` | cp3 compiler (default `build/bin/cp3c.exe`) |
| `--strict` | exit code 1 when the performance gate prints any WARN |
| `--results DIR` | tee the raw output to `DIR/run.txt` plus `DIR/meta.txt` |
| `--seconds S`, `--concurrency C`, `--latency N`, `--warmup N` | server load phases (defaults 3 s, 8 connections, 300 latency requests, 100 warmup) |

The test harness can also run the suite for you (opt-in, additive):

```bash
python tests/runner/run_tests.py --bench
python tests/runner/run_tests.py --bench --bench-args "--only hello-world"
```

`--bench` runs the conformance corpus first, then invokes
`benchmarks/run_benchmark.py`, streams its output, and re-prints the
performance-gate WARN lines at the end. Without `--bench` the harness behaves
exactly as before.

The runner needs Python 3 only. It never stops a process it did not start:
servers are launched with `subprocess.Popen`, measured, and killed by exact
process id.

## The tasks

| task | what it measures | output (match oracle) | cp3 today |
| --- | --- | --- | --- |
| `compute-single` | total Collatz steps for 1..1,000,000, single thread | `131434424` | runs |
| `compute-single-int` | the same total in native integer types (D65 tier) | `131434424` | pending native types |
| `compute-multi` | the same total split across all cores | `131434424` | pending `parallel.*` |
| `hello-world` | spawn -> exit wall time, best and median of 15+ runs | `Hello, world!` | runs |
| `string-churn` | 8 case/slice rounds per iteration over a 4 KiB source | `852167` (50k) / `254546` (300k) | runs |
| `word-frequency` | 3M LCG picks into a 64-word hash map, folded counts | `37425343` | pending `std/collections` |
| `object-churn` | 5M small objects, half die in scope, half escape | `90641269` | pending classes |
| `array-growth` | 20M pushes while summing, then pop half | `200000010000000` | pending arrays |
| `closure-capture` | map/filter/reduce over 10M + 1000 escaping closures | `16666688333331 998521500000` | pending closures |
| `interface-dispatch` | 2M shapes through `area()`, plus a direct-call control loop | `37999986 66000000` | pending classes/interfaces |
| `generics-mono` | generic `Box<T>` loop vs a hand-written concrete copy | `998986007 998986007` | pending generics |
| `health-server` | `GET /healthz` -> `ok`, `GET /` -> `health-server ok`, else `404` | wire contract (below) | pending `std/network` |

Every value above is re-derived independently by `bench/oracle.py`, which
re-implements every workload again in plain Python; the runner cross-checks
all languages against each other and against those oracle values (the `match`
column plus `!!` warning lines for any disagreement).

The health-server contract: every language listens on `127.0.0.1` only and
answers `GET /healthz` with `200` text/plain `ok`, `GET /` with `200`
text/plain `health-server ok`, and anything else with `404` text/plain
`not found`. The port comes from `argv[1]` (the suite assigns 18080-18084 per
language); the cp3 entry uses a `const` port because the beta-one std spec has
no process-argv surface.

Each task folder has its own README with the exact workload definition, the
expected output, the per-language build/run commands, and a link back to the
labeled run tables here.

## Reading the tables

Compute tables:

| column | meaning |
| --- | --- |
| `build_ms` | fresh compile (`g++ -O2`, `rustc -O`, `go build`, `cp3c build`); `-` = no build step, or the binary was already up to date |
| `best_ms` | best wall-clock run of the `--reps` runs (hello-world: best and median of 15+) |
| `all_runs_ms` | each run, in order, so the noise is visible |
| `output` | stdout, stripped |
| `match` | `ref` = first language's output, `ok` = equal, `MISMATCH` = the languages disagree (a red flag, never a rounding artifact: every workload is integer-exact by construction) |
| `cp3/cpp`, `cp3/rust` | time ratio of cp3 against that reference on the same task, printed on the cp3 row; lower is better |
| `peak_rss_mb` | kernel `PeakWorkingSetSize` sampled from Python while the process ran (Windows); `-` when the process exits before a sample lands, and on other platforms |
| `binary_kb` | size of the built executable after stripping a **copy** where the toolchain `strip` applies; the binary that ran is never modified. Go rows are as-built (GNU strip does not cleanly shrink Go binaries); Node/Python rows are `-` |
| `note` | build cache state, failures, and any diagnostic standing in for a timing |

Server table adds `variant` (e.g. Express vs zero-dep `node:http`), `boot_ms`
(process start to first `200` on `/healthz`), `p50_ms`/`p95_ms` (per-request
round trips on one persistent keep-alive connection) and `req/s` (successful
requests over the load window at fixed concurrency). For servers the
`cp3/cpp` and `cp3/rust` columns are time-per-request ratios (lower is better
for cp3), computed from the throughputs.

The summary table at the end collects one ratio pair per task and prints a
geomean footer over every runnable pair.

## The performance gate

The runner prints a WARN line for every runnable row where cp3 is more than
1.2x slower than cpp or rust (time ratio above 1.2). The D64 target is cp3
within about 5% of the native pack for the native-type tiers; 1.2x is the
regression tripwire, not the goal. `--strict` makes the runner exit 1 when any
WARN printed, so CI or a wave landing can gate on it.

The gate deliberately fires on rows where the beta-one compiler is genuinely
behind (see the labeled run below): those WARN lines are the performance
wave's checklist, and they should only ever become shorter and quieter.

## The fairness contract

- One entry per language per task, in the most optimized idiomatic form that
  does the same work. No per-language hand-tuning beyond idiomatic style; the
  sources are near-identical by construction, and the shared output oracle
  makes equality mechanical.
- Pinned release configurations: `g++ -std=c++20 -O2`; `rustc -O`; `go build`
  (defaults); Node and Python run their scripts directly; `cp3c build`
  (default profile, no release/LTO flags) and the compiled binary is what
  runs. No debug builds anywhere, for any language.
- Case-conversion fairness note (`string-churn`): C++ uses explicit ASCII
  fast-path loops and Go uses byte-level `toUpperASCII`/`toLowerASCII`
  helpers, because locale-aware `std::toupper` / `strings.ToUpper` would
  compare Unicode tables, not language speed. JS, Python and cp3 use their
  built-in `toUpperCase`/`toLowerCase`, which special-case ASCII and are the
  idiomatic choice in those runtimes. Both compiled entries carry an inline
  `Fairness note` comment saying the same thing.
- cp3 has no argv/stdin/env surface in the beta-one spec, so `string-churn`'s
  scale is a source constant (`ITERATIONS`). For any scale other than the
  committed default the runner substitutes **only** that one constant into a
  generated copy under `bench/.build/` and builds the copy; the committed
  source is never rewritten. The machinery can be deleted when cp3 gains an
  argv surface.
- Workload sizes live in the sources as constants and are stated in each
  task README; the runner never scales a workload behind a language's back.

## Honest methodology caveats

* **One machine, loopback only, no TLS, empty request bodies.** These numbers
  compare implementation shapes on Windows 11 with MinGW g++; they are not a
  review of the languages in the abstract.
* **Persistent keep-alive connections, not fresh connections per request.**
  Real load generators (wrk, ab, hey) do the same by default. This is also
  self-defense: an earlier fresh-connection version of this suite drove
  Windows to 16,309 of 16,384 ephemeral ports sitting in TIME_WAIT, at which
  point new connections fail with `WSAEADDRINUSE` even when the server is
  perfectly healthy. The runner prints a warning when TIME_WAIT is high, but
  if you ran another fresh-connection tool a minute ago, wait a bit and rerun
  for clean numbers.
* **Node JIT warmup.** Servers get 100 warmup requests before the latency and
  throughput phases. Compute entries have no in-process warmup possible (each
  rep is a fresh process), so `best_ms` is the best of `--reps` runs to smooth
  over V8's warmup; `all_runs_ms` shows the spread, and the first run of a
  fresh binary can include an antivirus scan.
* **Python GIL.** `compute-multi` uses worker *processes*; `compute-single` is
  pure CPython bytecode, no JIT. Being far behind the compiled languages there
  is the honest result, not an implementation trick.
* **Process startup is included everywhere.** `boot_ms` includes process
  spawn plus the first request; compute wall time includes process start.
  Fresh process starts on this box can run through Windows Defender's
  real-time scan, which is why some boot and first-run numbers spike.
* **Client overhead is in every latency number** (Python's `http.client`, one
  request at a time, no pipelining), so read p50/p95 as an upper bound on
  server think time plus one loopback round trip. Compare rows to each other,
  not to absolute expectations.
* **`peak_rss_mb` is a true kernel peak but it is sampled.** The runner polls
  the process's `PeakWorkingSetSize` while it lives; if a process exits before
  the first sample lands, the row honestly says `-`. Non-Windows platforms
  report `-` (no psutil dependency is used or wanted).
* **`binary_kb` measures a stripped copy, never the artifact that ran.** When
  the toolchain `strip` is missing or rejects the file, the as-built size is
  reported; the Go rows are the standing example.
* **Versions are pinned per run.** The header prints the exact toolchain
  versions used, and the labeled run below records them again.

## Labeled run (2026-10-18)

Machine: Intel Core i7-12700H (12th Gen, 6P+8E), 20 logical CPUs, Windows 11
(10.0.26200). Toolchains: g++ 16.1.0 (MinGW-W64 UCRT), rustc 1.95.0, go
1.26.4, node v24.15.0, python 3.14.5, cp3c 0.1.0-beta.1 (the compiler binary
in `build/bin/` that day, SHA-256
`C8864FCEF7801EA102D0BA8C87A7744525FC9C9B5FAA160BD7D02344CD770D04`, verified
unchanged across both runs below). Command: `python benchmarks/run_benchmark.py
--reps 2 --hello-reps 15 --results benchmarks/bench/results/2026-10-18T0705-local`
from the repo root, with `bench/.build/` cleared immediately before, so every
native build below is a fresh compile. The raw file is committed verbatim at
`bench/results/2026-10-18T0705-local/run.txt` (with `meta.txt` beside it); the
`[run ] ...` progress lines are removed here for readability, everything else
is verbatim output.

```
benchmarks bench suite -- 2026-10-18T07:05:38
machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | 20 logical CPUs | Windows-11-10.0.26200-SP0
runner:  python 3.14.5 (C:\Program Files\Python314\python.exe)
  g++     g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0
  rustc   rustc 1.95.0 (59807616e 2026-04-14)
  go      go version go1.26.4 windows/amd64
  node    v24.15.0
  python  Python 3.14.5
  cp3c    cp3c 0.1.0-beta.1
every number below is a wall-clock measurement from this run; load phases use persistent keep-alive connections (one per client)

== health-server ==
lang    variant                         build_ms  boot_ms   p50_ms   p95_ms     req/s  cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
----------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp     hand-rolled sockets (Winsock2)      2242    542.2     0.06     0.10   12530.8        -         -         5.3        60  warmup 100, 300 latency reqs, 8 conns x 3s
rust    std::net, no crates                  313    544.5     0.07     0.13   12888.0        -         -         4.5       174  warmup 100, 300 latency reqs, 8 conns x 3s
go      net/http                           10906    516.5     0.08     0.11    9001.1        -         -        18.1      6205  warmup 100, 300 latency reqs, 8 conns x 3s
js      express (installed)                    -    522.1     0.13     0.26    8645.3        -         -        62.7         -  warmup 100, 300 latency reqs, 8 conns x 3s
python  http.server (threaded)                 -    518.2     0.14     0.24    9488.6        -         -        23.6         -  warmup 100, 300 latency reqs, 8 conns x 3s
cp3     -                                     15        -        -        -         -        -         -           -         -  not runnable yet: main.cp3:24:13: error: CP3002: codegen: CallExpr ('call to function 'createServer' not declared in this unit') is not implemented in M1

== compute-single == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           425       152 181/152              131434424                  ref            -         -         4.0        19  
rust          163       134 214/134              131434424                  ok             -         -         4.0       122  
go            557       197 313/197              131434424                  ok             -         -         5.6      1750  
js              -      1075 1075/1109            131434424                  ok             -         -        38.4         -  
python          -     10128 10128/12886          131434424                  ok             -         -        11.6         -  
cp3          2371      1154 1194/1154            131434424                  ok         7.59x     8.60x         4.2       107  

== compute-single-int == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           483       142 188/142              131434424                  ref            -         -         4.0        19  
rust          356       132 216/132              131434424                  ok             -         -         3.6       123  
go          13574       195 314/195              131434424                  ok             -         -         5.5      1750  
js              -      1138 1197/1138            131434424                  ok             -         -        36.8         -  
python          -      9540 9540/10249           131434424                  ok             -         -        11.5         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:25:10: error: CP3002: codegen: Identifier ('returned value outside M1') is not implemented in M1

== compute-multi == expected = 131434424 (same total split across all cores)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           770        26 58/26                131434424                  ref            -         -         5.3        21  
rust          316        18 92/18                131434424                  ok             -         -         4.7       154  
go            607        28 142/28               131434424                  ok             -         -         7.1      1759  
js              -       275 275/285              131434424                  ok             -         -       281.8         -  
python          -      1583 1642/1583            131434424                  ok             -         -        21.0         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:34:15: error: CP3002: codegen: CallExpr ('parallel.*') is not implemented in M1

== hello-world == expected = Hello, world! | spawn -> exit wall time, best and median of 15 runs
lang     build_ms   best_ms median_ms all_runs_ms          output           match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           459       8.6       9.2 41/10/9/10/9/9/10/10/9/9/9/9/10/9/9 Hello, world!    ref            -         -         4.5        19  
rust          163       6.2       6.6 42/8/7/7/8/8/7/6/7/6/6/6/6/7/7 Hello, world!    ok             -         -         4.0       122  
go            573       7.6       8.2 135/10/10/8/8/8/9/8/8/8/8/9/9/8/9 Hello, world!    ok             -         -         5.7      1750  
js              -      44.4      47.8 49/48/48/46/47/47/49/51/47/44/46/53/48/48/51 Hello, world!    ok             -         -        35.1         -  
python          -      26.7      29.0 41/29/27/29/30/29/28/29/28/29/30/29/28/28/28 Hello, world!    ok             -         -        11.5         -  
cp3            51       6.2       7.9 21/8/8/10/8/8/8/8/7/8/7/8/6/7/7 Hello, world!    ok         0.73x     1.00x         4.6       106  

== string-churn == expected = 852167 | scale: default (50000 iterations)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           553      1578 1608/1578            852167                     ref            -         -         4.4        23  
rust          276       228 421/228              852167                     ok             -         -         3.9       165  
go            801      2189 2369/2189            852167                     ok             -         -        58.3      1764  
js              -      2031 2031/2035            852167                     ok             -         -        45.0         -  
python          -      1440 1440/1455            852167                     ok             -         -        11.5         -  
cp3          6488      1681 1716/1681            852167                     ok         1.07x     7.38x         5.0       115  

== word-frequency == expected = 37425343
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           667        50 81/50                37425343                   ref            -         -         4.1        23  
rust          292        50 75/50                37425343                   ok             -         -         3.8       130  
go            553        36 117/36               37425343                   ok             -         -         5.6      1762  
js              -       180 180/204              37425343                   ok             -         -        37.0         -  
python          -       531 541/531              37425343                   ok             -         -        11.5         -  
cp3            12         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:27:1: error: CP3302: cannot find type name 'Map'

== object-churn == expected = 90641269
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           574       118 149/118              90641269                   ref            -         -        61.8        20  
rust          194       115 338/115              90641269                   ok             -         -        61.4       124  
go            503        61 173/61               90641269                   ok             -         -        45.2      1751  
js              -       330 361/330              90641269                   ok             -         -       247.1         -  
python          -      1543 1561/1543            90641269                   ok             -         -       235.0         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:17:1: error: CP3002: not implemented yet: class declarations

== array-growth == expected = 200000010000000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           508       108 143/108              200000010000000            ref            -         -       260.8        20  
rust          187       121 143/121              200000010000000            ok             -         -       260.6       124  
go            553       249 380/249              200000010000000            ok             -         -       615.8      1750  
js              -       333 333/334              200000010000000            ok             -         -       353.1         -  
python          -      1584 1599/1584            200000010000000            ok             -         -       830.3         -  
cp3            12         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:15:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1

== closure-capture == expected = 16666688333331 998521500000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           716        85 115/85               16666688333331 998521500000 ref            -         -       182.1        21  
rust          216        15 99/15                16666688333331 998521500000 ok             -         -         3.6       125  
go            560        20 134/20               16666688333331 998521500000 ok             -         -         5.6      1752  
js              -       674 693/674              16666688333331 998521500000 ok             -         -       293.1         -  
python          -      1240 1242/1240            16666688333331 998521500000 ok             -         -        11.9         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:20:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1

== interface-dispatch == expected = 37999986 66000000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           746       134 166/134              37999986 66000000          ref            -         -       111.4        22  
rust          258       110 366/110              37999986 66000000          ok             -         -        90.6       126  
go            529        48 161/48               37999986 66000000          ok             -         -        52.4      1754  
js              -       467 477/467              37999986 66000000          ok             -         -       388.7         -  
python          -      1456 1458/1456            37999986 66000000          ok             -         -       236.2         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:18:1: error: CP3207: syntax error: expected ';'

== generics-mono == expected = 998986007 998986007
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           447        19 51/19                998986007 998986007        ref            -         -         4.0        19  
rust          180        20 92/20                998986007 998986007        ok             -         -         4.0       124  
go            545        21 142/21               998986007 998986007        ok             -         -         5.5      1750  
js              -        63 67/63                998986007 998986007        ok             -         -        40.2         -  
python          -       530 530/557              998986007 998986007        ok             -         -        11.5         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:14:1: error: CP3207: syntax error: expected ';'

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
health-server                -          -
compute-single           7.59x      8.60x
compute-single-int           -          -
compute-multi                -          -
hello-world              0.73x      1.00x
string-churn             1.07x      7.38x
word-frequency               -          -
object-churn                 -          -
array-growth                 -          -
closure-capture              -          -
interface-dispatch           -          -
generics-mono                -          -
------------------------------------------
geomean over runnable rows     1.80x (n=3)     3.99x (n=3)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; tripwire: WARN above 1.2x
WARN compute-single: cp3 1154 ms is 7.59x cpp (152 ms), gate 1.2x
WARN compute-single: cp3 1154 ms is 8.60x rust (134 ms), gate 1.2x
WARN string-churn: cp3 1681 ms is 7.38x rust (228 ms), gate 1.2x
gate: 3 warning(s); rerun with --strict to fail on warnings
```

## Stress preset (2026-10-18)

The `stress` scale of `string-churn` exists to stretch the fast runtimes: at
300,000 iterations the fast pack runs for roughly 10-24 seconds, so GC cycles
and allocator behavior have time to show up instead of hiding in startup
noise. Command: `python benchmarks/run_benchmark.py --only string-churn
--churn-scale stress --reps 2 --results
benchmarks/bench/results/2026-10-18T0712-local-stress`, fresh builds. Raw
output committed at
`bench/results/2026-10-18T0712-local-stress/run.txt`.

```
benchmarks bench suite -- 2026-10-18T07:12:11
machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | 20 logical CPUs | Windows-11-10.0.26200-SP0
runner:  python 3.14.5 (C:\Program Files\Python314\python.exe)
  g++     g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0
  rustc   rustc 1.95.0 (59807616e 2026-04-14)
  go      go version go1.26.4 windows/amd64
  node    v24.15.0
  python  Python 3.14.5
  cp3c    cp3c 0.1.0-beta.1
every number below is a wall-clock measurement from this run; load phases use persistent keep-alive connections (one per client)

== string-churn == expected = 254546 | scale: stress (300000 iterations)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           510     13334 13334/13661          254546                     ref            -         -         4.4        23  
rust          231      1267 1449/1267            254546                     ok             -         -         3.9       165  
go            544     23164 23164/23399          254546                     ok             -         -       138.6      1764  
js              -     21462 23144/21462          254546                     ok             -         -        70.0         -  
python          -     10796 10796/11579          254546                     ok             -         -        11.5         -  
cp3          5264     13819 13819/13844          254546                     ok         1.04x    10.90x         4.5       115  

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
string-churn             1.04x     10.90x
------------------------------------------
geomean over runnable rows     1.04x (n=1)    10.90x (n=1)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; tripwire: WARN above 1.2x
WARN string-churn: cp3 13819 ms is 10.90x rust (1267 ms), gate 1.2x
gate: 1 warning(s); rerun with --strict to fail on warnings
```

Rust stays well under two seconds at this scale because its string handling
is simply that fast; that is the result, not a measurement problem.

## Pending cp3 entries, precisely

- `health-server`: `main.cp3:24:13: error: CP3002: codegen: CallExpr ('call to function 'createServer' not declared in this unit') is not implemented in M1`
- `compute-single-int`: `main.cp3:25:10: error: CP3002: codegen: Identifier ('returned value outside M1') is not implemented in M1`
- `compute-multi`: `main.cp3:34:15: error: CP3002: codegen: CallExpr ('parallel.*') is not implemented in M1`
- `word-frequency`: `main.cp3:27:1: error: CP3302: cannot find type name 'Map'`
- `object-churn`: `main.cp3:17:1: error: CP3002: not implemented yet: class declarations`
- `array-growth`: `main.cp3:15:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1`
- `closure-capture`: `main.cp3:20:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1`
- `interface-dispatch`: `main.cp3:18:1: error: CP3207: syntax error: expected ';'`
- `generics-mono`: `main.cp3:14:1: error: CP3207: syntax error: expected ';'`

`compute-single/cp3` and `hello-world/cp3` and `string-churn/cp3` DO run
today; they are written in the subset the current compiler implements and
their rows in the labeled run are real timings.

## Refresh policy

* **Numbers are refreshed by rerunning the suite, never hand-edited.** After
  any performance wave (any change under `src/**` that could move codegen),
  rerun `python benchmarks/run_benchmark.py` with `bench/.build/` cleared and
  paste the new verbatim block above with a dated header, plus a fresh
  `--churn-scale stress` block. The root `README.md` benchmark section is
  refreshed from the same labeled run, in the same commit.
* The dated header is the refresh trigger: if the root README and this file
  disagree on the date, something was refreshed halfway.
* Raw results live under `bench/results/<timestamp>/` and are committed;
  `bench/.build/` is machine state and is gitignored.
* The collaboration rule for the gate: a wave that lands performance work
  should reduce the number of WARN lines or the ratios behind them. If a wave
  makes a WARN worse, that is the regression the gate exists to catch.
