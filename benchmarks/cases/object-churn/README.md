# object-churn - 5,000,000 small heap objects, half surviving

Allocation-heavy workload, identical in every language: create a small mutable
object per iteration for 5,000,000 iterations; each iteration's temporary is
used once and dies in scope, and every second one is also stored into a
growing array where it survives until the final fold. One line, one integer:

```
90641269
```

The exact workload, exactly as every entry implements it:

1. For `i in 0..5,000,000`: create `temp = Cell(i)`, then
   `acc = (acc + temp.value) % 1000000007`. The temporary is dead after use.
2. Every even `i` additionally creates `Cell(i)` and appends it to `kept`
   (2,500,000 survivors).
3. Fold: `checksum = acc`, then for every kept object in order,
   `checksum = (checksum * 31 + value) % 1000000007`, and print `checksum`.

## Why this shape

The task separates two allocation stories: the short-lived temporaries churn
the allocator (and each language's ability to scalarize what it can prove does
not escape), while the 2.5M survivors are real GC pressure and real memory.
The fold walks the survivors in creation order, so no language's iteration
order or object identity leaks into the output.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o object-churn.exe main.cpp
./object-churn.exe

# rust
rustc -O main.rs -o object-churn.exe
./object-churn.exe

# go
go build -o object-churn.exe main.go
./object-churn.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the classes wave lands
build/bin/cp3c.exe build benchmarks/cases/object-churn/cp3/src/index.cp3
```

The expected value is re-derived independently by `../../runner/oracle.py`
(`object_churn_checksum`) with no class instantiation at all, only the same
arithmetic, so a language entry can only match it by doing the same work:

```bash
python benchmarks/runner/oracle.py
```

Rerun the whole tier with `python benchmarks/runner/run_benchmark.py --only
object-churn` and compare the `output` and `match` columns.
