# compute-single-int - total Collatz steps for 1..1,000,000, native integers

The same deterministic workload as `../compute-single/`, but this tier is about
integer representation: every entry does the whole Collatz count in its
language's native machine-integer type, never in a floating-point double.
Identical output in every language, one line, one integer:

```
131434424
```

## What each entry uses

| language | arithmetic type | notes |
| --- | --- | --- |
| cpp | `long long` | signed 64-bit, `-O2` |
| rust | `i64` | signed 64-bit, `rustc -O` |
| go | `int64` | `go build` defaults |
| js | `number` | the honest JS row: every intermediate stays below 2^53, so the double-based math is exact; no BigInt, because that would measure a different machine |
| python | `int` | exact arbitrary-precision integers, no wraparound |
| cp3 | `r_'i64'` | a real Rust `i64` in the native type system, not a double |

## Why a second Collatz task exists

`../compute-single/` lets cp3 run the loop with its `number` type, which is how
the beta-one compiler executes everything today. `compute-single-int` is the
tier that D65 (native type systems) unlocks: the cp3 entry is written to the
frozen spec with the inline raw type prefix `r_'i64'` and is deliberately not
runnable until that wave lands, so the row never pretends a double result is a
native-integer result. The five other languages run today.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o compute-single-int.exe main.cpp
./compute-single-int.exe

# rust
rustc -O main.rs -o compute-single-int.exe
./compute-single-int.exe

# go
go build -o compute-single-int.exe main.go
./compute-single-int.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the native type system wave lands
build/bin/cp3c.exe build benchmarks/compute-single-int/cp3/main.cp3
```

The expected value is re-derived independently by `../bench/oracle.py`
(`collatz_total`), which re-implements the workload in plain Python:

```bash
python benchmarks/bench/oracle.py
```

Rerun the whole tier with `python benchmarks/run_benchmark.py --only
compute-single-int` and compare the `output` and `match` columns.
