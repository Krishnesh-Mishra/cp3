# cp3 benchmarks - one suite, seven languages

Cross-language comparison suite (decision D60, refreshed under D66): the same
small tasks implemented side by side in **cp3, C++, Rust, Go, JavaScript
(Node), Python and Java**, in near-identical readable sources, plus a
dependency-free runner that builds, starts, measures and stops everything on
this machine and prints comparison tables with per-row `cp3/cpp` and
`cp3/rust` ratios, measured peak memory, stripped binary size, and a
performance gate.

Compute rows use each language's native standard library. The two HTTP rows
use the framework each ecosystem recommends for a production service: Gin
(Go), Axum (Rust), Drogon (C++), Express and Fastify (JS), FastAPI/Uvicorn
(Python), Undertow (Java), and cp3's `std/network`. The dependency-free
implementations remain as fallbacks, and the runner prints which variant ran.

Everything is measured, nothing is estimated. Every number in every table
came from a real process the runner started and stopped itself. Toolchains
that are not on `PATH` are skipped with a printed note; cp3 entries whose
compiler features have not landed yet are attempted like everything else and
reported with the compiler's own `CP3xxx` diagnostic instead of a timing;
when a feature lands, the row fills in by itself, unchanged.

The recorded tables live in [`results.md`](results.md). A complete
default-scale full-suite run rewrites the latest block there automatically; a
single-task run (`--only ...`) never touches it. The repository's root
`README.md` links to the same recorded results.

## Layout

```text
benchmarks/
  cases/       # one self-contained project per task, all languages side by side
  runner/      # suite engine, wrapper, oracle, load generator, setup scripts
  .cache/      # gitignored build outputs, framework deps, Python bytecode
  README.md
  results.md
```

`cases/<task>/` is the only place benchmark sources live. The runner and its
Python tooling live in `runner/`; run everything from the repo root with
`python benchmarks/runner/run_benchmark.py`. Generated binaries, framework
dependencies and Python bytecode go under `.cache/`, which is gitignored, so
the committed benchmark tree stays source-only.

## How to run

Single entry point (Python 3 only, stdlib, no packages):

```bash
python benchmarks/runner/run_benchmark.py                  # everything, defaults
python benchmarks/runner/run_benchmark.py --only hello-world,string-churn
python benchmarks/runner/run_benchmark.py --reps 3         # compute reps (best reported)
python benchmarks/runner/run_benchmark.py --churn-scale stress
python benchmarks/runner/run_benchmark.py --cp3c build/bin/cp3c.exe
python benchmarks/runner/run_benchmark.py --strict         # exit 1 on any performance WARN or BIG WARN (CP3002 always fails)
python benchmarks/runner/run_benchmark.py --results DIR    # tee raw output to DIR
```

`run_benchmark.py` is a thin wrapper: it forwards every argument to the engine
`runner/run_suite.py`, which is the only implementation. From `benchmarks/runner`
in PowerShell, `.\run.ps1 --only word-frequency` does the same.

| flag | meaning |
| --- | --- |
| `--only T[,T...]` | run a subset of tasks (see the task table below) |
| `--reps N` | compute repetitions per entry; best is reported (default 2) |
| `--hello-reps N` | hello-world runs; best and median are reported (default 15) |
| `--churn-scale V` | string-churn scale: `default` (50,000), `stress` (300,000) or an integer |
| `--cp3c PATH` | cp3 compiler (default `build/bin/cp3c.exe`) |
| `--strict` | exit code 1 when the performance gate prints any WARN or BIG WARN; CP3002 blockers always exit 1 |
| `--results DIR` | tee the raw output to `DIR/run.txt` plus `DIR/meta.txt` (provenance, incl. the cp3c SHA-256) |
| `--seconds S`, `--concurrency C`, `--latency N`, `--warmup N` | server load phases (defaults 3 s, 8 connections, 300 latency requests, 100 warmup) |
| `--clients N` | client OS processes for `health-server-multi` (default 4, minimum useful 2); each owns `--concurrency` keep-alive connections |
| `--single-clients N` | client OS processes for `health-server` (default 2, the quiet-probe shape); each owns `--concurrency` `http.client` sessions |

