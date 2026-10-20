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
python benchmarks/run_benchmark.py --strict         # exit 1 on any gate WARN or BIG WARN
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
| `--strict` | exit code 1 when the performance gate prints any WARN or BIG WARN |
| `--results DIR` | tee the raw output to `DIR/run.txt` plus `DIR/meta.txt` (provenance, incl. the cp3c SHA-256) |
| `--seconds S`, `--concurrency C`, `--latency N`, `--warmup N` | server load phases (defaults 3 s, 8 connections, 300 latency requests, 100 warmup) |
| `--clients N` | client OS processes for `health-server-multi` (default 4, minimum useful 2); each owns `--concurrency` keep-alive connections |
| `--single-clients N` | client OS processes for `health-server` (default 2, the quiet-probe shape); each owns `--concurrency` `http.client` sessions |

The test harness runs the suite for you automatically (D67): after the
conformance corpus pass it invokes `benchmarks/run_benchmark.py`, streams its
output, and re-prints the performance-gate `WARN` and `BIG WARN` lines at the
end.

```bash
python tests/runner/run_tests.py                    # corpus, then the full suite
python tests/runner/run_tests.py --bench-args "--only hello-world"
python tests/runner/run_tests.py --no-bench         # corpus only
```

`--bench` is still accepted as an explicit (now redundant) request;
`--no-bench` skips the bench pass; `--json` skips it with a note so machine
output stays parseable. The harness exit code stays `max(corpus, bench)`.

The runner needs Python 3 only. Servers are launched with `subprocess.Popen`,
measured, and stopped by exact process id together with the process tree they
created (cluster or multiprocessing workers); no unrelated process is touched.

## The tasks

| task | what it measures | output (match oracle) | cp3 today |
| --- | --- | --- | --- |
| `compute-single` | total Collatz steps for 1..1,000,000, single thread | `131434424` | runs |
| `compute-single-int` | the same total in native integer types (D65 tier) | `131434424` | pending native types |
| `compute-multi` | the same total split across all cores | `131434424` | pending `parallel.*` |
| `hello-world` | spawn -> exit wall time, best and median of 15+ runs | `Hello, world!` | runs |
| `string-churn` | 8 case/slice rounds per iteration over a 4 KiB source | `852167` (50k) / `254546` (300k) | runs |
| `word-frequency` | 3M LCG picks into a 64-word hash map, folded counts | `37425343` | pending arrays (literals, indexing, push) |
| `object-churn` | 5M small objects, half die in scope, half escape | `90641269` | pending classes |
| `array-growth` | 20M pushes while summing, then pop half | `200000010000000` | pending arrays |
| `closure-capture` | map/filter/reduce over 10M + 1000 escaping closures | `16666688333331 998521500000` | pending closures |
| `interface-dispatch` | 2M shapes through `area()`, plus a direct-call control loop | `37999986 66000000` | pending classes/interfaces + arrays |
| `generics-mono` | generic `Box<T>` loop vs a hand-written concrete copy | `998986007 998986007` | pending generic classes + generic functions |
| `health-server` | `GET /healthz` -> `ok`, `GET /` -> `health-server ok`, else `404`, single core | wire contract (below) | runs (contract verified) |
| `health-server-multi` | the same contract with one worker per logical CPU in every language | wire contract (below) | runs (`createServer({ threads: os.cpus() })`) |

Every value above is re-derived independently by `bench/oracle.py`, which
re-implements every workload again in plain Python; the runner cross-checks
all languages against each other and against those oracle values (the `match`
column plus `!!` warning lines for any disagreement).

The health-server contract: every language listens on `127.0.0.1` only and
answers `GET /healthz` with `200` text/plain `ok`, `GET /` with `200`
text/plain `health-server ok`, and anything else with `404` text/plain
`not found`. The port comes from `argv[1]` (the suite assigns 18080-18085 per
language to the single-core row and 18090-18095 to the multicore row, default
8080); the cp3 entry reads `process.argv` like every other entry.

