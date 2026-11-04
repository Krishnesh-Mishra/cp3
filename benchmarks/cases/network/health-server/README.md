# health-server - `GET /healthz` -> `200 "ok"` in seven languages

The smallest production-shaped service: an HTTP server that a probe can hit.
This is the task where "startup time" and "requests per second" actually mean
something, because every language here ships a different amount of server
machinery under the same three routes.

Contract, identical in every language:

| request | response |
| --- | --- |
| `GET /healthz` | `200` text/plain `ok` |
| `GET /` | `200` text/plain `health-server ok` |
| anything else | `404` text/plain `not found` |

Every entry listens on `127.0.0.1` only (no firewall prompt, no exposure) and
takes the port as `argv[1]` (default 8080); the suite assigns 18080-18087 per
language. The cp3 entry reads `process.argv` like every other entry.

## Entries

Each language uses the framework its ecosystem recommends for a production
HTTP service, with the dependency-free implementation kept as a fallback.

| row | entry | source |
| --- | --- | --- |
| `cpp` | Drogon 1.9.13 (MSVC, vcpkg) | `cpp/drogon_main.cpp`, `cpp/CMakeLists.txt` |
| `rust` | Axum 0.8 on Tokio | `rust/main.rs`, `rust/Cargo.toml` |
| `go` | Gin 1.12 on `net/http` | `go/main.go`, `go/go.mod` |
| `js` | Express 5.3 | `js/express/server.mjs` |
| `js-fastify` | Fastify 5.12 | `js/fastify/server.mjs` |
| `python` | FastAPI on Uvicorn | `python/fastapi/main.py` |
| `java` | Undertow | `java/Main.java` |
| `cp3` | `std/network` | `cp3/src/index.cp3`, `cp3/package.json` |

Fallbacks used only when a dependency is absent: `cpp/main.cpp` (raw sockets,
`g++ -O2`), `rust/main.rs` (`std::net` when `Cargo.toml` is missing), `js/main.mjs`
(`node:http`), and `python/main.py` (threaded `http.server`). The runner prints
the selected variant in the table, so a fallback is never silent.

## Dependencies

Installed once, outside git:

```powershell
# C++: Drogon into the vcpkg tree (best-effort; raw sockets otherwise)
powershell -ExecutionPolicy Bypass -File benchmarks/runner/setup-drogon.ps1

# Java: pinned Undertow jars into benchmarks/.cache/deps/java/undertow
powershell -ExecutionPolicy Bypass -File benchmarks/runner/setup-java-undertow.ps1

# Python: FastAPI + Uvicorn into benchmarks/.cache/deps/python
python -m venv benchmarks/.cache/deps/python
benchmarks/.cache/deps/python/Scripts/python.exe -m pip install fastapi uvicorn

# JS: per-folder installs
cd benchmarks/cases/health-server/js/express; npm install
cd ../fastify; npm install
```

Go modules are fetched by `go build`; Cargo fetches the Rust crates.

## Run one by hand

```bash
# cpp - Drogon via CMake (Release)
cmake -S benchmarks/cases/health-server/cpp -B benchmarks/.cache/build/health-server-cpp-drogon \
      -G "Visual Studio 18 2026" -A x64 \
      -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows \
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build benchmarks/.cache/build/health-server-cpp-drogon --config Release
benchmarks/.cache/build/health-server-cpp-drogon/Release/health-server-drogon.exe 18080

# rust
cd benchmarks/cases/health-server/rust && cargo build --release
target/release/health-server-rust.exe 18081

# go
cd benchmarks/cases/health-server/go && go build -o health-server.exe .
./health-server.exe 18082

# js - Express (or Fastify on 18087)
cd benchmarks/cases/health-server/js/express && node server.mjs 18083

# python
benchmarks/.cache/deps/python/Scripts/python.exe benchmarks/cases/health-server/python/fastapi/main.py 18084

# java
javac -cp "benchmarks/.cache/deps/java/undertow/*" -d benchmarks/.cache/build/health-server-java \
      benchmarks/cases/health-server/java/Main.java
java -cp "benchmarks/.cache/build/health-server-java;benchmarks/.cache/deps/java/undertow/*" Main 18086

# cp3 - build first, then run the binary
build/bin/cp3c.exe build src/index.cp3 --show-runtime   # from cp3/
benchmarks/cases/health-server/cp3/.cp3/out/index.exe 18085
```

Then `curl http://127.0.0.1:<port>/healthz` (or any HTTP client) and stop the
server. The suite runner (`../../runner/run_suite.py`) starts and stops these
automatically and never touches a process it did not start.

## Verification record (2026-10-28, cp3c 0.1.0-beta.1)

All eight rows were built, started on their assigned ports, hit with real
HTTP GETs, and stopped by exact process id. Every row returned:

* `GET /healthz` -> `200` body `ok`
* `GET /` -> `200` body `health-server ok`
* `GET /nope` -> `404` body `not found`

The runner also strict-checks `GET /healthz/` and `POST /healthz` on the new
framework entries, which return `404 not found` like the contract requires.
Timings from the latest full run live in [`../results.md`](../results.md).
