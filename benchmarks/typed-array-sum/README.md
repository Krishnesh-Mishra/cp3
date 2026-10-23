# typed-array-sum - fill and sum fixed typed arrays

Numeric-memory workload, identical in every language. Allocate two fixed-size
typed arrays with `SIZE = 1,000,000` elements:

1. a `Float64Array` / `double[]` / `Vec<f64>` / `[]float64` / `array('d')`
2. a `Uint8Array` / `byte[]` / `Vec<u8>` / `[]byte` / `bytearray`

Fill them with a deterministic pattern (no random source):

```text
values[i] = (i % 97) * 0.5 + (i % 13) * 0.25
bytes[i]  = (i * 31 + 7) % 256
```

The float pattern is a multiple of 0.25 and every partial sum stays far below
2^53, so the addition is exact in IEEE-754 doubles regardless of order. Sum
the float array `ROUNDS = 32` times, add the rounds together, then read the
byte array once and fold everything into one integer:

```text
scaled    = trunc(total * 4)
checksum  = (scaled + byte_total) % 1000000007
checksum  = (checksum * 31 + SIZE + ROUNDS) % 1000000007
```

One line, one integer: `135618465`.

## Why this task

It measures the typed-array allocation, fill, element read and reduction path,
not allocation growth: both arrays are fixed-size from the first statement.
The float sum is the hot loop; the byte pass keeps a second element width in
the workload.

## Language notes

- cpp: `std::vector<double>` / `std::vector<std::uint8_t>`.
- rust: `Vec<f64>` / `Vec<u8>`.
- go: `[]float64` / `[]byte`.
- js: `Float64Array` / `Uint8Array`.
- python: `array('d')` / `bytearray` (the stdlib typed-array shapes).
- cp3: built-in `Float64Array` / `Uint8Array` and their `ArrayBuffer`
  backreferences. cp3's typed-array element read with a runtime index is typed
  `number | null | undefined`, and arithmetic on that union does not codegen
  yet, so the cp3 entry reads the same buffers through `DataView` views
  (`getFloat64(..., true)` and `getUint8`). The storage, fill and indexing
  writes are the typed arrays themselves; only the scalar read path goes
  through the landed `DataView` surface.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o typed-array-sum.exe main.cpp
./typed-array-sum.exe

# rust
rustc -O main.rs -o typed-array-sum.exe
./typed-array-sum.exe

# go
go build -o typed-array-sum.exe main.go
./typed-array-sum.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/typed-array-sum/cp3/main.cp3
build/bin/cp3c.exe run benchmarks/typed-array-sum/cp3/main.cp3
```

The value is re-derived independently by `../bench/oracle.py`
(`typed_array_sum_checksum`):

```bash
python benchmarks/bench/oracle.py
```

Rerun the row with `python benchmarks/run_benchmark.py --only typed-array-sum`.
