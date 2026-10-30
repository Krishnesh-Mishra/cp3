# deep-recursion - deep call stacks and recursive call overhead

Call-stack workload. Every language recurses to a fixed depth, returns the
triangular sum, and folds that value into a checksum. The depth is deep enough
to exercise stack growth but shallow enough to fit a default native stack.

## Workload

Constants:

```text
DEPTH  = 5000
ROUNDS = 200
MOD    = 1000000007
```

The recursive function is:

```text
sumTo(0) = 0
sumTo(n) = n + sumTo(n - 1)    for n > 0
```

For each of `ROUNDS` rounds:

```text
checksum = (checksum * 31 + sumTo(DEPTH)) % MOD
```

`checksum` starts at `0`. `sumTo(5000)` is `12502500`.

One line, one integer: `292249866`.

## Why this task

It measures call overhead and stack growth at a depth that ordinary code
rarely reaches, without relying on tail-call optimization (the addition happens
after the recursive call, so no language may turn this into a loop). It
complements the flat compute loops and the allocation-heavy binary trees.

## Language notes

- cpp: plain recursive `long long sumTo(long long n)`.
- rust: plain recursive `fn sum_to(n: i64) -> i64`.
- go: plain recursive `func sumTo(n int64) int64`; Go grows goroutine stacks.
- js: plain recursive function; V8 handles 5,000 frames on the default stack.
- python: call `sys.setrecursionlimit(20000)` once at startup; the default
  1,000-frame limit is below the workload depth.
- java: plain recursive `static long sumTo(long n)`; the default stack handles
  5,000 frames.
- cp3: plain recursive `function sumTo(n: i64): i64`; the current compiler
  handles 5,000 frames (verified). Use `typeSystem: "rust"` in `package.json`.

Every round must start a fresh `sumTo(DEPTH)` call. No memoization, no
precomputed triangular number, and no manual loop replacing the recursion: the
recursive calls are the workload.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o deep-recursion.exe main.cpp
./deep-recursion.exe

# rust
rustc -C opt-level=3 -o deep-recursion.exe main.rs
./deep-recursion.exe

# go
go build -o deep-recursion.exe main.go
./deep-recursion.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/deep-recursion/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/deep-recursion/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`deep_recursion_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only deep-recursion`.