`health-server-multi` is the multicore sibling: cp3 uses
`createServer({ threads: os.cpus() })` (one event loop and one uWS app per
logical CPU), cpp and rust run one accept/handler worker per core on a shared
listener, go uses `net/http` with `GOMAXPROCS`, js uses `node:cluster`, and
python passes one listening socket to `multiprocessing` workers. The runner
drives this row from `--clients` separate client OS processes (default 4),
each with `--concurrency` raw keep-alive connections.

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
requests over the load window at fixed concurrency). The single-core row uses
`--single-clients` client processes (default 2), each with `--concurrency`
`http.client` sessions, matching the diagnosis quiet probe; the multicore row
uses `--clients` client processes, each with `--concurrency` raw-socket
keep-alive sessions. For servers the `cp3/cpp` and `cp3/rust` columns are
time-per-request ratios (lower is better for cp3), computed from the
throughputs.

The summary table at the end collects one ratio pair per task and prints a
geomean footer over every runnable pair.

## The performance gate

Every runnable task row is gated against all five references (D67). Ratios
are time ratios, lower is better for cp3:

| tier | when it fires | lines |
| --- | --- | --- |
| BIG WARN | cp3 is over 1.20x cpp or rust, or slower than go/js/python at all (a native compiler should not lose to a scripted runtime) | one per reference, starting `BIG WARN (fix this):` |
| WARN | cp3 is over 1.10x cpp or rust but under the big tier | one per reference, starting `WARN ` |

The footer counts both tiers, and `--strict` makes the runner exit 1 when any
WARN or BIG WARN fired, so CI or a wave landing can gate on it. The D64
target stays cp3 within about 5% of the native pack for the native-type
tiers; 1.20x is the regression tripwire, not the goal.

Noise floor: compute rows whose reference best is under 10 ms are not gated,
because spawn jitter at that scale can exceed the ratio itself. When cp3 is
slower than such a reference, the runner prints a `note:` line saying the
ratio is noise-scale instead. hello-world, with every reference in the
single-digit milliseconds, is the row the floor exists for.

The gate deliberately fires on rows where the beta-one compiler is genuinely
behind (see the labeled run below): those lines are the performance wave's
checklist, and they should only ever become shorter and quieter. The labeled
runs below were recorded before the tiered gate landed and still show the
older single 1.2x `WARN` format; their raw files are kept as recorded, and
the next labeled refresh will show the tier format.

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

## Labeled run (2026-10-18, post-P1)

Machine: Intel Core i7-12700H (12th Gen, 6P+8E), 20 logical CPUs, Windows 11
(10.0.26200). Toolchains: g++ 16.1.0 (MinGW-W64 UCRT), rustc 1.95.0, go
1.26.4, node v24.15.0, python 3.14.5, cp3c 0.1.0-beta.1 (the canonical binary
rebuilt after the P1 codegen wave, SHA-256
`C051967809915DAE897618FC9F5CC0D5C4516329E991DC3C190DE9CC37257432`, recorded
in `meta.txt`; verified unchanged across both runs below). Command: `python
benchmarks/run_benchmark.py --reps 2 --hello-reps 15 --results
benchmarks/bench/results/2026-10-18T0803-local` from the repo root, with
`bench/.build/` cleared immediately before, so every native build below is a
fresh compile. The raw file is committed verbatim at
`bench/results/2026-10-18T0803-local/run.txt` (with `meta.txt` beside it); the
`[run ] ...` progress lines are removed here for readability, everything else
is verbatim output.

