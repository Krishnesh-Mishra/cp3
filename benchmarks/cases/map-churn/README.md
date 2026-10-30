# map-churn - hash-map delete, reinsert and update churn

Hash-map workload with interleaved deletion, reinsertion and in-place update.
Every language uses its standard hash map with integer keys and prints one
integer.

## Workload

Constants:

```text
KEYS   = 100000
ROUNDS = 2
MOD    = 1000000007
```

Initialization:

```text
for i = 0 .. KEYS - 1:
    map[i] = i * 3 + 1
```

Then repeat `ROUNDS` times, with `r` the zero-based round number:

```text
# delete every 10th key
for i = 0; i < KEYS; i = i + 10:
    map.delete(i)

# reinsert the deleted keys with a new value
for i = 0; i < KEYS; i = i + 10:
    map[i] = i * 5 + r + 2

# update every 7th key in place
for i = 0; i < KEYS; i = i + 7:
    map[i] = map[i] + 1

# fold every key in ascending key order
for i = 0; i < KEYS; i = i + 1:
    checksum = (checksum * 31 + map[i]) % MOD
```

`checksum` starts at `0`. Every key is present again before the fold, so the
fold never reads a missing key. The fold visits keys in ascending numeric
order, not in the map's iteration order, so insertion-order differences cannot
affect the result.

One line, one integer: `942471769`.

## Why this task

It measures the hash-map path under mutation: `set`, `get`, `delete`, then
reinsertion, repeated until the table has seen real churn. It complements
`word-frequency`, which is insert-only and never deletes.

This row is also a deliberate probe of cp3's insertion-ordered `Map`: deleting
a key has to remove it from both the hash index and the insertion-order vector.
A standard-library hash map with O(1) delete can beat cp3 here, and the row is
supposed to show that honestly.

## Language notes

- cpp: `std::unordered_map<long long, long long>`; `erase`.
- rust: `std::collections::HashMap<i64, i64>`; `remove`.
- go: `map[int64]int64`; `delete`.
- js: `Map` with number keys; `delete`, `set`, `get`.
- python: `dict`; `del`, assignment, indexing.
- java: `HashMap<Long, Long>`; `remove`, `put`, `get`.
- cp3: `Map<i64, i64>` from `std/collections`; `delete`, `set`, `get` with
  the `?? ZERO` fallback for `get`'s `i64 | undefined` result. Use
  `typeSystem: "rust"` in `package.json`.

No language may use a sorted map or a pre-sized custom table: the point is the
standard hash map.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o map-churn.exe main.cpp
./map-churn.exe

# rust
rustc -C opt-level=3 -o map-churn.exe main.rs
./map-churn.exe

# go
go build -o map-churn.exe main.go
./map-churn.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/map-churn/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/map-churn/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`map_churn_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only map-churn`.
