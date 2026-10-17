# cp3c examples

Nine small, self-contained C+++ programs — the ones to read first after the
language overview. Each lives in its own folder:

```
examples/<category>/<name>/main.cp3     the program
examples/<category>/<name>/expect.json  what running it must produce
```

They serve two purposes at once:

* **showcase** — real programs (games, HTTP services, a parallel computation),
  written against the frozen spec, not toy fragments;
* **canary** — `tools/run_examples.py` runs them all and tells you when a
  `pending` example starts compiling: as compiler milestones land, examples
  flip from *waiting* to *READY to promote* with no extra test to write.

## Status legend

| Status | Meaning |
| --- | --- |
| **active** | Verified against the frozen compiler snapshot: it compiles, runs, and its stdout/exit code match `expect.json` exactly (newline-normalized). The runner **fails** if it drifts. |
| **pending** | Spec-correct, but needs a compiler/std-lib feature that does not exist yet (this is normal: the compiler is mid-M1). The runner reports it with the compiler's current diagnostic and **never** fails the run. `requires` says what it is waiting for; `observed` records the snapshot's actual first diagnostic. |
| **READY** | Not a stored status: when a `pending` example starts compiling and running, the runner prints a prominent `READY: promote to active` line. See *Promotion workflow* below. |

`observed` is refreshed, never guessed:

```bash
python tools/run_examples.py --write-observed
```

## Inventory

| Example | Status | What it shows | Waiting on (`requires`) |
| --- | --- | --- | --- |
| `basics/hello-world` | **active** | the smallest program: import `std/console`, print one line | — |
| `basics/fizzbuzz` | **active** | `while`, a function, `if`/`else`, `%`, template literals | — |
| `basics/template-literals` | **active** | `` `${}` `` interpolation, string building, exact number formatting | — |
| `games/guess-the-number` | pending | interactive game: `Math.random`, `console.input` loop, `parseInt`, EOF handling | async/await + `std/math` + `console.input` |
| `games/rock-paper-scissors` | pending | interactive game: helper functions, string method, running score | async/await + `std/math` + `console.input` + string methods |
| `servers/healthz` | pending | HTTP liveness service: `GET /healthz` -> `ok`, logging middleware, 404 fallback | `std/network` (createServer/.use/.get/.text/.json/.listen) + async middleware |
| `servers/echo-api` | pending | HTTP JSON API: route params, typed JSON body (`req.json<T>()`), counters, per-request errors | `std/network` + async handlers + `req.json<T>()` |
| `concurrency/parallel-sum` | pending | `parallel.run` / `.for` / `.reduce`, following the capture-safety rules | `parallel.*` + arrays/indexing |
| `async/timers-demo` | pending | `async`/`await`, `setTimeout` / `clearTimeout` / `sleep`, event-loop exit | async/await + timers (`setTimeout`, `clearTimeout`, `sleep`) |

The two `servers/` examples are the centerpieces: `healthz` is the minimum
production-shaped service (a probe endpoint orchestrators can hit), `echo-api`
is the next step up (middleware + live counters + JSON in/out + a typed body).

## Running them

```bash
# all of them; default compiler resolution matches the test harness:
#   build/bin/cp3c.exe (Windows) / build/bin/cp3c
python tools/run_examples.py

# against a specific compiler build (this folder was classified with the
# 288/953 build at commit 83e1b7f; any newer cp3c is fine — the runner
# reports READY for every pending example that starts working):
python tools/run_examples.py --cp3c <path-to-cp3c>

# one example (substring match on <category>/<name>)
python tools/run_examples.py --filter fizzbuzz

# as a CMake convenience target (wired only when Python 3 was found at
# configure time; never part of ALL, exactly like cp3c-tests)
cmake --build build --target example-checks

# by hand — this is exactly what the runner does
cd examples/basics/fizzbuzz
cp3c run main.cp3
```

Flags: `--cp3c <path>`, `--examples <dir>`, `--filter <substr>`, `--verbose`,
`--timeout <sec>` (default 30), `--write-observed`. Exit code is **0** when
every active example passed (pending ones do not count) and **1** when an
active example failed, drifted, timed out, or a manifest is broken.

Examples run with **stdin closed**, like a CI box. The interactive programs are
written so the EOF path prints a reproducible transcript, which is what their
`expect.stdout` records.

### `expect.json` contract

```jsonc
{
  "status": "active",            // "active" = verified to compile+run NOW;
                                 // "pending" = needs unimplemented features
  "mode": "run",
  "expect": { "stdout": "exact\noutput\n", "exitCode": 0 },
  "requires": "one-line feature note",             // pending only
  "observed": "main.cp3:17:1: error: CP3002: ..."  // pending only: the
                                                   // snapshot's first diagnostic
}
```

Rules of the road: never mark an example `active` unless you ran it with a
compiler and stdout/exit code matched `expect` exactly; a `pending` example
that actually compiles and runs must be promoted (`status` -> `active`,
`requires`/`observed` removed).

## Promotion workflow

When the compiler grows a feature, examples do not need to be trusted by hand:

1. Run `python tools/run_examples.py --cp3c <fresh compiler>`.
2. Still blocked examples print `pend <name>: <diagnostic>`; a pending example
   that now compiles and runs prints
   `READY <name>: it compiles and runs now ...`.
3. If it says **stdout matches expect.json**, flip that example's `status` to
   `"active"` and delete `requires` + `observed`.
4. If it says **stdout differs**, run the example by hand, decide which side is
   wrong (program vs expectation), fix it, then promote.
5. Re-run the runner: active examples must pass, and the run must exit 0.
6. For still-blocked examples, `--write-observed` commits the new diagnostics.

Two honest caveats:

* **The servers are long-running.** `cp3c run main.cp3` never returns on its
  own (spec 14: the program stays alive while the event loop has work), so the
  runner will report them as `did not finish within 30s` even after
  `std/network` lands. They are *not* auto-promotable: verify them manually
  (`cp3c run main.cp3`, then `curl http://localhost:3000/healthz` -> `ok`) and
  promote by hand.
* **Games read stdin.** Their recorded transcript is the stdin-closed run
  (`... - bye!`). Playing them interactively exercises the rest of the game.

## Notes from writing these (spec gaps hit)

* **Range `..` endpoint inclusivity is not pinned anywhere.** The only usage in
  the spec is `parallel.for(0..numbers.length, ...)`
  (docs/language-spec/13), which is only coherent as *end-exclusive* — an
  inclusive end would index one past the array. `parallel-sum` follows that
  reading; if the lead pins the rule differently, its `expect.stdout` changes.
* **`console.input(prompt?)` prompt display is unspecified.** Whether the
  optional prompt is written to stdout, and with what framing, is not stated.
  The games call `console.input()` without the prompt and print their own
  instructions, so their stdout is exactly reproducible under the runner.
* **`if` / `else if` / `else` where every branch returns** is currently
  diagnosed as a (false) `CP3504: may fall off the end of its body` by the
  snapshot. The examples use early returns plus a final return instead of the
  chain — spec-correct either way, but this keeps them "waiting on the real
  feature, nothing else". Worth re-simplifying once the analysis is fixed.
* **Servers** are written against the pinned `std/network` surface in
  STDLIB-README: `createServer()`, `.use(async (req, res, next) => ...)`,
  `.get`/`.post`, route params via `req.params.get`, `res.text`/`res.json(data,
  status)`, `.listen(port, cb)`, and the documented per-request 500 boundary.
  Nothing was invented; where the doc leaves a question (e.g. WebSocket + HTTP
  on one port) the example simply does not go.
