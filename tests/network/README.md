# tests/network - the `std/network` verification suite

Permanent verification harness for the `network` standard module, by project-owner
directive. It proves the documented HTTP/WebSocket surface over real sockets with
dual verification: every wire-level claim is checked from both ends, with python
(stdlib only) as the independent referee in phases A and C and a cp3 program as
the independent half in phases B and D.

Docs oracle: `docs/standard-library/04-imported-std-modules.md`, section
`network`, "Included in beta one" only. Checks that need a surface that is
scheduled but not landed are still written docs-faithfully and reported as
`PENDING SURFACE` with the exact compiler message; they are never weakened.

## Running

The suite (phases A, B, C, D, S; default cp3c `build/bin/cp3c.exe`):

    python tests/network/run_network_suite.py --cp3c <cp3c> [--only A,B,S]
           [--json] [--results DIR] [--skip-pending]

The benchmark pre-flight smoke (compiles, boots and contract-checks
`benchmarks/health-server/cp3`; never times anything; never binds 8080 - the
bench port constant is substituted into a scratch copy):

    python tests/network/run_network_suite.py --cp3c <cp3c> --smoke-bench-server
           [--smoke-port 48209] [--scratch DIR]

The corpus cases for this wave (self-contained run cases, ports 48105-48114):

    python tests/runner/run_tests.py --no-bench --jobs 6 --cp3c <cp3c>
           --filter network_suite_

Exit codes: `0` only when every executed check passes; `1` when any check
failed or is `PENDING SURFACE`; `2` for usage errors (missing cp3c, unknown
phase, `--smoke-port 8080`). `--skip-pending` marks the whole phase B new-surface
fixture family (B15-B21) `SKIPPED` instead of running it, which is the green run
on a fork without the scheduled surface (exit 0; nothing is counted as a
failure).
`--json` prints one machine-readable document instead of the human output.
Per-check timeout defaults to 120 s, hard overall bound 300 s, whole-suite target
~90 s (cold fixture compilation dominates; warm runs are ~30 s).

## Files

- `run_network_suite.py` - the orchestrator: phases, checkers, port map, smoke
  mode, `--json` / `--results` output (raw logs plus `checks.json` and
  `run.txt` per run).
- `http_referee.py` - phase A python `ThreadingHTTPServer`: fixed statuses,
  echo (method/path/body/headers as JSON), redirect, slow endpoint, binary
  payload, JSON endpoints, and a request log that acts as the independent half
  of phase A (A12).
- `ws_rfc6455.py` - stdlib RFC 6455 server and client (handshake, frame codec,
  text and binary frames, ping/pong, close codes) used by phases C and D; no
  pip, no downloads.
- `security_probes.py` - phase S probes; each asserts survival of the server
  and a correct response afterwards, plus the per-probe invariant (for example,
  a CRLF-injected header must never appear in a response).
- `fixtures/{client,server}/*.cp3` - the cp3 side: fetch client (A), server owner (B), WebSocket
  client (C), WebSocketServer (D), probe target (S), smoke contract checker.
  Each fixture prints machine-checkable `ID|field|...` lines and exits 0/1.

## Coverage matrix (doc surface -> check -> phase)

### fetch - phase A (python referee on 48201, cp3 fetch client)

| docs surface | check | expected |
| --- | --- | --- |
| `fetch` 2xx: `status`/`ok`/`text()`/`statusText` | A1 | `200|true|ok-body|OK` |
| non-2xx does not throw | A2 | `404|false|not found|Not Found` |
| `fetch(url, {method, body})` POST | A3 | `200|POST|/echo|alpha` |
| PUT body + query untouched | A4 | `200|PUT|/echo?x=1|beta` |
| DELETE without body | A5 | `200|DELETE|/echo|` |
| query string passes through untouched | A6 | `200|GET|/echo?x=1&y=2|` |
| `timeout` (ms) rejects: `timed out after 300 ms` | A7 | rejection with the documented message |
| network-level failure rejects (`could not connect`) | A8 | rejection |
| `https://` with TLS off: `https:// is not supported` | A9 | clear rejection (D70) |
| redirect behavior (docs silent) | A10 | recorded: `302|redirecting` (no auto-follow) |
| `arrayBuffer()` -> Buffer hex | A11 | 64 bytes `000102...3f` |
| `RequestInit.headers` Map sent, `res.headers` Map read | A13 | `200|yes|net` |
| `json<number>()` typed read | A14 | `200|42` |
| `json<User>()` reified interface read | A15 | `200|7|alice` |
| `json<string>()` / `json<boolean>()` | A16a/b | `200|hi` / `200|true` |
| `json<T>()` shape mismatch rejects catchably | A17 | rejection: `JSON payload is not a number` |
| referee log: every request, bodies, `x-probe`, Host | A12 | independent authority on the wire |

