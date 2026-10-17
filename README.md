# C+++

**TypeScript you already know - compiled to native code.** C+++ (`.cp3`) is a
statically-typed, JavaScript/TypeScript-flavored language that compiles to
C++20: no garbage collector, no pointers, no manual memory management, no
CMake - and no JIT runtime underneath your JS-shaped code.

The design goal, in the spec's own words: *"a competent JS/TS developer can be
productive in roughly two days, without learning pointers, manual memory
management, or CMake."* That is the whole bet - `let`/`const`, `interface`,
classes, generics, `async`/`await`, template literals and `console.log` keep
their TS shape; what changes is that the output is a native binary.

## Why it exists

- **The syntax tax is the real barrier.** C++, Rust, Go and Java all make a
  JS/TS developer relearn declarations, loops, classes and arrays - the same
  concepts in a different spelling. That relearning cost keeps people away,
  not the concepts themselves.
- **JS/TS pays a runtime tax.** A trivial Express server can sit at hundreds
  of MB of RAM because a full JIT runtime (GC, dynamic typing machinery,
  warmup, object shapes) runs underneath even the simplest handler.
- **Go kept the garbage collector.** GC pauses and GC overhead are exactly
  the cost C+++ avoids - not relocates.
- **Rust and C++ removed the GC but kept the steep syntax** (ownership and
  borrow-checking, manual memory and pointers).
- **C+++ targets the gap:** JS/TS syntax, native compilation, no GC. The
  genuinely hard C++ (Unreal, Qt, OpenCV internals) stays C++ behind the
  native-interop boundary - you don't write it to ship an app.

*(The full argument: [Why C+++?](docs/language-spec/00b-why-cp3.md) · [What C+++ combines](docs/language-spec/22-what-cp3-combines.md).)*

## Measured, not estimated

[`examples/competitions/`](examples/competitions/README.md) implements the
same small programs side by side in **cp3, C++, Rust, Go, JavaScript and
Python**; a stdlib-only runner builds, starts, measures and stops everything
and prints comparison tables. One real run (2026-10-17, Intel Core i7-12700H,
Windows 11; g++ 16.1.0, rustc 1.95.0, go 1.26.4, node v24.15.0, python 3.14.5,
cp3c 0.1.0-beta.1):

**One integer workload** - total Collatz steps for `1..1,000,000` (answer
`131434424`, byte-identical in every language), single-threaded, best of two
runs:

| Rust | C++ | Go | JS (Node) | **cp3** | Python |
|---:|---:|---:|---:|---:|---:|
| 0.13 s | 0.15 s | 0.20 s | 1.13 s | **1.16 s** | 10.13 s |

The cp3 row runs today, no asterisk: it prints the same integer, matches
Node's JIT-assisted 1.13 s, and beats CPython ≈8.7×. The ≈8× gap to Rust/C++
is codegen maturity - closing it is roadmap work on the generated C++, not a
language redesign.

Same total split across all cores: Rust 19 ms · C++ 24 ms · Go 33 ms ·
Node 262 ms · Python 1.34 s - the cp3 entry joins that table when
`parallel.*` lands.

**The Express comparison.** The same health server, five runtimes measured at
8 keep-alive clients for 3 s (sources side-by-side in
[`health-server/`](examples/competitions/health-server/)):

| C++ | Rust | Python | Express | Go | **cp3** |
|---:|---:|---:|---:|---:|---:|
| 10,498 req/s | 9,675 req/s | 9,491 req/s | 9,336 req/s | 9,284 req/s | *pending `std/network`* |

On a trivial handler the load client and loopback dominate, so those rows
cluster; the compute tables are where the runtimes separate. The cp3 entry is
already written against the frozen `std/network` API - when the module lands,
its row fills itself in, unchanged.

Full methodology, caveats and the raw runner output:
[`examples/competitions/README.md`](examples/competitions/README.md).

## This is the language

```ts
// hello - the smallest program
import console from "std/console";

console.log("Hello, world!");
```