The test harness runs the suite for you automatically (D67): after the
conformance corpus pass it invokes `benchmarks/runner/run_benchmark.py`, streams its
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
| `compute-single-int` | the same total in native integer types (D65 tier) | `131434424` | runs |
| `compute-multi` | the same total split across all cores | `131434424` | runs |
| `hello-world` | spawn -> exit wall time, best and median of 15+ runs | `Hello, world!` | runs |
| `string-churn` | 8 case/slice rounds per iteration over a 4 KiB source | `852167` (50k) / `254546` (300k) | runs |
| `sha256-churn` | SHA-256 over one fixed 1 KiB buffer, 60,000 times | `218229` | runs (cp3/go/js/python; cpp/rust omitted) |
| `buffer-copy` | 64 KiB byte-pattern buffer rotated, sliced and rebuilt 5,000 times | `688024` | runs |
| `typed-array-sum` | 1M `Float64Array` + `Uint8Array` fill, 32 sum rounds, byte pass | `135618465` | runs |
| `math-kernel` | 5M sqrt/floor/abs/pow/clamp points, integer-quantized fold | `177047762` | runs |
| `word-frequency` | 3M LCG picks into a 64-word hash map, folded counts | `37425343` | runs |
| `object-churn` | 5M small objects, half die in scope, half escape | `90641269` | runs |
| `array-growth` | 20M pushes while summing, then pop half | `200000010000000` | runs |
| `closure-capture` | map/filter/reduce over 10M + 1000 escaping closures | `16666688333331 998521500000` | runs |
| `interface-dispatch` | 2M shapes through `area()`, plus a direct-call control loop | `37999986 66000000` | runs |
| `generics-mono` | generic `Box<T>` loop vs a hand-written concrete copy | `998986007 998986007` | runs |
| `json-roundtrip` | 20,000 parse + stringify round trips of one 310-byte record | `664346` | runs (cp3/go/js/python; cpp/rust omitted) |
| `async-fanout` | 10,000 zero-delay timers joined at once, then 10,000 chained awaits | `99898` | runs (cp3/go/js/python; cpp/rust omitted) |
| `env-injection` | 500,000 fresh reads of one runner-injected environment variable | `133644` | runs |
| `file-roundtrip` | write, read and stat one 4 KiB temp file, 200 times | `698140` | runs |
| `path-join` | join/normalize/basename/extname one fixed segment list, 170,000 times | `425577` | runs |
| `date-format` | format 50,000 UTC instants by hand from fixed epoch milliseconds | `796897` | runs (cp3/go/js/python; rust omitted) |
| `os-probe` | query platform/CPU/home/temp/hostname 50,000 times, fold booleans | `208981` | runs (cp3/cpp/go/js/python; rust omitted) |
| `binary-trees` | full binary trees to depth 13, recursive build and check | `1348958` | runs |
| `map-churn` | 100,000-key hash map delete/reinsert/update churn | `942471769` | runs |
| `utf8-text` | 417,792-byte UTF-8 text, 32 manual decode rounds | `78156718` | runs |
| `regex-scan` | 240,000-byte text, global `[0-9]+` scan, 8 rounds | `735703090` | runs (cp3/cpp/go/js/python/java; rust omitted) |
| `json-parse-large` | 204,472-byte JSON record parsed 20 times | `613781` | runs (cp3/go/js/python/java; cpp/rust omitted) |
| `deep-recursion` | recurse to depth 5,000 for 200 rounds | `292249866` | runs |
| `exception-cost` | 1M throw/catch vs sentinel-return iterations | `446742259 446742259` | runs (cp3/cpp/js/python/java; rust/go omitted) |
| `file-big` | 8 MiB temp-file write/read, 8 rounds | `779913` | runs |
| `health-server` | `GET /healthz` -> `ok`, `GET /` -> `health-server ok`, else `404`, single core | wire contract (below) | runs (Gin/Axum/Drogon/Express/Fastify/FastAPI/Undertow) |
| `health-server-multi` | the same contract with one worker per logical CPU in every language | wire contract (below) | runs (`createServer({ threads: os.cpus() })`) |

Every value above is re-derived independently by `runner/oracle.py`, which
re-implements every workload again in plain Python; the runner cross-checks
all languages against each other and against those oracle values (the `match`
column plus `!!` warning lines for any disagreement).

