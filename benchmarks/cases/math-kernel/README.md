# math-kernel - sqrt, floor, abs, pow and clamp over a fixed grid

Floating-point throughput workload, identical in every language. For
`COUNT = 5,000,000` points `i = 1..COUNT`, evaluate:

```text
x       = i * 0.000001
s       = sqrt(i)                          // accumulated separately
f       = floor(x + 0.5)
a       = abs(x - 2.5)
p       = pow(i, 2)                        // integer exponent
c       = clamp(x * 1000, 0, 1000)
mixed   = s + f + a + c
quantum = trunc(mixed * 1000000)
```

Fold each quantum and the truncated `pow` value into an integer checksum:

```text
checksum = (checksum * 31 + quantum + trunc(pow(i, 2))) % 1000000007
```

After the loop, fold the accumulated `sqrt` total as well:

```text
checksum = (checksum * 31 + trunc(sqrt_total * 1000)) % 1000000007
```

One line, one integer: `177047762`.

## Why the result is bit-stable

- `sqrt` is a correctly rounded IEEE-754 operation on every entry.
- `pow(i, 2)` uses an integer exponent, and every square is exact well below
  2^53.
- Every term is quantized to an integer (multiply by 1,000,000 and truncate)
  before it enters the fold, so the final value is integer-exact and no entry
  ever prints a floating-point number. Last-bit formatting differences cannot
  leak into the output.
- The same operations run in the same order in every language, so the
  double-precision intermediates round identically.

## Language notes

- cpp: `std::sqrt`, `std::floor`, `std::abs`, `std::pow`, `std::clamp`.
- rust: `f64::sqrt`, `floor`, `abs`, `powi(2)`, `clamp`.
- go: `math.Sqrt`, `math.Floor`, `math.Abs`, `math.Pow(x, 2)`, `math.Min/Max`.
- js: `Math.sqrt`, `Math.floor`, `Math.abs`, `Math.pow(i, 2)`, `Math.min/Max`.
- python: `math.sqrt`, `math.floor`, `abs`, `math.pow(i, 2)`, `min/max`.
- cp3: `std/math` `Math.sqrt`, `Math.floor`, `Math.abs`, `Math.pow`,
  `Math.clamp`, `Math.trunc`.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o math-kernel.exe main.cpp
./math-kernel.exe

# rust
rustc -O main.rs -o math-kernel.exe
./math-kernel.exe

# go
go build -o math-kernel.exe main.go
./math-kernel.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/math-kernel/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/math-kernel/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`math_kernel_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only math-kernel`.
