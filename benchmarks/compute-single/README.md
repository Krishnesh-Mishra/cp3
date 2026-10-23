# compute-single - total Collatz steps for 1..1,000,000

One deterministic integer workload, identical in all six languages: for every
start value in `1..1,000,000`, count the steps the Collatz sequence
(`n -> n/2` when even, `n -> 3n+1` when odd) takes to reach `1`, and print the
sum. One line of output, one integer:

```
131434424
```

Integers only - no floating-point formatting anywhere - so the outputs must
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

# cp3 - from the repo root; this one runs today
build/bin/cp3c.exe run benchmarks/compute-single/cp3/main.cp3
```

## Verification record (2026-10-18, post-P1, cp3c 0.1.0-beta.1)

All six entries were built and run, and all six printed `131434424`. That
includes the cp3 entry, which deliberately sticks to the subset the current
compiler implements (functions, `while`, `if`/`else`, `%`, arithmetic) and is
exactly what the suite's cross-check column compares (`match: ref/ok`).

Wall-clock timings from the labeled run in `../results.md`
(`bench/results/2026-10-18T0803-local/`, best of 2 runs each, process startup
included):

| language | best wall time |
| --- | --- |
| rust | 130 ms |
| cpp | 151 ms |
| go | 193 ms |
| js (node) | 1081 ms |
| cp3 | 162 ms |
| python | 9622 ms |

The cp3 row: close behind the native pack after the P1 wave (1.08x C++ and
1.25x Rust), 6.7x faster than V8 and ≈59x faster than CPython. The remaining
native ratio is what the performance gate flags as `BIG WARN (fix this)`
(D67 tiering); closing it is the performance wave's checklist - this table is
the honest state of the beta-one codegen for number-heavy loops, not an
implementation trick. Pure CPython bytecode being ~64x behind C++ here is
also the result, not a flaw in the entry. Rerun it on your machine - `python
../run_benchmark.py --only compute-single` - before quoting any of it.
