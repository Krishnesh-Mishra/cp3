# hello-world - spawn a process, print a line, exit

The smallest honest benchmark in the suite: start the process, write one line
to standard output, exit. The output is identical everywhere:

```
Hello, world!
```

What the suite measures is the whole spawn-to-exit wall time of the process,
including interpreter/runtime startup, compiled-binary load, and the exit
path. Because single runs of a task this small are dominated by noise (the
first run of a fresh binary can hit an antivirus scan on Windows), the suite
reports the best and the median over at least 15 runs instead of a single
number, and the raw run list is printed next to them.

This is the task where a fast compiler story shows up as "as fast as a native
binary": on the labeled run in `../results.md`, the cp3 binary's best spawn
time is within a few percent of the C++ and Rust binaries and clearly ahead of
Node and CPython startup.

## Language notes

- cpp / rust / go: compiled ahead of time; the measured process is the binary
  itself.
- cp3: `cp3c build` produces a native executable; the suite runs that binary,
  recompiling only when the entry changes.
- js: `node main.mjs`, so the number is Node startup plus the script.
- python: `python main.py`, so the number is CPython startup plus the script.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o hello-world.exe main.cpp
./hello-world.exe

# rust
rustc -O main.rs -o hello-world.exe
./hello-world.exe

# go
go build -o hello-world.exe main.go
./hello-world.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/hello-world/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/hello-world/cp3/src/index.cp3
```

Rerun the tier with `python benchmarks/runner/run_benchmark.py --only hello-world`
(optionally `--hello-reps 30`) and read the `best_ms` and `median_ms` columns.