`typed-array-sum` and `math-kernel` are integer-exact workloads: every
floating-point result is quantized before it enters the fold. The cp3
typed-array entry keeps the built-in `Float64Array`/`Uint8Array` storage and
fill, and reads the same buffers through `DataView`; a runtime-index
typed-array element read is typed `number | null | undefined` and does not
codegen arithmetic yet.

Eight rows deliberately omit a language. `json-roundtrip`, `async-fanout` and
`sha256-churn` omit cpp and rust: C++ `std` and Rust `std` have no JSON
parser/serializer, no timer/event-loop surface, and no SHA-256. `date-format`
and `os-probe` omit rust: Rust `std` has no timezone-aware calendar formatter
and no hostname/platform probe without `libc` or a crate. `regex-scan` omits
rust: Rust `std` has no regular-expression engine. `json-parse-large` omits cpp
and rust: neither standard library has a JSON parser. `exception-cost` omits
rust and go: neither has exceptions, and `Result`/error values are the
returned-value path, not throw/catch. A single dependency-free file would have
to vendor or hand-roll the missing stack, which compares dependencies, not the
language. The runner prints the omission reason per row (`TASK_OMIT_LANGUAGES`
in `runner/run_suite.py`) and still gates the remaining languages; each task
README states the exact rationale.

## Standard-library coverage

Each landed stdlib family has one benchmark that exercises its hot surface.
Every row below now has a cp3 timing in [`results.md`](results.md).

| std module | benchmark | coverage note |
| --- | --- | --- |
| `std/console` | `hello-world` | No separate console row. `hello-world` already writes one line through `console.log`; a tight console loop would measure pipe/terminal throughput, not language work. |
| `std/process` | `hello-world` | Process spawn-to-exit, including runtime startup. |
| `std/string` | `string-churn`, `utf8-text` | Case conversion, slicing and concatenation over a 4 KiB source; manual UTF-8 byte decode over 417,792 bytes. |
| `std/crypto` | `sha256-churn` | SHA-256 over a fixed 1 KiB buffer; cpp/rust omitted (no std hash). |
| `std/buffer` | `buffer-copy` | Allocate, rotate, slice and rebuild a 64 KiB byte buffer. |
| `std/typed-arrays` | `typed-array-sum` | `Float64Array`/`Uint8Array` fill, element reads and reduction. |
| `std/math` | `math-kernel` | sqrt/floor/abs/pow/clamp with an integer-quantized fold. |
| `std/env` | `env-injection` | Fresh lookup of one runner-injected variable. |
| `std/fs` | `file-roundtrip`, `file-big` | Write, read and stat one 4 KiB temp file; write/read an 8 MiB payload. |
| `std/path` | `path-join` | Join, normalize, basename and extname. |
| `std/date` | `date-format` | UTC component formatting and `diffMs`; rust omitted. |
| `std/os` | `os-probe` | Platform, CPU count, home, temp and hostname; rust omitted. |
| `std/json` | `json-roundtrip`, `json-parse-large` | Parse + stringify a fixed record; parse a 204,472-byte record. cpp/rust omitted. |
| `std/network` | `health-server`, `health-server-multi` | HTTP wire contract, single-core and multicore. |
| `std/collections` | `word-frequency`, `map-churn` | 3M LCG picks into a 64-word hash map; 100,000-key delete/reinsert churn. |
| `std/arrays` | `array-growth` | 20M pushes/pops. |
| timers / event loop | `async-fanout` | Zero-delay timers and chained awaits; cpp/rust omitted. |
| regex engine | `regex-scan` | Global `[0-9]+` scan over 240,000 bytes; rust omitted. |

The health-server contract: every language listens on `127.0.0.1` only and
answers `GET /healthz` with `200` text/plain `ok`, `GET /` with `200`
text/plain `health-server ok`, and anything else with `404` text/plain
`not found`. The port comes from `argv[1]` (the suite assigns 18080-18087 per
language to the single-core row and 18090-18097 to the multicore row, default
8080); the cp3 entry reads `process.argv` like every other entry.