```
benchmarks bench suite -- 2026-10-18T08:03:13
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
cpp     hand-rolled sockets (Winsock2)      1347    545.8     0.05     0.07   11524.6        -         -         5.4        60  warmup 100, 300 latency reqs, 8 conns x 3s
rust    std::net, no crates                  353    524.3     0.06     0.10   12827.6        -         -         4.6       174  warmup 100, 300 latency reqs, 8 conns x 3s
go      net/http                            1205    509.0     0.10     0.14    8424.0        -         -        55.7      6205  warmup 100, 300 latency reqs, 8 conns x 3s
js      express (installed)                    -    521.6     0.12     0.23   10746.4        -         -        63.7         -  warmup 100, 300 latency reqs, 8 conns x 3s
python  http.server (threaded)                 -    538.7     0.10     0.14   11977.5        -         -        23.4         -  warmup 100, 300 latency reqs, 8 conns x 3s
cp3     -                                     13        -        -        -         -        -         -           -         -  not runnable yet: main.cp3:24:13: error: CP3002: codegen: CallExpr ('call to function 'createServer' not declared in this unit') is not implemented in M1

== compute-single == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           411       151 163/151              131434424                  ref            -         -         4.0        19  
rust          148       130 208/130              131434424                  ok             -         -         3.6       122  
go            486       193 312/193              131434424                  ok             -         -         5.7      1750  
js              -      1081 1110/1081            131434424                  ok             -         -        38.3         -  
python          -      9622 9622/9642            131434424                  ok             -         -        11.5         -  
cp3          4944       162 180/162              131434424                  ok         1.08x     1.25x         4.1       108  

== compute-single-int == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           400       145 173/145              131434424                  ref            -         -         4.0        19  
rust          141       131 213/131              131434424                  ok             -         -         3.7       123  
go            494       196 311/196              131434424                  ok             -         -         5.5      1750  
js              -      1099 1122/1099            131434424                  ok             -         -        38.2         -  
python          -      9490 9490/9710            131434424                  ok             -         -        11.4         -  
cp3            13         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:25:10: error: CP3002: codegen: Identifier ('returned value outside M1') is not implemented in M1

== compute-multi == expected = 131434424 (same total split across all cores)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           651        23 58/23                131434424                  ref            -         -         4.8        21  
rust          274        19 67/19                131434424                  ok             -         -         4.2       154  
go            535        30 152/30               131434424                  ok             -         -         6.8      1759  
js              -       261 261/262              131434424                  ok             -         -       282.2         -  
python          -      1343 1343/1400            131434424                  ok             -         -        21.0         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:34:15: error: CP3002: codegen: CallExpr ('parallel.*') is not implemented in M1

== hello-world == expected = Hello, world! | spawn -> exit wall time, best and median of 15 runs
lang     build_ms   best_ms median_ms all_runs_ms          output           match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           414       6.7       8.8 35/10/7/7/7/7/7/8/9/8/10/10/9/9/10 Hello, world!    ref            -         -         3.0        19  
rust          139       6.0       6.2 41/6/6/6/6/6/6/6/6/7/6/6/6/8/7 Hello, world!    ok             -         -         2.6       122  
go            509       5.8       6.3 126/6/6/6/6/8/6/6/7/6/6/7/12/8/6 Hello, world!    ok             -         -         5.7      1750  
js              -      37.8      42.4 48/42/45/44/46/41/43/39/41/38/42/38/43/40/43 Hello, world!    ok             -         -        35.1         -  
python          -      22.4      23.8 30/24/24/24/23/24/28/23/23/26/23/22/24/23/27 Hello, world!    ok             -         -        11.5         -  
cp3          5336       5.9       7.7 44/7/6/6/7/7/9/8/8/8/9/8/7/8/8 Hello, world!    ok         0.89x     0.99x         3.1       106  

== string-churn == expected = 852167 | scale: default (50000 iterations)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           549      1445 1591/1445            852167                     ref            -         -         4.4        23  
rust          244       214 434/214              852167                     ok             -         -         3.9       165  
go            504      2216 2378/2216            852167                     ok             -         -        80.7      1764  
js              -      1910 1954/1910            852167                     ok             -         -        46.3         -  
python          -      1229 1253/1229            852167                     ok             -         -        11.6         -  
cp3          5292      1596 1596/1603            852167                     ok         1.10x     7.46x         4.5       115  

== word-frequency == expected = 37425343
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           588        45 74/45                37425343                   ref            -         -         4.1        23  
rust          248        49 72/49                37425343                   ok             -         -         3.8       130  
go            485        33 152/33               37425343                   ok             -         -         5.6      1762  
js              -       169 169/177              37425343                   ok             -         -        37.0         -  
python          -       509 512/509              37425343                   ok             -         -        11.5         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:27:1: error: CP3302: cannot find type name 'Map'

== object-churn == expected = 90641269
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           562       119 135/119              90641269                   ref            -         -        61.8        20  
rust          189       115 341/115              90641269                   ok             -         -        61.4       124  
go            515        52 163/52               90641269                   ok             -         -        45.8      1751  
js              -       314 341/314              90641269                   ok             -         -       303.6         -  
python          -      1405 1405/1427            90641269                   ok             -         -       235.0         -  
cp3            11         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:17:1: error: CP3002: not implemented yet: class declarations

== array-growth == expected = 200000010000000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           471        93 120/93               200000010000000            ref            -         -       215.6        20  
rust          169        95 132/95               200000010000000            ok             -         -       260.6       124  
go            504       183 276/183              200000010000000            ok             -         -       455.7      1750  
js              -       278 278/286              200000010000000            ok             -         -       363.1         -  
python          -      1458 1458/1485            200000010000000            ok             -         -       830.3         -  
cp3            12         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:15:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1

== closure-capture == expected = 16666688333331 998521500000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           628        73 105/73               16666688333331 998521500000 ref            -         -       182.1        21  
rust          195        13 114/13               16666688333331 998521500000 ok             -         -         2.7       125  
go            499        16 129/16               16666688333331 998521500000 ok             -         -         4.8      1752  
js              -       630 630/644              16666688333331 998521500000 ok             -         -       293.0         -  
python          -      1175 1208/1175            16666688333331 998521500000 ok             -         -        11.8         -  
cp3            16         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:20:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1

== interface-dispatch == expected = 37999986 66000000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           619       117 145/117              37999986 66000000          ref            -         -       111.4        22  
rust          223       102 334/102              37999986 66000000          ok             -         -        90.6       126  
go            484        39 151/39               37999986 66000000          ok             -         -        59.9      1754  
js              -       429 444/429              37999986 66000000          ok             -         -       388.3         -  
python          -      1277 1301/1277            37999986 66000000          ok             -         -       237.5         -  
cp3            15         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:18:1: error: CP3207: syntax error: expected ';'

== generics-mono == expected = 998986007 998986007
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           432        17 38/17                998986007 998986007        ref            -         -         4.3        19  
rust          154        20 93/20                998986007 998986007        ok             -         -         3.9       124  
go            476        19 139/19               998986007 998986007        ok             -         -         5.6      1750  
js              -        62 67/62                998986007 998986007        ok             -         -        40.0         -  
python          -       500 528/500              998986007 998986007        ok             -         -        11.5         -  
cp3            13         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:14:1: error: CP3207: syntax error: expected ';'

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

== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
BIG WARN (fix this): compute-single: cp3 162 ms is 1.25x rust (130 ms), tripwire 1.20x
WARN string-churn: cp3 1596 ms is 1.10x cpp (1445 ms), watch 1.10x
BIG WARN (fix this): string-churn: cp3 1596 ms is 7.46x rust (214 ms), tripwire 1.20x
BIG WARN (fix this): string-churn: cp3 1596 ms is 1.30x python (1229 ms), tripwire 1.00x
note: hello-world: cp3 5.9 ms vs go 5.8 ms is noise-scale, ratio not gated
gate: 3 BIG WARN(s), 1 WARN(s); rerun with --strict to fail on warnings
```

