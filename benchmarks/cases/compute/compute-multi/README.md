# compute-multi - the same total, split across all cores

The compute-single workload (total Collatz steps for every start value in
`1..1,000,000`, one integer out - `131434424`) split across
`hardware_concurrency()` workers:

1. split `1..1,000,000` into contiguous chunks of `ceil(N / workers)`;
2. each worker sums its chunk (each worker runs its own private accumulator);
3. the partial sums are merged on the main thread / goroutine and printed.

No worker ever mutates shared state, which is also what spec §13's
capture-safety rules require of the cp3 entry.

| language | workers | mechanism |
| --- | --- | --- |
| cpp | `std::thread::hardware_concurrency()` | `std::thread` pool, partials joined on the main thread |
| rust | `std::thread::available_parallelism()` | `std::thread`, `join()`ed into a total |
| go | `runtime.NumCPU()` | goroutines + `sync.WaitGroup`, partials in a slice |
| js | `os.availableParallelism()` | `worker_threads` (each with its own V8 isolate) |
| python | `os.cpu_count()` | `concurrent.futures.ProcessPoolExecutor` - **processes**, not threads: the GIL makes CPU-bound threads pointless; Windows spawn re-imports the module per worker, and that spawn cost is inside the measured wall time |
| cp3 | TBB (managed by `parallel.*`) | `parallel.reduce` per spec §13: per-worker accumulate, merged at the end |

## Run one by hand

From each language's folder (build commands as in compute-single):

```bash
g++ -std=c++20 -O3 -o compute-multi.exe main.cpp && ./compute-multi.exe      # cpp
rustc -O main.rs -o compute-multi.exe && ./compute-multi.exe                # rust
go build -o compute-multi.exe main.go && ./compute-multi.exe                # go
node main.mjs                                                               # js
python main.py                                                              # python

# cp3 - from the project folder
build/bin/cp3c.exe build src/index.cp3 --show-runtime
./.cp3/out/index.exe
```

All seven entries print `131434424`, the same integer as every single-threaded
entry, which is the correctness check built into the suite (`match` column).

## Verification record (2026-10-18, post-P1, cp3c 0.1.0-beta.1)

cpp, rust, go, js and python were built and run: all printed `131434424`.
Wall-clock best-of-2 from the labeled run in `../results.md`
(`runner/results/2026-10-18T0803-local/`, process startup included):

| language | best wall time | vs its single-thread run |
| --- | --- | --- |
| rust | 19 ms | ~6.8x faster |
| cpp | 23 ms | ~6.6x faster |
| go | 30 ms | ~6.4x faster |
| js (node) | 261 ms | ~4.1x faster |
| python | 1343 ms | ~7.2x faster |

(The speedup column is computed from the two tables of that run; absolute
numbers are machine-specific.)

The cp3 entry uses `parallel.reduce(1..1000001, 0, (sum, i) =>
sum + collatzSteps(i))` with an end-exclusive range. The compiler proves the
native-`i64` helper pure and emits the chunked
`parallelReduce<ParallelCombineKind::Add>` lowering, so the reduction runs
across all cores and combines the per-chunk partials in index order. The
committed source is `cp3/src/index.cp3`; the suite builds it with
`cp3c build src/index.cp3 --show-runtime` and runs `.cp3/out/index.exe`.
