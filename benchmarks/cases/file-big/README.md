# file-big - large sequential file write and read

Large-I/O workload. Every language writes an 8 MiB file, reads it back, and
folds the bytes. The existing `file-roundtrip` row uses 4 KiB files; this row
tests the same path at a size where buffering and allocation strategy matter.

## Workload

Constants:

```text
SIZE       = 8388608        # 8 MiB
ITERATIONS = 8
MOD        = 1000003
```

The payload is deterministic. Start with the 64-byte seed:

```text
0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

Repeat that 64-byte seed exactly `SIZE / 64 = 131072` times to produce the
`8388608`-byte payload. The byte sum of the payload is `588251136`.

For each of `ITERATIONS` iterations:

1. Write the payload to a file in the OS temp directory.
2. Read the file back.
3. Fold the read result:

```text
checksum = (checksum * 31 + length(read)) % MOD
checksum = (checksum * 31 + sum(read bytes)) % MOD
checksum = (checksum * 31 + read[0]) % MOD
checksum = (checksum * 31 + read[length - 1]) % MOD
```

`checksum` starts at `0`. Use a language-specific temp filename such as
`cp3-bench-file-big-<lang>.bin`; remove the file at the end.

One line, one integer: `779913`.

## Why this task

It measures large sequential I/O, buffer allocation and the cost of materializing
an 8 MiB value, at a scale the small round-trip row cannot reach. It also makes
sure the language can write and read binary-safe data without truncation or
text encoding changes.

## Language notes

- cpp: `std::ofstream`/`std::ifstream` in binary mode; read into
  `std::vector<unsigned char>`; sum with `unsigned char`.
- rust: `std::fs::write`, `std::fs::read`; sum `u8` values.
- go: `os.WriteFile`, `os.ReadFile`; sum `byte` values.
- js: `fs.writeFileSync(path, buffer)`, `fs.readFileSync(path)`; the read
  result is a `Buffer`.
- python: `open(path, "wb")`, `open(path, "rb").read()`; sum `bytes` values.
- java: `Files.write`, `Files.readAllBytes`; mask each `byte` with `& 0xFF`
  before summing.
- cp3: `fs.writeFileSync(file, data)`, `fs.readFileSync(file)`; the read result
  is a string whose `length` is bytes and whose `[i]` is a byte number. Build
  the payload with `SEED.repeat(SIZE / 64)` rather than repeated `+=`. Use
  `typeSystem: "rust"` in `package.json`.

The timed section includes the write, the read and the fold. Build the payload
once before the loop; do not rebuild it on every iteration.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o file-big.exe main.cpp
./file-big.exe

# rust
rustc -C opt-level=3 -o file-big.exe main.rs
./file-big.exe

# go
go build -o file-big.exe main.go
./file-big.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/file-big/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/file-big/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`file_big_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only file-big`.