### server - phase B (cp3 createServer on 48204, cp3 fetch client)

| docs surface | check | expected |
| --- | --- | --- |
| `res.text(data)` | B1 | `200|true|hello` |
| `res.status(201)` | B2 | `201|created` |
| chained `res.status(...).header(...)` | B3 | `202|chained` |
| `res.write` + `res.end()` streaming | B4 | `200|chunk1chunk2` |
| `res.json(scalar)` wire bytes | B5-B7 | `7` / `"hi"` / `true` |
| `req.method` + `req.path` (query stripped) | B8 | `200|GET /method` |
| POST route matching | B9 | `200|posted` |
| PUT route with `:id` params | B10 | `200|putparam` |
| 404 fallback | B11 | `404|not found` |
| `res.send(status)` empty body | B12 | `203|` |
| middleware answering stops the chain | B13 | `418|teapot` |
| server alive after the whole run | B14 | `200|hello` |
| middleware `next()` continues the chain | B15 | `PB1|200|x12` |
| async handler + `req.text()` body echo | B16 | pending surface (`async arrow function`) |
| `req.params` / `req.query` / `req.headers` Maps | B17 | `PB3|200|7|1|en|on` |
| `app.delete(route, handler)` | B18 | `PB4|200|deleted` + `PB4b|404|not found` |
| `res.json<T>(payload, status?)` typed payload | B19 | pending surface (`json<T>() takes no arguments`) |
| unannotated `req.path === literal` | B20 | pending surface, runtime gap (finding F3) |
| `res.json(payload, status?)` + reified client read | B21 | `PB5a|200|7|alice|application/json; charset=utf-8` + `PB5b|201|9|bob` |

### WebSocket client - phase C (python RFC6455 server on 48205)

| docs surface | check | expected |
| --- | --- | --- |
| `on("open")` | C1 | `open` |
| text echo delivered twice | C2a/b | `echo:hello`, `echo:again` |
| binary frame delivered byte-exact | C2c | `000102ff62696e` (raw bytes) |
| `close(code, reason)` handshake reported | C3 | `1000|done` |
| payload/closed bookkeeping | C5 | `3|true` |
| python-side observations: texts, binary, auto-pong, close | C6 | exact payload list |

### WebSocketServer - phase D (cp3 server on 48206, python RFC6455 client)

| docs surface | check | expected |
| --- | --- | --- |
| server constructed | D0 | `ready` |
| `on("connection")` | D1 | `connection` |
| text messages delivered and echoed | D2a/b | `hello`, `second` |
| client binary frame delivered byte-exact | D2c | `deadbeef` (raw bytes) |
| close event with code/reason | D3 | `1000|bye` |
| server closed cleanly | D4 | `done` |
| python-side observations: echoes, binary echo, pong, close ack | D6 | exact payload list |
| plain HTTP request to the ws port (docs silent) | D7 | recorded: `404 File Not Found` |

### security - phase S (python probes against cp3 server on 48207)

| probe | check | assertion |
| --- | --- | --- |
| malformed JSON body | S1 | survives, still serves (JSON parse is behind pending async handlers) |
| oversized 1 MiB body | S2 | survives, echoes length |
| short Content-Length then close | S3 | connection closed, server survives |
| negative Content-Length | S4 | observed `400` (docs silent), server survives |
| duplicate Content-Length | S5 | observed `400` (docs silent), server survives |
| CRLF header/path injection | S6 | no injected marker in any response, server survives |
| path traversal / middleware bypass | S7 | `/private` exact match only, traversal stays 404, server survives |
| small-scale concurrency | S8 | 8 workers x 5 requests, 0 failures |
| 500 isolation under real sockets | S9 | throwing handler -> 500 for that request only, server survives |
| unregistered method | S10 | observed `404` (docs silent), server survives |

### corpus run cases (tests/cases/stdlib, ports 48105-48114)

| case | port | covers |
| --- | --- | --- |
| `network_suite_route_params` | 48105 | `:id` params, over-path miss |
| `network_suite_query_string` | 48106 | query parsing, absent query |
| `network_suite_status_header` | 48107 | `res.status`, `res.header` roundtrip via `res.headers.get` |
| `network_suite_streamed_response` | 48108 | `res.write` + `end()` repeatability |
| `network_suite_json_scalar` | 48109 | `res.json` scalar wire bytes read back as text |
| `network_suite_post_put_routes` | 48110 | POST/PUT routes, wrong-method fallback |
| `network_suite_middleware_auth` | 48111 | auth-stop middleware + Map header in |
| `network_suite_request_bodies` | 48112 | `req.text()` echo (PENDING: async arrow -> runner skip) |
| `network_suite_params_query_headers` | 48113 | params/query/headers Maps in one request |
| `network_suite_json_typed_roundtrip` | 48114 | interface read via `json<User>`, untyped `res.json` writes |

