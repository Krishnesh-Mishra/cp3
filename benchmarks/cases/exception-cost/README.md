# exception-cost - throw/catch versus a returned error value

Error-path workload. Every language runs the same predicate twice: once with a
thrown exception and once with a sentinel return value. The two paths do the
same number of iterations and take the error branch at the same rate, so the
time difference is the cost of the error mechanism.

## Workload

Constants:

```text
ITERATIONS = 1000000
MOD        = 1000000007
```

The predicate is:

```text
mayThrow(i):
    if i % 3 == 0: throw "boom"
    return i

mayReturn(i):
    if i % 3 == 0: return -1
    return i
```

First loop, the throwing path:

```text
thrown = 0
for i = 0 .. ITERATIONS - 1:
    try:
        v = mayThrow(i)
        thrown = (thrown * 31 + v) % MOD
    catch:
        thrown = (thrown * 31 + 1) % MOD
```

Second loop, the returned-value path:

```text
returned = 0
for i = 0 .. ITERATIONS - 1:
    v = mayReturn(i)
    if v < 0:
        returned = (returned * 31 + 1) % MOD
    else:
        returned = (returned * 31 + v) % MOD
```

Print both values on one line, separated by one space:

```text
446742259 446742259
```

Both values are equal by construction: the two loops fold the same value
(`1` for the error case, `i` otherwise) in the same order. The row compares
time, not output.

## Why this task

Exceptions are a core error-handling path, but their cost varies by orders of
magnitude between runtimes. This row isolates throw/catch dispatch and unwinding
against a plain branch and sentinel, with identical control flow and iteration
counts.

## Language notes

- cpp: `throw std::runtime_error("boom")`, `catch (const std::exception&)`.
- js: `throw new Error("boom")`, `catch (e)`.
- python: `raise ValueError("boom")`, `except ValueError`.
- java: `throw new RuntimeException("boom")`, `catch (RuntimeException e)`.
  Use a fresh exception per throw (the idiomatic form); do not override
  `fillInStackTrace`.
- cp3: `throw "boom"`, `catch (e)` (verified with the current compiler). Use
  `typeSystem: "rust"` in `package.json`.

Rust and Go are omitted: neither has exceptions. Implementing the same control
flow with `Result`/`error` values would compare a different mechanism (the
returned-value path), not throw/catch. The suite's omission rule is the same
one used for regex in Rust and JSON in C++/Rust.

Both loops must call the predicate on every iteration. Do not hoist the
`i % 3` check out of the loop: the branch and the call are part of the
workload.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o exception-cost.exe main.cpp
./exception-cost.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/exception-cost/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/exception-cost/cp3/src/index.cp3
```

The values are re-derived independently by `../../runner/oracle.py`
(`exception_cost_checksums`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only exception-cost`.
