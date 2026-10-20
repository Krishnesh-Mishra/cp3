# health-server - `GET /healthz` -> `200 "ok"` in six languages

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
takes the port as `argv[1]` (default 8080); the suite assigns 18080-18085 per
language. The cp3 entry reads `process.argv` like every other entry and was
smoke-verified on its own port (`/healthz` -> 200 `ok`, `/` -> 200
`health-server ok`, `/nope` -> 404 `not found`).

## Run one by hand

From each language's folder (commands shell-agnostic; the Windows binaries get
`.exe`):

```bash
# cpp - hand-rolled HTTP/1.1 over Winsock2/BSD sockets, no dependencies
g++ -std=c++20 -O2 -o health-server.exe main.cpp -lws2_32   # POSIX: drop -lws2_32
./health-server.exe 18080

# rust - std::net only, no crates
rustc -O main.rs -o health-server.exe
./health-server.exe 18081

# go - net/http
go build -o health-server.exe main.go
./health-server.exe 18082

# js - zero-dependency node:http variant (always present)
node main.mjs 18083

# js - optional Express variant (same behavior)
cd express && npm install && node server.mjs 18083

# python - stdlib http.server, threaded
python main.py 18084

# cp3 - the frozen-spec entry (build first, then run the binary)
build/bin/cp3c.exe build benchmarks/health-server/cp3/main.cp3   # from the repo root
benchmarks/health-server/cp3/.cp3/out/main.exe 18085
```

Then `curl http://127.0.0.1:18080/healthz` (or any HTTP client) and stop the
server with Ctrl+C. The suite runner (`../bench/run_suite.py`) starts and stops
these automatically and never touches a process it did not start.

## The js variant rule

`js/main.mjs` (zero-dep `node:http`) is the always-available entry.
`js/express/` is an optional second entry with a `package.json`; it is used by
the runner only when `js/express/node_modules/` exists, i.e. when `npm install`
there has succeeded at least once. On this machine it did (2026-10-17:
express 5.3.0, 65 packages, ~9 s), so the sample run in `../README.md` shows
the **express** row; with no `node_modules` the runner falls back to the
zero-dep row and says so in the `variant` column. `npm install` requires
network access; the zero-dep variant never does.

## Verification record (2026-10-18, post-P1, cp3c 0.1.0-beta.1)

All five runnable entries were built, started on ports >= 18080, hit with real
HTTP GETs, and stopped by exact process id. Observed, in every case:

* `GET /healthz` -> `200` body `ok`
* `GET /` -> `200` body `health-server ok`
* `GET /nope` -> `404` body `not found`
* startup line `listening on http://127.0.0.1:<port>` on stdout

From the labeled run in `../README.md`
(`bench/results/2026-10-18T0803-local/`, 100 warmup requests, 300 latency
requests, 8 keep-alive connections x 3 s):

| entry | boot_ms | p50_ms | req/s |
| --- | --- | --- | --- |
| rust | 524.3 | 0.06 | 12827.6 |
| python | 538.7 | 0.10 | 11977.5 |
| cpp | 545.8 | 0.05 | 11524.6 |
| js/express | 521.6 | 0.12 | 10746.4 |
| go | 509.0 | 0.10 | 8424.0 |

The uniform ~half-second boot is Windows Defender scanning each freshly
spawned process on this box, not the languages - see the methodology caveats
in `../README.md`. On a trivial handler the load client and loopback dominate:
all five rows land within ~1.5x of each other, and the compute tables are
where the runtimes actually separate.

The cp3 entry now builds and runs against the landed `std/network` API:
`createServer()`, `.get(path, handler)`, `res.text(...)`, `.use(...)` as the
404 fallback, `.listen(port, cb)`. It serves the contract on its argv port and
is measured by the suite. The single-core row is unchanged by D79; the
multicore sibling lives in `../health-server-multi/`.
