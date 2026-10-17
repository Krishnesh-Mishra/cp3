# cp3c examples

Nine small, self-contained C+++ programs — the ones to read first after the
language overview. Each lives in its own folder as a single `main.cp3`.

Run any of them from the repo root with `cp3c run <path>`; the command builds
the program and then executes it.

| Example | What it shows | Run it |
| --- | --- | --- |
| `basics/hello-world` | the smallest program: import `std/console`, print one line | `build/bin/cp3c.exe run examples/basics/hello-world/main.cp3` |
| `basics/fizzbuzz` | `while`, a function, `if`/`else`, `%`, template literals | `build/bin/cp3c.exe run examples/basics/fizzbuzz/main.cp3` |
| `basics/template-literals` | `` `${}` `` interpolation, string building, exact number formatting | `build/bin/cp3c.exe run examples/basics/template-literals/main.cp3` |
| `games/guess-the-number` | interactive game: `Math.random`, `console.input` loop, `parseInt`, EOF handling | `build/bin/cp3c.exe run examples/games/guess-the-number/main.cp3` |
| `games/rock-paper-scissors` | interactive game: helper functions, string method, running score | `build/bin/cp3c.exe run examples/games/rock-paper-scissors/main.cp3` |
| `servers/healthz` | HTTP liveness service: `GET /healthz` -> `ok`, logging middleware, 404 fallback | `build/bin/cp3c.exe run examples/servers/healthz/main.cp3` |
| `servers/echo-api` | HTTP JSON API: route params, typed JSON body (`req.json<T>()`), counters, per-request errors | `build/bin/cp3c.exe run examples/servers/echo-api/main.cp3` |
| `concurrency/parallel-sum` | `parallel.run` / `.for` / `.reduce`, following the capture-safety rules | `build/bin/cp3c.exe run examples/concurrency/parallel-sum/main.cp3` |
| `async/timers-demo` | `async`/`await`, `setTimeout` / `clearTimeout` / `sleep`, event-loop exit | `build/bin/cp3c.exe run examples/async/timers-demo/main.cp3` |

The two `servers/` examples are the centerpieces: `healthz` is the minimum
production-shaped service (a probe endpoint orchestrators can hit), `echo-api`
is the next step up (middleware + live counters + JSON in/out + a typed body).

## What runs today

Checked against the `build/bin/cp3c.exe` on this machine (2026-10-17,
`cp3c 0.1.0-beta.1`, after the async/timer wave landed) — run each of these;
they print and exit cleanly:

* `basics/hello-world` -> `Hello, world!`
* `basics/fizzbuzz` -> `1` .. `FizzBuzz` for 1..15
* `basics/template-literals` -> the six-line order summary
* `async/timers-demo` -> the countdown demo: timers fire in deadline order,
  and the event loop drains pending work before exiting

The rest are written against the frozen spec but wait on compiler or
std-lib features that are still landing, so `cp3c run` reports `CP3002: not
implemented yet` today:

* `games/*` need **builtin method calls** (`Math.random`, string methods),
  **`console.input`**, and `parseInt` — the member/method wave plus the
  grammar tail.
* `servers/*` need **`std/network`** plus method calls, interfaces and async
  middleware/handlers.
* `concurrency/parallel-sum` parses, but its **`parallel.*` calls, array
  literals and indexing** are not implemented in codegen yet.

The `games/*` programs read stdin. Play them interactively, or redirect an
empty input; on EOF they print their goodbye line and exit.

There is also [`competitions/`](competitions/README.md): the same small task
implemented side by side in cp3, C++, Rust, Go, JavaScript (Node) and Python,
with a dependency-free bench runner that measures boot time, request latency
and wall-clock compute time. Pending cp3 entries there are written to spec and
labeled as such.

## Notes on the spec

* **Range `..` endpoint inclusivity is not pinned anywhere.** The only usage in
  the spec is `parallel.for(0..numbers.length, ...)`
  (docs/language-spec/13), which is only coherent as *end-exclusive* — an
  inclusive end would index one past the array. `parallel-sum` follows that
  reading.
* **`console.input(prompt?)` prompt display is unspecified.** Whether the
  optional prompt is written to stdout, and with what framing, is not stated.
  The games call `console.input()` without the prompt and print their own
  instructions, so their output stays reproducible for a given input stream.
* **`if` / `else if` / `else` where every branch returns** is currently
  diagnosed as a (false) `CP3504: may fall off the end of its body`. The
  examples use early returns plus a final return instead of the chain —
  spec-correct either way.
* **Servers** are written against the pinned `std/network` surface in
  STDLIB-README: `createServer()`, `.use(async (req, res, next) => ...)`,
  `.get`/`.post`, route params via `req.params.get`, `res.text`/`res.json(data,
  status)`, `.listen(port, cb)`, and the documented per-request 500 boundary.
