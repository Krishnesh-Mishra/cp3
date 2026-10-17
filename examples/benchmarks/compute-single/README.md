# compute-single — total Collatz steps for 1..1,000,000

One deterministic integer workload, identical in all six languages: for every
start value in `1..1,000,000`, count the steps the Collatz sequence
(`n -> n/2` when even, `n -> 3n+1` when odd) takes to reach `1`, and print the
sum. One line of output, one integer:

```
131434424
```

Integers only — no floating-point formatting anywhere — so the outputs must
be byte-identical, and they are. Every intermediate value stays below 2^53,
so even the double-based JS and cp3 entries represent it exactly.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o compute-single.exe main.cpp
./compute-single.exe

# rust
rustc -O main.rs -o compute-single.exe
./compute-single.exe

# go
go build -o compute-single.exe main.go
./compute-single.exe

# js
node main.mjs

# python
python main.py

# cp3 — from the repo root; this one runs today
build/bin/cp3c.exe run examples/competitions/compute-single/cp3/main.cp3
```

## Verification record (2026-10-17, cp3c 0.1.0-beta.1)

All six entries were built and run, and all six printed `131434424`. That
includes the cp3 entry, which deliberately sticks to the subset the current
compiler implements (functions, `while`, `if`/`else`, `%`, arithmetic) and is
exactly what the suite's cross-check column compares (`match: ref/ok`).

Wall-clock timings from the labeled sample run in `../README.md` (best of 2
runs each, process startup included):

| language | best wall time |
| --- | --- |
| rust | 132 ms |
| cpp | 147 ms |
| go | 203 ms |
| js (node) | 1133 ms |
| cp3 | 1158 ms |
| python | 10132 ms |

Two honest notes that table invites: pure CPython bytecode is ~70x behind the
compiled pack here — that is the result, not a flaw in the entry; and cp3
lands in the same second as V8 for this loop, well ahead of Python, which is
what "fast like C++, shaped like TypeScript" is supposed to look like for
straight-line integer work. Rerun it on your machine — `python
../bench/run_suite.py --only compute-single` — before quoting any of it.
