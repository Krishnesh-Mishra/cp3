# generics-mono - generic Box<T> loop vs a hand-written concrete copy

The same 2,000,000-iteration sum is run twice in the same program: once
through a generic wrapper type plus a generic function, once through a
hand-written concrete type. One line of output, two integers:

```
998986007 998986007
```

Both totals are `sum(0..2,000,000) % 1000000007`, computed by two code paths
that must agree; the interesting reading is the intra-language ratio between
the two timings, not the absolute time.

## The exact shape

- Generic path: create `Box(i)` (a generic wrapper holding a value), call a
  generic `sumBox(...)` on it, fold the returned value into an accumulator
  with `% 1000000007`.
- Concrete path: create `NumberBox(i)` (a normal hand-written class/struct
  holding the same integer) and fold `box.get()` in the same way.

Every language runs both loops in one process, so allocator state, GC warmth
and CPU frequency are shared between the two halves and the comparison is as
fair as a single process can make it.

## Language notes

- C++: `template <typename T> struct Box` plus `template <typename T> T
  sumBox(...)`; the concrete struct is a separate hand-written type.
- Rust: `Boxed<T>` generic struct and a generic `sum_box<T>` function; the
  concrete struct is separate; the name `Boxed` avoids colliding with `std::Box`.
- Go: `Box[T any]` and `sumBox[T any]`; Go instantiates generics per type at
  compile time, so the two loops compile to comparable direct code.
- JS and Python have no type parameters at all. Both loops are plain classes
  and functions in those entries and the ratio is expected to hover at 1.0;
  that is the honest reading for languages where monomorphization does not
  exist, and the row is kept so the table does not silently skip them.
- cp3: the generic class and generic function are written to the frozen spec;
  the entry is not runnable until the generics wave lands.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o generics-mono.exe main.cpp
./generics-mono.exe

# rust
rustc -O main.rs -o generics-mono.exe
./generics-mono.exe

# go
go build -o generics-mono.exe main.go
./generics-mono.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the generics wave lands
build/bin/cp3c.exe build benchmarks/generics-mono/cp3/main.cp3
```

Both expected values are re-derived independently by `../bench/oracle.py`
(`generics_mono_totals`):

```bash
python benchmarks/bench/oracle.py
```

Rerun the whole tier with `python benchmarks/run_benchmark.py --only
generics-mono` and compare the `output` and `match` columns.
