---
title: Introduction
icon: Lightbulb
---
# Introduction

## What is CP3?

CP3 is a statically typed language that compiles your `.cp3` program ahead of time to a **native executable**. You write familiar TypeScript-shaped code; the compiler checks it, lowers it to C++, and hands it to Clang. What lands in your hands is a single machine binary — no VM, no bundled JavaScript runtime, no package to install before your program can run.

- **Familiar syntax** — `const`, `let`, typed parameters, classes, interfaces, modules, `async`/`await`.
- **Static checking** — the compiler rejects invalid assignments and calls before any native code is produced.
- **Managed lifetimes without a tracing GC** — reference counting plus compiler lifetime analysis. You never write `delete`.
- **All-core serving** — one line (`threads: os.cpus()`) gives you an event loop per logical CPU.

## What every other option makes you pay

You would not be reading this if some language already had everything. Each mainstream choice asks you to accept a different pain:

| Language | You get | You pay |
| --- | --- | --- |
| **C++** | Absolute control over the machine | Templates, smart pointers, header files, a build system, and decades of footguns — every abstraction is manual |
| **Rust** | Real memory safety at compile time | Ownership and the borrow checker gate every data structure; a small web service is a systems programming project |
| **Go** | Fast to write, fast to read | A tracing runtime behind every binary — pauses you do not control, in a language designed to make you forget the machine exists |
| **JS / TS (Node)** | The fastest path from idea to running server | A single-threaded V8 tax on everything, no ahead-of-time binary, and coercion semantics that bite at the worst times |
| **Python** | Ubiquitous and convenient | An interpreter tax on every hot path, a GIL, and a deployment story that means "install the right interpreter first" |

## Why CP3 exists

CP3 is the refusal to choose between that table.

You keep the code shape you already write — TypeScript-flavored app code, not systems code. The compiler keeps the checks — types are verified, not hinted. The backend keeps the metal — Clang emits machine code that a CPU actually executes. And the runtime keeps working for you — references clean themselves up, and a local escape analysis removes the work it can prove is unnecessary, so the common case never waits on a collector.

In the recorded health-server benchmark (one Windows 11 x64 machine, October 2026; treat it as a snapshot, not a law), CP3 served **15,719 req/s** single-worker and **81,815 req/s** all-core — level with hand-written C++, ahead of Go and Node, and about **9× Python** in the same single-worker run. The same server source ran on one event loop or on every core, by changing one option.

## What CP3 ships

Not features you read about and never touch — the ones your code will actually stand on:

- **CPU-heavy multithreading in one call.** Three heavy jobs, every core, no thread management:

  ```ts
  const [l, h, fib] = parallel.run(
    () => lcm(a, b),
    () => hcf(c, d),
    () => fibonacci(n),
  );
  ```

  `parallel.run` takes nullary closures and hands you their results in order. The checker rejects captured mutation and `async` closures, so the fast version is the safe version.

- **Dynamic arrays that stay native-fast.** `const a: string[] = ["hello"]; a.push("world");` — growable, contiguous storage with C++ managing capacity growth, an automatic reserve hint on proven counted loops, and fixed-width typed arrays for hot binary workloads.

- **An escape-analysis engine with teeth.** The compiler proves objects that never escape their frame, stores them directly, and skips the heap and reference counting entirely — the allocation simply does not happen. ([How it works →](./internal-engineering/03-escape-analysis.md))