`health-server-multi` is the multicore sibling: cp3 uses
`createServer({ threads: os.cpus() })` (one event loop and one uWS app per
logical CPU), cpp uses Drogon with one event loop per hardware thread, rust
uses Axum on Tokio's multi-threaded runtime, go uses Gin with `GOMAXPROCS`,
js uses `node:cluster` with Express and Fastify workers, python runs FastAPI
under Uvicorn with one process per CPU, and java runs Undertow with one I/O
thread per CPU. The runner drives this row from `--clients` separate client OS
processes (default 4), each with `--concurrency` raw keep-alive connections.

Each task folder has its own README with the exact workload definition, the
expected output, and the per-language build/run commands. Recorded tables live
in [`results.md`](results.md).

## Reading the tables

Compute tables:

| column | meaning |
| --- | --- |
| `build_ms` | fresh compile (`g++ -O2`, `cargo build --release`/`rustc -O`, `go build`, `javac`, `cp3c build`); `-` = no build step, or the binary was already up to date |
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

Every runnable task row is gated against all six references (D67). Ratios
are time ratios, lower is better for cp3:

| tier | when it fires | lines |
| --- | --- | --- |
| CP3002 blocker | any cp3 row fails to build with CP3002 | one per row, starting `CP3002 BLOCKER`; always exits 1 |
| BIG WARN | cp3 is over 1.20x cpp or rust, or slower than go/js/python/java at all (a native compiler should not lose to a scripted runtime) | one per reference, starting `BIG WARN (fix this):` |
| WARN | cp3 is over 1.05x cpp or rust (the D64 5% band; tightened from 1.10x by the 2026-10-19 directive) | one per reference, starting `WARN ` |
| WARN multi-core | a multi-core row (`compute-multi`, `health-server-multi`) is not FASTER than cpp (ratio above 1.00) | one dedicated line per row, starting `WARN multi-core` or `BIG WARN (fix this) multi-core` |

The footer counts both performance tiers, and `--strict` makes the runner exit
1 when any WARN or BIG WARN fired, so CI or a wave landing can gate on it. A
CP3002 blocker always exits 1, even without `--strict`: a row that cannot
compile is not a pending measurement. The D64
target stays cp3 within about 5% of the native pack for the native-type
tiers, and the WARN trigger now sits exactly on that band: anything past 5%
is a fix-me line, 1.20x is the regression tripwire. Multi-core rows carry
the stricter rule from the 2026-10-19 directive: cp3 ships TBB, so a parallel
row is expected to win against the cpp comparator outright, not tie it.

Noise floor: compute rows whose reference best is under 10 ms are not gated,
because spawn jitter at that scale can exceed the ratio itself. When cp3 is
slower than such a reference, the runner prints a `note:` line saying the
ratio is noise-scale instead. hello-world, with every reference in the
single-digit milliseconds, is the row the floor exists for.

The gate deliberately fires on rows where the beta-one compiler is genuinely
behind (see the recorded runs in [`results.md`](results.md)): those lines are
the performance wave's checklist, and they should only ever become shorter and
quieter. The historical runs in `results.md` were recorded before the tiered
gate and the CP3002 blocker landed; they still show the older single 1.2x
`WARN` format and the old `not runnable yet` rows. Their raw files are kept as
recorded, and the next full refresh will show the blocker and tier format.

## The fairness contract

- One entry per language per task, in the most optimized idiomatic form that
  does the same work. No per-language hand-tuning beyond idiomatic style; the
  sources are near-identical by construction, and the shared output oracle
  makes equality mechanical.
- Pinned release configurations: `g++ -std=c++20 -O3` for the dependency-free
  C++ fallback and CMake `Release` (MSVC) for Drogon; `cargo build --release`
  for Axum, `rustc -O` for the `std::net` fallback; `go build` (defaults);
  `javac`/`java` (default JVM settings, no `-Xmx`); Node and Python run their
  scripts directly; `cp3c build --show-runtime` (default profile, no
  release/LTO flags) and the compiled binary is what runs. `--show-runtime`
  only writes the generated C++ plus a show-only runtime mirror under
  `.cp3/build/_runtime/` (created once, never re-copied); it does not change the measured executable.
  No debug builds anywhere, for any language.
