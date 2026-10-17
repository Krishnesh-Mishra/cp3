# cp3 competitions — one small task, six languages

Cross-language comparison suite (decision D60): the same tiny task implemented
side by side in **cp3, C++, Rust, Go, JavaScript (Node) and Python**, in
near-identical readable sources, plus a dependency-free runner that builds,
starts, measures and stops everything on this machine and prints comparison
tables.

Everything is measured, nothing is estimated. Every number in every table
came from a real process the runner started and stopped itself
(`bench/run_suite.py`). Toolchains that are not on `PATH` are skipped with a
printed note; cp3 entries whose compiler features have not landed yet are
attempted like everything else and reported with the compiler's own `CP3xxx`
diagnostic instead of a timing — when a feature lands, the row fills in by
itself.

This suite is featured in the repository's root `README.md`; every number is
a real measurement, and re-running the suite refreshes them.

## Layout

```
competitions/
  README.md                this file: contract, runner usage, methodology, sample run
  health-server/           HTTP liveness server: GET /healthz -> 200 "ok"
    cpp/main.cpp             hand-rolled HTTP over sockets, no deps
    rust/main.rs             std::net only, no crates
    go/main.go               net/http
    js/main.mjs              node:http, zero-dependency (always present)
    js/express/              optional Express variant + package.json
    python/main.py           stdlib http.server (threaded)
    cp3/main.cp3             std/network per the frozen spec (pending)
  compute-single/          single-threaded integer workload; same entries per language
  compute-multi/           the same workload split across hardware_concurrency() workers
  bench/
    run_suite.py             the runner (Python 3 stdlib only, no packages)
    run.ps1                  tiny PowerShell wrapper that forwards arguments
    .build/                  binaries + server logs written by the runner (gitignored)
```

Each task folder has its own README with the exact per-language build/run
commands and its verification record.

## How to run

```bash
# everything, defaults (servers + both compute tasks)
python examples/competitions/bench/run_suite.py

# just one section
python examples/competitions/bench/run_suite.py --only compute-single

# longer/stressier: 5 s throughput window, 16 clients, 3 compute repetitions
python examples/competitions/bench/run_suite.py --seconds 5 --concurrency 16 --reps 3

# a specific compiler build
python examples/competitions/bench/run_suite.py --cp3c build\scratch\async\bin\cp3c.exe
```

From `examples/competitions/bench` in PowerShell the wrapper is enough:
`.\run.ps1 --only compute-multi`.

Flags: `--only health-server,compute-single,compute-multi` | `--seconds S`
(throughput window, default 3) | `--concurrency C` (default 8) | `--latency N`
(latency requests, default 300) | `--warmup N` (default 100) | `--reps N`
(compute runs, best reported, default 2) | `--cp3c path` (default
`build/bin/cp3c.exe`).

The runner needs Python 3 only (it is stdlib-only). It never stops a process
it did not start: servers are launched with `subprocess.Popen`, measured, and
then `process.kill()`ed by exact id.

## The three tasks

**health-server** — every language listens on 127.0.0.1 and answers:

* `GET /healthz` -> `200` text/plain `ok`
* `GET /` -> `200` text/plain `health-server ok`
* anything else -> `404` text/plain `not found`

Port comes from `argv[1]` (default 8080). The cp3 entry uses a `const` port
(the beta-one std spec has no argv surface), so if it ever runs, that is 8080.

**compute-single** — total Collatz steps for every start value in
1..1,000,000, printed as one integer: **131434424**. Integers only, so there
are no float-formatting differences; every intermediate value stays below
2^53, so even the double-based JS and cp3 entries hold it exactly.

**compute-multi** — the same total, split across `hardware_concurrency`
workers as contiguous chunks whose partial sums are merged: cpp/rust
`std::thread`, go goroutines + WaitGroup, js `worker_threads`, python
`ProcessPoolExecutor` (processes, not threads — the GIL makes CPU-bound
threads pointless), cp3 `parallel.reduce` per spec §13.

## Reading the tables

Server table:

| column | meaning |
| --- | --- |
| `variant` | implementation shape (e.g. the js row says whether the Express variant or the zero-dep `node:http` variant ran) |
| `build_ms` | fresh compile (`g++ -O2`, `rustc -O`, `go build`, `cp3c build`); `-` = no build step, or the binary was already up to date |
| `boot_ms` | process start to first `200` on `/healthz`, polled every 5 ms — process spawn included |
| `p50_ms` / `p95_ms` | per-request round trips on ONE persistent keep-alive connection: 300 requests after 100 warmup requests |
| `req/s` | successful requests divided by elapsed time over the load window (8 concurrent keep-alive clients, 3 s by default) |
| `note`, failure line | configuration used, and how many requests failed during measurement |