## Port map

| port | owner |
| --- | --- |
| 48105-48114 | corpus run cases above (48105 upward, one per case) |
| 48201 | phase A python HTTP referee |
| 48202 | phase A connection-refused probe (intentionally closed) |
| 48204 | phase B cp3 `createServer` |
| 48205 | phase C python RFC6455 server |
| 48206 | phase D cp3 `WebSocketServer` |
| 48207 | phase S cp3 probe target |
| 48209 | `--smoke-bench-server` |
| 48216-48221 | phase B new-surface fixtures (one per fixture, see the port table in `run_network_suite.py`) |

The rest of 48201-48299 is reserved but unused. The suite never binds 8080
(benchmarks) and never 48101-48104 (the pre-existing stdlib/network cases).
48220 is shared by `server_pending_path_eq.cp3` and
`server_pending_json_object.cp3`; they never run at the same time (the latter is
a compile-time pending surface, and fixture runs are sequential).

## Pending surface and open findings

`PENDING SURFACE` means the check is written to the docs and the observed
behavior is recorded with the exact compiler or runtime evidence. When the
scheduled surface lands, the check flips to `PASS` without edits.

- B16 async handlers + `req.text()`: `CP3002: not implemented yet: async arrow
  function`. Corpus case `network_suite_request_bodies` depends on the same
  surface and therefore shows as a scaffold skip in the runner.
- B19 `res.json<User>(payload)`: `CP3002: not implemented yet: std/network
  json<T>() takes no arguments`. The untyped payload form is covered by B21.
- B20 finding F3 (silent miscompile, no diagnostic): an unannotated
  `req.path === "/teapot"` lowers to
  `cp3_truthy(((req).path, std::string("/teapot"), false))`, which is always
  false, so the middleware never answers and the 404 fallback is served.
  Repro: `tests/network/fixtures/server/server_pending_path_eq.cp3`; generated line in
  the fixture's `.cp3/build/server_pending_path_eq.cpp`. Adding
  `let path: string = req.path;` before the comparison lowers it correctly
  (that is what `server/server_own.cp3` does for B13).
- F1 (top-level await rejection with a live listener): an unhandled
  `await res.json<number>()` mismatch against an object payload makes the
  process print `cp3: error: unhandled promise rejection: std/network: JSON
  payload is not a number` and exit 1 only when the event loop has no listener
  left (server closed); with the server still listening the process serves on
  silently and never reports. Caught mismatches work (A17, and a caught
  mismatch probes `U1|caught|...` plus `U2|done`).
- F2 (message listener typing): the documented `(data: string | Buffer)` slot
  is registered as `std::string` for body lowering regardless of the user's
  annotation. `(data: Buffer)` is honored in the generated C++ signature and
  does receive binary frames, but Buffer-only members inside the body
  (`data.toString("hex")`) hit `CP3002: not implemented yet: this method call`.
  The suite works around it with the unannotated slot plus `Buffer.from(msg)`
  (still byte-exact, C2c/D2c).

## Deliberately not covered (docs "Explicitly out of scope for beta one")

Nested/sub-routers, regex path matching, HTTPS/TLS (the suite asserts the clear
`https:// is not supported` rejection instead), server-side template engines,
`multipart/form-data` upload parsing, response compression. Also not covered:

- `wss://` WebSocket client URLs (TLS off; same D70 rule as `https://`).
- `req.body` ReadStream consumption (server-side upload streaming): the
  documented consumer surface needs the pending async handlers; S2 covers the
  1 MiB-body survival half only.
- Auto-following redirects: docs are silent; A10 records the observed
  non-following behavior instead of asserting one.
- Status codes where docs are silent (S4, S5, S10, D7): recorded observations,
  asserted only for survival, isolation and no-injection.

## Notes

- Python 3, stdlib only. No pip, no downloads, ever.
- The suite and the corpus cases must never bind 8080 or 48101-48104.
- `--results DIR` writes `logs/*.txt` (raw fixture/server output),
  `checks.json` (machine summary) and `run.txt` (human transcript) per run.
- Cold runs pay a full fixture recompile (roughly 30-60 s); after a rebuild the
  first run refills the toolchain cache. The 90 s target holds cold.
