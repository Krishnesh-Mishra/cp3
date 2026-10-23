# env-injection - read one injected environment variable 500,000 times

Environment-lookup workload, identical in all six languages. The suite runner
injects one fixed variable into every child process for this task only:

```text
CP3_BENCH_ENV=cp3-benchmark-value
```

Each iteration performs a fresh environment read and folds the value's length,
byte sum, first byte and last byte into the running checksum:

```text
checksum = (checksum * 31 + value.length) % 1000003
checksum = (checksum * 31 + byte_sum)     % 1000003
checksum = (checksum * 31 + value[0])     % 1000003
checksum = (checksum * 31 + value[-1])    % 1000003
```

One line, one integer: `133644`.

A missing variable is an error in every entry, so a run without the injection
fails instead of silently measuring an empty string. The runner merges the
variable over the inherited environment only for this task; other tasks see
the normal environment.

## Language notes

- cpp: `std::getenv` on every iteration.
- rust: `std::env::var` on every iteration.
- go: `os.Getenv` on every iteration.
- js: `process.env.CP3_BENCH_ENV` on every iteration.
- python: `os.environ["CP3_BENCH_ENV"]` on every iteration.
- cp3: `env.get<string>("CP3_BENCH_ENV")` from `std/env` on every iteration.

## Run one by hand

From each language's folder, set the variable first:

```bash
# cpp
g++ -std=c++20 -O2 -o env-injection.exe main.cpp
CP3_BENCH_ENV=cp3-benchmark-value ./env-injection.exe

# rust
rustc -O main.rs -o env-injection.exe
CP3_BENCH_ENV=cp3-benchmark-value ./env-injection.exe

# go
go build -o env-injection.exe main.go
CP3_BENCH_ENV=cp3-benchmark-value ./env-injection.exe

# js
CP3_BENCH_ENV=cp3-benchmark-value node main.mjs

# python
CP3_BENCH_ENV=cp3-benchmark-value python main.py

# cp3
build/bin/cp3c.exe build benchmarks/env-injection/cp3/main.cp3
CP3_BENCH_ENV=cp3-benchmark-value build/bin/cp3c.exe run benchmarks/env-injection/cp3/main.cp3
```

On PowerShell, use `$env:CP3_BENCH_ENV = "cp3-benchmark-value"` before the
command. The oracle value is re-derived by `../bench/oracle.py`
(`env_injection_checksum`):

```bash
python benchmarks/bench/oracle.py
```

Rerun the tier with `python benchmarks/run_benchmark.py --only env-injection`.