Compute tables: `build_ms`; `best_ms` (best of `--reps` wall-clock runs);
`all_runs_ms` (each run, in order, so you can see the noise); `output` (stdout
stripped); `match` (`ref` = first language's output, `ok` = equal, `MISMATCH`
= the languages disagree — a red flag, not a rounding thing).

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
  throughput phases. Compute entries have no in-process warmup possible
  (each rep is a fresh process), so `best_ms` is the best of `--reps` runs to
  smooth over V8's warmup; `all_runs_ms` shows the spread.
* **Python GIL.** `compute-multi` uses worker *processes*; `compute-single` is
  pure CPython bytecode, no JIT — being far behind the compiled languages
  there is the honest result, not an implementation trick.
* **Process startup is included everywhere.** `boot_ms` includes process
  spawn plus the first request; compute wall time includes process start
  (~5-15 ms native, more for Node/Python). On this box, fresh process starts
  run a uniform ~500 ms through Windows Defender's real-time scan, which is
  why all five servers' boot times cluster there.
* **Client overhead is in every latency number** (Python's `http.client`, one
  request at a time, no pipelining), so read p50/p95 as an upper bound on
  server think time plus one loopback round trip. Compare rows to each other,
  not to absolute expectations.
* **Build times are per-run and include whatever the tool caches.** The
  sample below started with `.build/` cleared, so all five native builds are
  real compiles.
* **Versions are pinned per run.** The header prints the exact toolchain
  versions used; the sample below records them again for the record.

## Sample run (2026-10-17)

Machine: Intel Core i7-12700H (12th Gen, 6P+8E), 20 logical CPUs, Windows 11
(10.0.26200). Toolchains: g++ 16.1.0 (MinGW-W64 UCRT), rustc 1.95.0, go
1.26.4, node v24.15.0, python 3.14.5, cp3c 0.1.0-beta.1 (the compiler binary
in `build/bin/` that day). Command: `python
examples/competitions/bench/run_suite.py` from the repo root, with
`bench/.build/` cleared immediately before. The `[run ] ...` progress lines are
removed; everything else is verbatim runner output.

```
examples/competitions bench suite -- 2026-10-17T13:03:20
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
lang    variant                 build_ms  boot_ms   p50_ms   p95_ms     req/s  note
----------------------------------------------------------------------------------------------------
cpp     hand-rolled sockets (Winsock2)      1328    513.8     0.06     0.13   10498.1  warmup 100, 300 latency reqs, 8 conns x 3s
rust    std::net, no crates          398    529.5     0.05     0.10    9674.8  warmup 100, 300 latency reqs, 8 conns x 3s
go      net/http                    1311    509.5     0.09     0.13    9284.1  warmup 100, 300 latency reqs, 8 conns x 3s
js      express (installed)            -    527.2     0.14     0.29    9335.5  warmup 100, 300 latency reqs, 8 conns x 3s
python  http.server (threaded)         -    529.9     0.11     0.21    9491.2  warmup 100, 300 latency reqs, 8 conns x 3s
cp3     -                             14        -        -        -         -  not runnable yet: main.cp3:24:13: error: CP3002: codegen: CallExpr ('call to function 'createServer' not declared in this unit') is not implemented in M1

== compute-single == expected total Collatz steps for 1..1,000,000 = 131434424
lang     build_ms   best_ms all_runs_ms          output       match   note
----------------------------------------------------------------------------------------------------
cpp           412       147 195/147              131434424    ref
rust          161       132 184/132              131434424    ok
go            500       203 416/203              131434424    ok
js              -      1133 1141/1133            131434424    ok
python          -     10132 10169/10132          131434424    ok
cp3          4355      1158 1221/1158            131434424    ok

== compute-multi == expected total Collatz steps for 1..1,000,000 = 131434424
lang     build_ms   best_ms all_runs_ms          output       match   note
----------------------------------------------------------------------------------------------------
cpp           643        24 80/24                131434424    ref
rust          276        19 70/19                131434424    ok
go            569        33 237/33               131434424    ok
js              -       262 278/262              131434424    ok
python          -      1337 1388/1337            131434424    ok
cp3            12         - -                    -            -       not runnable yet: main.cp3:34:15: error: CP3002: codegen: CallExpr ('parallel.*') is not implemented in M1
```

## Pending cp3 entries, precisely

* `health-server/cp3/main.cp3` — cannot run yet: `std/network` has not landed;
  the compiler stops at `CP3002: codegen ... 'createServer' not declared in
  this unit`. The source follows the frozen API in STDLIB-README "network".
* `compute-multi/cp3/main.cp3` — cannot run yet: `parallel.*` codegen is not
  implemented (`CP3002: codegen: CallExpr ('parallel.*') is not implemented in
  M1`). The source follows spec §13's `parallel.reduce` shape.

`compute-single/cp3/main.cp3` DOES run today; it is written in the subset the
current compiler implements (functions, `while`, `if`/`else`, `%`,
arithmetic) and produces the same 131434424 as the other five languages.
