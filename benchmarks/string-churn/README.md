# string-churn - uppercase/slice/lowercase/slice over a 4 KiB source

Allocation-heavy string workload, identical in every language. Build a
4,096-character source string by repeating the seed `abcXYZ0123456789`, then
repeat the following per iteration:

1. `toUpperCase()`
2. `slice(3, length - 1)`
3. `toLowerCase()` concatenated with `slice(0, 8)`
4. `slice(2)`

Four fresh strings per round, eight rounds per iteration. After the rounds,
fold `checksum = (checksum * 31 + indexOf("c") + length) % 1000003`. One line,
one integer.

## Expected values: they depend on the scale

The workload size is the only knob. Two named scales are pinned in the suite
runner, and each has an oracle value:

| scale | iterations | expected output |
| --- | --- | --- |
| `default` | 50,000 | `852167` |
| `stress` | 300,000 | `254546` |

A custom integer scale is also accepted (`--churn-scale 12345`); at custom
scales the suite still cross-checks the five runnable languages against each
other but has no pinned oracle value to compare against.

## How the scale reaches each entry

The compiled entries, Node and CPython accept the iteration count as their one
command-line argument, so the suite passes it straight through:
`string-churn.exe 300000`. The cp3 entry has no argv surface in the beta-one
spec, so its iteration count is the source constant `ITERATIONS` (50,000 in
the committed file, matching the `default` scale). For any other scale the
runner writes a copy of the entry under `bench/.build/` with only that one
constant substituted and builds that copy; the committed source is never
rewritten. When cp3 gains an argv surface, the substitution machinery can be
deleted.

## Correctness: the fold makes it a real test

The final checksum folds in `indexOf("c")` and the current length, so a
language that produced even slightly different intermediate string contents
would print a different number. All five runnable languages print the exact
oracle values at both named scales; this was verified at a third scale (40,000
iterations, `675862`) during authoring as an extra cross-check.

## Fairness notes (why two entries look unusual)

String case conversion is where a suite can accidentally compare kitchen sinks
instead of languages:

- C++ uses explicit ASCII fast-path loops, not locale-aware `std::toupper`,
  because the tournament is not about locale tables.
- Go uses byte-level `toUpperASCII`/`toLowerASCII` helpers instead of
  `strings.ToUpper`, which allocates and handles the full Unicode table.
- JS/Python/cp3 use their built-in `toUpperCase`/`toLowerCase`, which in
  practice special-case ASCII and are the idiomatic choice; forcing a manual
  ASCII loop would fight each runtime's real string implementation.
- Slice parity: the row measures string churn, so the intermediate slices are
  owning strings in every entry. Rust and Go now materialize them
  (`to_string`/`strings.Clone`); their native slice types are zero-copy views
  that share the backing buffer and would skip one allocation per round.
  Python slices copy, and C++/cp3 `substr`/`slice` copy. V8's `String.slice`
  may return an internal thin string (a representation detail of the engine,
  observably identical); that one cannot be forced to copy without distorting
  the workload, so the JS row is read as a runtime reference rather than part
  of the owned-string parity set.

Both compiled entries carry an inline `Fairness note` comment saying the same
thing so the source explains itself.

## Run one by hand

From each language's folder:

```bash
# cpp (iterations optional, default 50000)
g++ -std=c++20 -O2 -o string-churn.exe main.cpp
./string-churn.exe 50000

# rust
rustc -O main.rs -o string-churn.exe
./string-churn.exe 50000

# go
go build -o string-churn.exe main.go
./string-churn.exe 50000

# js
node main.mjs 50000

# python
python main.py 50000

# cp3 - scale is a source constant; edit nothing, use the runner for other scales
build/bin/cp3c.exe run benchmarks/string-churn/cp3/main.cp3
```

The oracle values are re-derived independently by `../bench/oracle.py`
(`churn_checksum`):

```bash
python benchmarks/bench/oracle.py
```

Rerun the tier with `python benchmarks/run_benchmark.py --only string-churn`
and add `--churn-scale stress` for the long variant.