| Capability | What you get | Why it matters |
| --- | --- | --- |
| **Escape analysis** | Proven-local objects stored without allocation | The common case costs zero ownership traffic ([details](./internal-engineering/03-escape-analysis.md)) |
| **`parallel.*`** | Checked reduction-style CPU parallelism | Spread heavy work across every core without data races by construction |
| **Dynamic arrays** | `push`, `reserve`, contiguous storage, typed arrays | Ergonomics of JS with `std::vector` costs underneath |
| **No tracing GC** | Reference counting + a non-tracing cycle collector | No stop-the-world pauses; deterministic lifetimes ([details](./internal-engineering/02-no-garbage-collector.md)) |
| **All-core servers** | `createServer({ threads: os.cpus() })` | One event loop per logical CPU, same source |
| **Static types** | Checked before anything is emitted | Type errors are compile-time facts, not editor hints |
| **One binary** | AOT C++ backend, clang toolchain | Nothing to install on the machine that runs your program |
| **Battle-tested substrate** | libuv, uWebSockets, OpenSSL, libsodium, PCRE2 | Entire classes of bugs live in code CP3 never wrote ([see the libraries](./internal-engineering/05-runtime-libraries.md)) |

The deep reason CP3 can compete with C++ and Rust here is blunt: it does not invent its own slow paths. TLS is OpenSSL, HTTP and WebSocket serving is uWebSockets, the reactor is libuv, crypto is libsodium — the same native libraries the rest of the industry benchmarks on. CP3 spends its own code where it adds value (the language, the checker, the escape analysis) and links the fastest thing available everywhere else. **See the full list and the wiring: [Runtime libraries](./internal-engineering/05-runtime-libraries.md).**

That is also the bug story: the genuinely hard parts of a networked language — TLS, HTTP parsing, regex, JSON numbers, crypto — are not reimplemented in CP3's runtime and then debugged into submission; they are inherited, pinned, and audited as they are. Fewer novel mechanisms, fewer novel failures.

## Sixty seconds to your first CP3 server

This is the whole program — a `/add` route that takes two numbers and returns their sum:

```ts
import { createServer } from "std/network";
import console from "std/console";

const app = createServer();

app.get("/add", (req, res) => {
  const a = parseFloat(req.query.get("a") ?? "0");
  const b = parseFloat(req.query.get("b") ?? "0");
  res.json({ result: a + b });
});

app.listen(3000, () => {
  console.log("listening on http://localhost:3000");
});
```

Run it:

```powershell
cp3c run add.cp3
```

And from another terminal:

```powershell
curl "http://localhost:3000/add?a=19&b=23"
```

```json
{"result":42}
```

Or build it once and hand it to someone who has never heard of CP3:

```powershell
cp3c build add.cp3
.cp3\out\add.exe
```

A native executable. No runtime to install, nothing else to ship. When you outgrow one core, change `createServer()` to `createServer({ threads: os.cpus() })` (import `os` from `std/os`) and the same source serves on every logical CPU.

That is the pitch in one example: code you already know how to write, checks you already want, output a CPU actually runs.

## What CP3 is not

Being straight with you about the limits:

- **Not Node, and not TypeScript.** `std/*` modules are CP3's own; npm packages do not import into CP3.
- **Not a tracing GC language.** Deterministic reference counting, plus a non-tracing cycle collector for islands the compiler proves safe — anything else is either rejected at compile time (CP3401) or broken with a `weak` edge, never silently collected by a background pass ([the details](./internal-engineering/02-no-garbage-collector.md)).
- **Not "the compiler will make it fast."** CP3 emits native code, but allocation, aliasing, and missed proof conditions still cost. The [optimization pipeline](./cli-and-compiler/03-optimization-pipeline.md) says what the compiler proves and what it does not.
- **Not every allocation on the stack.** Escape analysis handles proven-local cases ([the proof rules](./internal-engineering/03-escape-analysis.md)); everything else keeps the managed path.

## Where to go next

- [Quick start](./quick-start.md) — create, run, and build your first project.
- [For JavaScript and TypeScript developers](./for-js-developers.md) — the migration map from TS to CP3.
- [Internal Engineering](./internal-engineering/00-overview.md) — how CP3 is built, why there is no GC, and what the compiler proves for you.
- [Compiler design](./internal-engineering/01-compiler-design.md) — what happens between your `.cp3` file and the executable.
