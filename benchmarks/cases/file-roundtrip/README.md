# file-roundtrip - write, read and stat a 4 KiB temp file 200 times

Filesystem workload, identical in all six languages. Build one fixed 4,096-byte
payload by repeating the 64-byte seed

```text
0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

then repeat 200 times:

1. write the payload to one fixed file in the OS temp directory
2. read the whole file back
3. stat the file
4. fold the read length, byte sum, first byte, last byte, `size`, `isFile`
   and `isDirectory` into the running checksum

One line, one integer: `698140`.

The file is removed at the end of the run. The payload and path are fixed, so
the output is deterministic and identical across languages and machines.

## Language notes

- cpp: `std::ofstream` / `std::ifstream` plus `std::filesystem::stat`.
- rust: `std::fs::write` / `std::fs::read` plus `std::fs::metadata`.
- go: `os.WriteFile` / `os.ReadFile` plus `os.Stat`.
- js: `fs.writeFileSync` / `fs.readFileSync` / `fs.statSync`.
- python: `open(..., "wb")` / `open(..., "rb")` plus `os.stat`.
- cp3: `std/fs` `writeFileSync` / `readFileSync` / `statSync`, with
  `std/os` `tmpdir()` and `std/path` `join()` to build the temp path.

The stat booleans are folded as `1`/`0`, so a language that returned different
metadata would print a different number.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o file-roundtrip.exe main.cpp
./file-roundtrip.exe

# rust
rustc -O main.rs -o file-roundtrip.exe
./file-roundtrip.exe

# go
go build -o file-roundtrip.exe main.go
./file-roundtrip.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/file-roundtrip/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/file-roundtrip/cp3/src/index.cp3
```

The oracle value is re-derived by `../../runner/oracle.py`
(`file_roundtrip_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the tier with `python benchmarks/runner/run_benchmark.py --only file-roundtrip`.
