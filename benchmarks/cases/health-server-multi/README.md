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
8080). The suite assigns 18090-18097 per language.

## What each entry does

Each language uses the framework its ecosystem recommends for a multicore
HTTP service, with the dependency-free implementation kept as a fallback.

| row | entry | multicore shape |
| --- | --- | --- |
| `cpp` | Drogon (vcpkg, MSVC) | `setThreadNum(std::thread::hardware_concurrency())`: one event loop per hardware thread |
| `rust` | Axum 0.8 + Tokio | Tokio's default multi-threaded runtime, one worker per logical CPU |
| `go` | Gin 1.12 | `GOMAXPROCS(runtime.NumCPU())`; the scheduler serves every connection concurrently |
| `js` | Express 5.3 | `node:cluster`, one Express worker process per logical CPU on the shared handle |
| `js-fastify` | Fastify 5.12 | `node:cluster`, one Fastify worker process per logical CPU |
| `python` | FastAPI + Uvicorn | `workers=os.cpu_count()`, one process per logical CPU |
| `java` | Undertow | `setIoThreads(cpus)`, `setWorkerThreads(cpus * 2)`, dispatched handlers |
| `cp3` | `std/network` | `createServer({ threads: os.cpus() })`: one event loop and one uWS app per logical CPU (D79) |

Fallbacks used only when a dependency is absent: `cpp/main.cpp` (raw sockets
with one `accept()` worker per core), `rust/main.rs` (`std::net` accept workers),
`js/main.mjs` (`node:cluster` with `node:http`), and `python/main.py`
(`multiprocessing` workers sharing one listening socket).

The compiled entries keep HTTP/1.1 keep-alive; pipelined requests are not
supported, and the bench client never pipelines.

## Build and run by hand

From this folder (Windows suffixes shown; drop `.exe` on POSIX):

```bash
# cpp - Drogon via CMake (Release)
cmake -S benchmarks/cases/health-server-multi/cpp -B benchmarks/.cache/build/health-server-multi-cpp-drogon \
      -G "Visual Studio 18 2026" -A x64 \
      -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows \
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build benchmarks/.cache/build/health-server-multi-cpp-drogon --config Release
benchmarks/.cache/build/health-server-multi-cpp-drogon/Release/health-server-multi-drogon.exe 18090

# rust
cd benchmarks/cases/health-server-multi/rust && cargo build --release
target/release/health-server-multi-rust.exe 18091

# go
cd benchmarks/cases/health-server-multi/go && go build -o health-server-multi.exe .
./health-server-multi.exe 18092

# js - Express (or Fastify on 18097)
cd benchmarks/cases/health-server-multi/js/express && node server.mjs 18093

# python
benchmarks/.cache/deps/python/Scripts/python.exe benchmarks/cases/health-server-multi/python/fastapi/main.py 18094

# java
javac -cp "benchmarks/.cache/deps/java/undertow/*" -d benchmarks/.cache/build/health-server-multi-java \
      benchmarks/cases/health-server-multi/java/Main.java
java -cp "benchmarks/.cache/build/health-server-multi-java;benchmarks/.cache/deps/java/undertow/*" Main 18096

# cp3 - build first, then run the binary
build/bin/cp3c.exe build src/index.cp3 --show-runtime   # from cp3/
benchmarks/cases/health-server-multi/cp3/.cp3/out/index.exe 18095
```

Then `curl http://127.0.0.1:18095/healthz` and stop the process. On Windows the
Python row's worker processes are terminated with the parent by the suite's
`taskkill /T`, so no worker survives a row.

## How the suite measures this row

`../../runner/run_suite.py` runs the single-core row and this row from the same
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

## Verification record (2026-10-28)

All eight rows were built, started on ports 18090-18097, and hit with real
requests. Every one returned:

* `GET /healthz` -> `200` body `ok`
* `GET /` -> `200` body `health-server ok`
* `GET /nope` -> `404` body `not found`

The latest full-run timings live in [`../results.md`](../results.md).
