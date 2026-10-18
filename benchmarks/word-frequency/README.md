# word-frequency - count 3,000,000 picks from a 64-word vocabulary

Hash-map workload, identical in every language. A pinned integer LCG
(`state = (state * 48271) % 2147483647`, starting at `state = 1`) chooses, on
each of 3,000,000 iterations, one of 64 words named `word00` ... `word63`; the
chosen word's count is incremented. Then the counts are folded into one
checksum by walking the vocabulary in fixed index order
(`checksum = (checksum * 31 + count) % 1000000007`). One line, one integer:

```
37425343
```

## Why the output is order-independent

The fold walks `word00, word01, ... word63` by name, not by whatever order the
language's hash map happens to iterate in, so map ordering can never change
the output. Every entry stores string keys in its idiomatic hash map: C++
`std::unordered_map<std::string, long long>`, Rust `HashMap`, Go `map`, JS
`Map`, Python `dict`, cp3 `Map` from `std/collections`.

The LCG is deliberately small enough to compute in 32-bit arithmetic but is
evaluated in 64-bit values everywhere; the multiplier, modulus and seed are
pinned in the source of every entry so no language can quietly disagree.

## Language notes

- JS uses a real `Map` (not a plain object) so the hash-map structure is being
  measured, not a shape transition on an object.
- Python uses `dict`, its native hash map.
- The cp3 entry is written to the frozen spec against the `std/collections`
  `Map`; it is not runnable until that wave lands, and the suite reports the
  compiler's own diagnostic in the meantime.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o word-frequency.exe main.cpp
./word-frequency.exe

# rust
rustc -O main.rs -o word-frequency.exe
./word-frequency.exe

# go
go build -o word-frequency.exe main.go
./word-frequency.exe

# js
node main.mjs

# python
python main.py

# cp3 - expected to fail until the collections wave lands
build/bin/cp3c.exe build benchmarks/word-frequency/cp3/main.cp3
```

The expected value is re-derived independently by `../bench/oracle.py`
(`word_frequency_checksum`), which uses a plain Python dict and the same LCG:

```bash
python benchmarks/bench/oracle.py
```

Rerun the tier with `python benchmarks/run_benchmark.py --only word-frequency`
and compare the `output` and `match` columns.
