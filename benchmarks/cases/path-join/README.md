# path-join - join, normalize, basename and extname 170,000 times

Path-manipulation workload, identical in all six languages. Repeat 170,000
times over one fixed segment list:

```text
var / log / cp3 / service / events-2026-10-23.log
```

1. `join` the segments
2. `normalize` the joined path
3. `basename` the joined path
4. `extname` the joined path
5. fold the four string lengths and the first character of the joined path,
   basename and extension into the running checksum

One line, one integer: `425577`.

Only lengths and first characters are folded. The path separator is one
character on both Windows and POSIX, so the checksum is identical on every
supported platform even though the separator itself differs.

## Language notes

- cpp: `std::filesystem::path` construction, `lexically_normal`, `filename`
  and `extension`.
- rust: `std::path::Path` / `PathBuf` with `join`, `file_name` and
  `extension`.
- go: `path/filepath.Join`, `Clean`, `Base` and `Ext`.
- js: `node:path` `join`, `normalize`, `basename` and `extname`.
- python: `os.path.join`, `normpath`, `basename` and `splitext`.
- cp3: `std/path` `join`, `normalize`, `basename` and `extname`.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o path-join.exe main.cpp
./path-join.exe

# rust
rustc -O main.rs -o path-join.exe
./path-join.exe

# go
go build -o path-join.exe main.go
./path-join.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/path-join/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/path-join/cp3/src/index.cp3
```

The oracle value is re-derived by `../../runner/oracle.py` (`path_join_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the tier with `python benchmarks/runner/run_benchmark.py --only path-join`.