```ts
// async + timers are built into the language (the event loop is part of it)
async function countdown(from: number): Promise<void> {
  let n = from;
  while (n > 0) {
    console.log(`tick ${n}`);
    await sleep(100);
    n = n - 1;
  }
  console.log("liftoff");
}

countdown(3);
```

```ts
// the compute-single benchmark - same workload as the table above
function collatzSteps(start: number): number {
  let n = start;
  let steps = 0;
  while (n !== 1) {
    if (n % 2 === 0) {
      n = n / 2;
    } else {
      n = 3 * n + 1;
    }
    steps = steps + 1;
  }
  return steps;
}
```

Same function, other languages:
[C++](examples/competitions/compute-single/cpp/main.cpp) ·
[Rust](examples/competitions/compute-single/rust/main.rs) ·
[Go](examples/competitions/compute-single/go/main.go) ·
[JavaScript](examples/competitions/compute-single/js/main.mjs) ·
[Python](examples/competitions/compute-single/python/main.py).

And the Express-vs-cp3 shape, in code
([JS](examples/competitions/health-server/js/express/server.mjs) vs
[cp3](examples/competitions/health-server/cp3/main.cp3)):

```js
// Express (JavaScript)
import express from "express";
const app = express();

app.get("/healthz", (req, res) => { res.type("text/plain").send("ok"); });
app.get("/",        (req, res) => { res.type("text/plain").send("health-server ok"); });
app.use(            (req, res) => { res.status(404).type("text/plain").send("not found"); });
```

```ts
// cp3 (native)
import { createServer } from "std/network";
const app = createServer();

app.get("/healthz", (req, res) => { res.text("ok"); });
app.get("/",        (req, res) => { res.text("health-server ok"); });
app.use(            (req, res, next) => { res.status(404).text("not found"); });
```

## What runs today

The compiler is built wave by wave against a frozen spec, with a
spec-conformance corpus (1,200+ case pairs in
[`tests/cases/`](tests/cases/)) as the automated oracle.

- **Landed:** the full pipeline (lexer → parser → sema → codegen → CLI), the
  diagnostic model, error handling, `async`/`await` + timers + the event
  loop, `std/console`, and builtin methods - `Math`, strings, `path`, `os`,
  `JSON.stringify`, `process`, `String(x)`.
- **In flight / next:** a performance wave (`%`/`number` lowering - see the
  benchmark section), then classes, arrays, `std/network` (servers) and
  `parallel.*`.

## Build and run it

```bash
cmake -S . -B build -G Ninja      # CMake >= 3.20 + a C++20 compiler
cmake --build build

build/bin/cp3c run examples/basics/hello-world/main.cp3   # cp3c.exe on Windows
```

Run the whole cross-language suite yourself (Python 3 only, no packages):

```bash
python examples/competitions/bench/run_suite.py
```

## Repository map

| Path | What |
| --- | --- |
| `docs/` | the browsable docs: `getting-started/` (TS → C+++ in two days), `language-spec/`, `cli-tooling/`, `standard-library/` |
| `docs/rough/` | internal process docs - `DECISIONS.md`, `M2-PLAN.md`, `STYLE.md`, `ARCHITECTURE.md` and the original single-file spec sources. **All rough work goes here; the repo root stays lean.** |
| `src/` | the compiler - `lexer`, `parser`, `sema`, `codegen`, `cli`, `runtime`, `modules`, `common` |
| `tests/` | `cases/` (the conformance corpus) + `runner/` (the harness) |
| `examples/` | showcase programs + `competitions/` (the benchmark suite above) |
| `tools/` | layout linter and helpers |

Start reading at
[From TypeScript to C+++ in two days](docs/getting-started/00-overview.md) or
the [full documentation index](docs/index.md).

---

*Every performance number in this README is a real measurement from a labeled
run recorded in [`examples/competitions/README.md`](examples/competitions/README.md) -
nothing here is estimated or extrapolated.*