## Stress preset (2026-10-18, post-P1)

The `stress` scale of `string-churn` exists to stretch the fast runtimes: at
300,000 iterations the fast pack runs for roughly 10-21 seconds, so GC cycles
and allocator behavior have time to show up instead of hiding in startup
noise. Command: `python benchmarks/run_benchmark.py --only string-churn
--churn-scale stress --reps 2 --results
benchmarks/bench/results/2026-10-18T0808-local-stress`. The five non-cp3
entries take the scale as a runtime argument and reused their builds from the
labeled run above (`build: cached` in the table); the cp3 entry re-bakes the
scale into its source and was compiled fresh (4839 ms). Raw output committed
at `bench/results/2026-10-18T0808-local-stress/run.txt`.

```
benchmarks bench suite -- 2026-10-18T08:08:32
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
cpp             -     13277 13277/13451          254546                     ref            -         -         4.4        23  build: cached
rust            -      1135 1135/1156            254546                     ok             -         -         3.9       165  build: cached
go              -     20956 21081/20956          254546                     ok             -         -        84.7      1764  build: cached
js              -     20105 20105/20615          254546                     ok             -         -        71.1         -  
python          -     10147 10147/10340          254546                     ok             -         -        11.5         -  
cp3          4839     13435 13435/13439          254546                     ok         1.01x    11.83x         4.5       115  

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
string-churn             1.01x     11.83x
------------------------------------------
geomean over runnable rows     1.01x (n=1)    11.83x (n=1)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
BIG WARN (fix this): string-churn: cp3 13435 ms is 11.83x rust (1135 ms), tripwire 1.20x
BIG WARN (fix this): string-churn: cp3 13435 ms is 1.32x python (10147 ms), tripwire 1.00x
gate: 2 BIG WARN(s), 0 WARN(s); rerun with --strict to fail on warnings
```

