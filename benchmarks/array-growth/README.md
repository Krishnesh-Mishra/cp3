# array-growth - push 20,000,000 numbers, pop half, print the sum

Dynamic-array workload, identical in every language: start from an empty
array, push `1..20,000,000` one value at a time while summing them as they are
pushed, then pop the last 10,000,000 entries back off. One line, one integer:

```
200000010000000
```

That number is the full sum `1 + 2 + ... + 20,000,000`; the pops happen after
it has been accumulated, so the value tests that every pushed element was the
right one, and the pop phase tests the array's shrink path (and keeps the
compiler from being able to skip the container entirely).

## What each entry intentionally does NOT do

No entry pre-reserves or pre-sizes the array. Doubling growth, element moves
during growth, and the final shrink are the workload. Go has no `pop()`;
reslicing (`values = values[:len(values)-1]`) is its idiomatic shrink and the
outcome is the same array state.

Sizes: 20,000,000 `i64`-sized elements is ~160 MB in the compiled entries and
in JS; the Python list holds 20M boxed integer objects and peaked around
830 MB in the labeled run. Peak RSS is reported per row in `../results.md`,
so the memory side of the story is visible next to the time.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o array-growth.exe main.cpp
./array-growth.exe

# rust
rustc -O main.rs -o array-growth.exe
./array-growth.exe

# go
go build -o array-growth.exe main.go
./array-growth.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the arrays wave lands
build/bin/cp3c.exe build benchmarks/array-growth/cp3/main.cp3
```

The expected value is re-derived independently by `../bench/oracle.py`
(`array_growth_sum`):

```bash
python benchmarks/bench/oracle.py
```

Rerun the whole tier with `python benchmarks/run_benchmark.py --only
array-growth` and compare the `output` and `match` columns.
