# closure-capture - map/filter/reduce plus an array of escaping closures

Closure-heavy workload in two phases over a 10,000,000-element source. One
line of output, two integers:

```
16666688333331 998521500000
```

## Phase A: map, filter, reduce with capturing closures

A source of `0, 1, ..., 9,999,999` is transformed with three closures that
capture `offset = 7` and `cap = 3`:

- map: `x -> x + 7`
- filter: keep values divisible by 3
- reduce: sum the survivors

The first total is `sum(x + 7)` over every `x` where `(x + 7) % 3 == 0`.

## Phase B: closures that outlive their creating scope

Build an array of 1,000 closures, each capturing its own `key` (the closure is
`x -> x * key + 7`). The closures escape the loop that created them; every one
is then invoked 3,000 times with its own index as the argument, for a second
total. The creating loop is finished before any invocation happens, so no
implementation can keep the captured state on the stack.

Both totals stay below 2^53 in every language, so even double-based JS and the
current cp3 `number` type can represent them exactly.

## Language notes

- JS uses `Array.prototype.map/filter/reduce` on an array; the arrows capture
  `offset` and `cap` from the enclosing scope. This is the idiomatic shape and
  the engine sees one call site per phase.
- Python runs `map`/`filter` with capturing lambdas and sums the result; each
  of the 10M elements therefore costs Python-level call overhead, and that is
  the honest CPython closure cost, not a bug. Phase B uses real closures made
  by a factory function, so `key` is a free variable, not a default argument.
- C++ composes the standard algorithms (`std::transform`, `std::copy_if`,
  `std::accumulate`) with lambdas; escaping closures live in a
  `std::vector<std::function<...>>`.
- Rust uses a lazy iterator chain (no intermediate allocation) and
  `Vec<Box<dyn Fn(i64) -> i64>>` for the escaping phase.
- Go has no map/filter helpers in the standard library, so the three closures
  are invoked directly in a loop; every closure escapes to the heap either way.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o closure-capture.exe main.cpp
./closure-capture.exe

# rust
rustc -O main.rs -o closure-capture.exe
./closure-capture.exe

# go
go build -o closure-capture.exe main.go
./closure-capture.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the closures wave lands
build/bin/cp3c.exe build benchmarks/cases/closure-capture/cp3/src/index.cp3
```

Both expected values are re-derived independently by `../../runner/oracle.py`
(`closure_capture_totals`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the whole tier with `python benchmarks/runner/run_benchmark.py --only
closure-capture` and compare the `output` and `match` columns.
