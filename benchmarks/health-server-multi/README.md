# health-server-multi - the same probe, one worker per core

This is the multicore sibling of `../health-server`: the identical three-route
contract, but every entry is written in the shape its ecosystem uses to put all
logical CPUs behind one listening address.

| request | response |
| --- | --- |
| `GET /healthz` | `200` text/plain `ok` |
| `GET /` | `200` text/plain `health-server ok` |
| anything else | `404` text/plain `not found` |

Every entry binds `127.0.0.1` only and takes the port as `argv[1]` (default
8080). The suite assigns 18090-18095 per language.

## What each entry does

| entry | multicore shape |
| --- | --- |
| cp3 | `createServer({ threads: os.cpus() })`: one event loop and one uWS app per logical CPU (D79), with module-level state copied per loop |
| cpp | `std::thread::hardware_concurrency()` workers, each calling `accept()` on the same listening socket and serving its connection in place |
| rust | `std::thread::available_parallelism()` workers, each calling `accept()` on an `Arc<TcpListener>` and serving its connection in place |
| go | `net/http` with `GOMAXPROCS(runtime.NumCPU())`; the Go scheduler serves every connection concurrently |
| js | `node:cluster`, one worker process per logical CPU, all listening on the same address through the shared cluster handle |
| python | one listening socket created by the parent and passed to `multiprocessing` workers (duplicated handle on Windows, inherited on POSIX); each worker runs a `ThreadingTCPServer` on it |

The compiled entries keep HTTP/1.1 keep-alive; pipelined requests are not
supported, and the bench client never pipelines.

## Build and run by hand

From this folder (Windows suffixes shown; drop `.exe` and `-lws2_32` on POSIX):

```bash
# cpp
g++ -std=c++20 -O2 -o health-server-multi.exe cpp/main.cpp -lws2_32
./health-server-multi.exe 18090

# rust
rustc -O -o health-server-multi.exe rust/main.rs
./health-server-multi.exe 18091

# go
go build -o health-server-multi.exe go/main.go
./health-server-multi.exe 18092

# js
node js/main.mjs 18093

# python
python python/main.py 18094

# cp3 (from the repo root)
build/bin/cp3c.exe build benchmarks/health-server-multi/cp3/main.cp3
benchmarks/health-server-multi/cp3/.cp3/out/main.exe 18095
```

Then `curl http://127.0.0.1:18095/healthz` and stop the process. On Windows the
Python row's worker processes are terminated with the parent by the suite's
`taskkill /T`, so no worker survives a row.

## How the suite measures this row

`bench/run_suite.py` runs the single-core row and this row from the same
registry. For this row it drives the server from **`--clients` separate client
OS processes** (default 4), each owning `--concurrency` keep-alive connections
(default 8), because one Python client process is often the bottleneck before a
20-loop server is. The load workers use raw sockets and parse Content-Length or
chunked bodies, so client parsing is not the limiting factor. `req/s` is total
successful requests divided by the fixed load window.

The single-core `../health-server` row uses the diagnosis quiet-probe shape:
`--single-clients` separate client OS processes (default 2), each owning
`--concurrency` `http.client` sessions. The two rows are compared within their
own table, not against each other's different client shapes.

## Verification record (2026-10-20)

All six entries were built, started on ports 48310-48315, and hit with real
requests. Every one returned:

* `GET /healthz` -> `200` body `ok`
* `GET /` -> `200` body `health-server ok`
* `GET /nope` -> `404` body `not found`

The cp3 entry was also measured at 20 loops against cpp and rust; see
`build/scratch/rounds/WAVE-M2-NET-SCALE-REPORT.md` in the worktree for the
measured table and the method.
