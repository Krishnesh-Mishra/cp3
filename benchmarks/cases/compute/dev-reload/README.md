# dev-reload

How fast is one edit -> rebuild -> rerun cycle in each language? The same
one-line program as `hello-world` (`console.log("Hello, world!")`); the
runner flips the `Hello`/`Hi` string in a scratch copy of each source each
iteration, then re-measures the whole cycle (edit -> build -> run -> exit).
The committed sources are never modified and each language's build tool runs
exactly as it does in a normal dev session (`cp3 run`, a fresh incremental
`rustc`/`g++` build, `go run`, plain `node`/`python`/`javac`+`java`).

This task reports the median edit-to-output wall time per language — the
single number a developer feels when they save a change and re-run, and the
baseline the v0.2.5 fast-path work (`cp3 run`/`cp3 dev` per-module TUs +
incremental frontend + native hot patch) is measured against.

Per-language commands (identical one-line program everywhere):

| Language | Build + run |
| --- | --- |
| C++ | `g++ -std=c++20 -O3 -o <out> main.cpp` then run |
| Rust | `rustc -C opt-level=3 -o <out> main.rs` then run |
| Go | `y=go build && run` (go run semantics) |
| Java | `javac Main.java` then `java Main` |
| JavaScript | `node main.mjs` (no compile step) |
| Python | `python main.py` (no compile step) |
| cp3 | `cp3c run src/index.cp3` |

## 2026-11-13 baseline (cp3 0.2.4, before the v0.2.5 fast-run/dev work)

Measured with `python benchmarks/runner/run_benchmark.py --only dev-reload --cp3c build/windows-release/bin/cp3c.exe` (15 runs per language, alternating edit):

| Language | median ms | best ms |
| --- | --- | --- |
| python | 40.7 | 33.0 |
| js | 65.1 | 59.5 |
| java | 549.5 | 527.0 |
| rust | 714.7 | 611.5 |
| cpp | 728.3 | 704.2 |
| go | 1906.8 | 1750.0 |
| cp3 | 2751.8 | 2610.9 |

cp3 is ~4x slower than C++/Rust today (it recompiles the whole program per
change). This is the baseline the v0.2.5 per-module TU + incremental
frontend + native hot patch work is measured against.
