# interface-dispatch - virtual area() vs a direct-call control loop

Dynamic-dispatch workload, identical in every language: fill an array with
2,000,000 shapes of three concrete types (Circle, Square, Rect) behind a
shared abstract type, sum every `area()` through that shared call site, then
repeat the same amount of arithmetic as direct calls on a concrete type. One
line of output, two integers:

```
37999986 66000000
```

## The exact shape

The shape at index `i` cycles with `i % 3`:

| index | type | parameters | area |
| --- | --- | --- | --- |
| `i % 3 == 0` | Circle | radius `i % 5 + 1` | `3 * r * r` |
| `i % 3 == 1` | Square | side `i % 4 + 1` | `s * s` |
| otherwise | Rect | `i % 3 + 1` by `i % 6 + 2` | `w * h` |

The first total is every shape's area through the shared call site. The second
total is a fresh array of 2,000,000 Circles with radius `i % 5 + 1`, summed
with direct concrete calls. Both loop bodies do the same integer arithmetic;
the difference is purely how the method call is dispatched, and the timing gap
between the two loops is the dispatch cost each runtime pays.

## Language notes

- C++: `virtual` base class, `std::unique_ptr<Shape>` array; the control loop
  calls the non-virtual `Circle::area()` directly.
- Rust: `trait Shape` and `Vec<Box<dyn Shape>>`; the control loop is a
  `Vec<Circle>` and gets static dispatch.
- Go: `interface Shape`; the control loop uses a concrete `Circle` value
  receiver, which compiles to a direct call.
- JS: three classes; the shared call site is polymorphic for the JIT (inline
  cache over 3 maps) while the control loop is monomorphic.
- Python: three `__slots__` classes; attribute lookup and bound-method calls
  are the dispatch cost.
- cp3: interfaces and classes use the landed classes/interfaces surface; the
  entry builds and prints the same two checksums as the others.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o interface-dispatch.exe main.cpp
./interface-dispatch.exe

# rust
rustc -O main.rs -o interface-dispatch.exe
./interface-dispatch.exe

# go
go build -o interface-dispatch.exe main.go
./interface-dispatch.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the classes/interfaces wave lands
build/bin/cp3c.exe build benchmarks/cases/interface-dispatch/cp3/src/index.cp3
```

Both expected values are re-derived independently by `../../runner/oracle.py`
(`interface_dispatch_totals`), which computes the same sums with no objects at
all:

```bash
python benchmarks/runner/oracle.py
```

Rerun the whole tier with `python benchmarks/runner/run_benchmark.py --only
interface-dispatch` and compare the `output` and `match` columns.