- On Windows, `rustc` normally targets `x86_64-pc-windows-msvc`. When no
  `link.exe` is visible (no MSVC linker/Windows SDK) but the official GNU
  target and MinGW `gcc` are installed, the runner passes
  `--target x86_64-pc-windows-gnu` (including the matching cargo target
  directory). Same optimized Rust source and release settings; no row is
  skipped.
- HTTP rows use the installed frameworks listed in
  `cases/health-server/README.md`; the runner falls back to the dependency-free
  entry only when a dependency is missing and prints the chosen variant.
- Case-conversion fairness note (`string-churn`): C++ uses explicit ASCII
  fast-path loops and Go uses byte-level `toUpperASCII`/`toLowerASCII`
  helpers, because locale-aware `std::toupper` / `strings.ToUpper` would
  compare Unicode tables, not language speed. JS, Python, Java
  (`toUpperCase(Locale.ROOT)`) and cp3 use their built-in
  `toUpperCase`/`toLowerCase`, which special-case ASCII and are the idiomatic
  choice in those runtimes. The compiled entries carry an inline `Fairness
  note` comment saying the same thing.
- `string-churn`'s scale is a compile-time constant (`ITERATIONS`) in the cp3
  entry, while the other entries accept it as `argv[1]`. For any scale other
  than the committed default the runner substitutes **only** that one constant
  into a generated cp3 copy under `.cache/build/` and builds the copy; the
  committed source is never rewritten.
- Workload sizes live in the sources as constants and are stated in each
  task README; the runner never scales a workload behind a language's back.
- Five rows omit a language by design: `json-roundtrip`, `async-fanout` and
  `sha256-churn` omit cpp/rust, while `date-format` and `os-probe` omit rust.
  A row compares standard-library surfaces that every entrant actually has;
  the omitted `std` lacks JSON, timers, SHA-256, a timezone formatter, or a
  hostname/platform probe. Omission is declared per task in
  `TASK_OMIT_LANGUAGES` and printed; the gate still applies to the remaining
  languages.

## Honest methodology caveats

* **One machine, loopback only, no TLS, empty request bodies.** These numbers
  compare implementation shapes on Windows 11 with MinGW g++ for the
  dependency-free C++ fallback and MSVC for Drogon; they are not a review of
  the languages in the abstract.
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
* **JVM startup and warmup.** Java compute rows include JVM startup in
  `best_ms` (each rep is a fresh process) and run with default JVM settings.
  Server rows get the same 100-request warmup as every other language, so the
  recorded throughput reflects a warm JIT; `boot_ms` includes JVM startup.
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
  versions used, and every full run records them again in `results.md`.

## Results

The recorded benchmark tables live in [`results.md`](results.md). A complete
default-scale full-suite run rewrites its latest block automatically; the
historical labeled runs are kept there verbatim.

## CP3002 status

CP3002 placeholders are fully retired. `python vendor/tools/check_cp3002.py` reports
zero markers, and every benchmark row now builds and produces a number; the
gate never prints `CP3002 BLOCKER`. A complete default-scale run reports only
performance warnings, if any.

## Refresh policy

* **Numbers are refreshed by rerunning the suite, never hand-edited.** A
  complete default-scale run (`python benchmarks/runner/run_benchmark.py`) rewrites
  the generated latest block in `results.md` automatically. A single-task run
  (`--only ...`) and a `--churn-scale stress` run never touch that block.
* After any performance wave (any change under `src/**` that could move
  codegen), clear `.cache/build/` and run the full suite once. The generated
  block records the command, timestamp, machine, toolchains and raw tables.
* The historical labeled runs in `results.md` are kept as recorded. If a
  stress-preset block is needed, run `--churn-scale stress` separately and add
  it under the historical section by hand.
* Raw results can additionally be kept under `runner/results/<timestamp>/`
  with `--results DIR`; `.cache/build/` is machine state and is gitignored.
* The root `README.md` links to `results.md` for the recorded numbers. Update
  its short headline prose only when the headline result changes.
* The collaboration rule for the gate: a wave that lands performance work
  should reduce the number of gate lines or the ratios behind them. If a wave
  makes a WARN or BIG WARN worse, that is the regression the gate exists to
  catch.