Rust stays well under two seconds at this scale because its string handling
is simply that fast; that is the result, not a measurement problem.

## Pending cp3 entries, precisely

Snapshot 2026-10-19 (`build\bin\cp3c.exe`; one `cp3c build main.cp3` per entry
from a scratch copy; the quoted text is the compiler's own first diagnostic):

- `compute-single-int`: `main.cp3:25:10: error: CP3002: not implemented yet: this returned value` (the native-type tier, D65)
- `compute-multi`: `main.cp3:34:15: error: CP3002: not implemented yet: parallel.*`
- `word-frequency`: `main.cp3:28:3: error: CP3002: not implemented yet: this method call` (the array surface: literals, indexing, push)
- `object-churn`: `main.cp3:17:1: error: CP3002: not implemented yet: classes`
- `array-growth`: `main.cp3:19:3: error: CP3002: not implemented yet: this method call`
- `closure-capture`: `main.cp3:23:3: error: CP3002: not implemented yet: this method call`
- `interface-dispatch`: `main.cp3:20:1: error: CP3002: not implemented yet: classes` (syntax re-checked against the spec: `function` members)
- `generics-mono`: `main.cp3:23:1: error: CP3002: not implemented yet: generic functions` (also `classes` for `Box<T>`)

`health-server` left this list on 2026-10-19: it builds against the landed
`std/network` and serves the wire contract on its argv port (smoke-verified:
`/healthz` -> 200 `ok`, `/` -> 200 `health-server ok`, `/nope` -> 404
`not found`). Its timed row joins the next suite run on a quiet machine.
`compute-single/cp3`, `hello-world/cp3` and `string-churn/cp3` keep their real
timings.

Every pending line maps to a live wave: arrays (word-frequency, array-growth),
classes/interfaces (object-churn, interface-dispatch), closures
(closure-capture), generics (generics-mono), `parallel.*` (compute-multi) and
the native-type tier (compute-single-int). A pending row is not a failure: it
is the compiler's own diagnostic, captured verbatim, and the row fills in
when the feature lands without any change to the entry or the runner.

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
  should reduce the number of gate lines or the ratios behind them. If a wave
  makes a WARN or BIG WARN worse, that is the regression the gate exists to
  catch.
