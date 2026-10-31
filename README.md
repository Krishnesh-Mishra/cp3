# CP3

### Familiar syntax. Static checks. Native executables. Multicore servers.

CP3 is a statically typed language with TypeScript-shaped syntax. It compiles your `.cp3` program ahead of time to a native executable, while managing object lifetimes for you.

[Get started](#quick-start) · [Why CP3](#why-cp3) · [Benchmarks](#http-server-benchmarks) · [Examples](examples/README.md) · [Language guide](docs/content/00-welcome/00-welcome.md)

> **Project status: beta.** CP3 and its standard library are under active development. The benchmark results below are a dated snapshot from one machine; see the [benchmark results and methodology](benchmarks/README.md) for the full data and measurement details.

---

## Why CP3?

CP3 aims to combine a familiar application-code style with static checks and native compilation.

| If you want… | CP3 gives you… |
| --- | --- |
| A familiar way to write programs | `const`, `let`, typed functions, classes, interfaces, modules, and async code. |
| Type errors before running the program | Static analysis through `cp3 check`, `cp3 build`, and `cp3 run`. |
| A native program | Ahead-of-time compilation to a native executable; no JavaScript VM is needed to run the built program. |
| Managed object lifetimes | Reference-counted references and compiler lifetime analysis, without a tracing garbage collector or manual `delete` in application code. |
| Multicore HTTP serving | Set the `threads` option on `createServer`; CP3 starts the event loops for you. |
| Explicit types | CP3 checks declared and inferred types instead of offering a catch-all `any` type. |

### The memory model, in plain words

You should be able to focus on what your program does, not on remembering which object needs a matching `delete`. So in CP3, you can write the kind of code you already know:

```ts
const person = { name: "Rahul" };
let scores: number[] = [1, 2, 3];
scores.push(4);
```

You do not have to manually allocate and free ordinary objects. CP3 keeps an object alive while strong references point to it, then releases it when the last one goes away. That is reference counting: automatic lifetime management without a tracing garbage collector.

The compiler also looks at how long each object is actually needed. When it can prove that a temporary stays local, it can avoid some of the usual heap and reference-counting work. When a value needs to live longer, CP3 keeps it in managed storage. If the compiler cannot prove a shorter lifetime is safe, it keeps the managed path; it does not promise that every object is stack allocated.

Arrays are growable and use contiguous native storage. You can keep using `push`; the runtime takes care of reserving more room as the array grows. Types are checked before the program runs, so CP3 does not need a catch-all `any` type to make this style convenient.

One small detail is worth knowing: as in other reference-counted systems, a cycle of strong references can keep objects alive. Use weak references when objects need to refer back to each other without extending each other's lifetime.

You get to describe your program and its data. CP3 takes care of the routine lifetime work, and applies faster local-storage choices only when it can prove they are safe.

## HTTP server benchmarks

The same small health-server contract was measured in single-worker and all-core configurations: `GET /healthz` returns `ok`, `GET /` returns `health-server ok`, and other paths return `404 not found`.

| Language / server | Single-worker req/s | All-core req/s |
| --- | ---: | ---: |
| **CP3** (`std/network`) | **15,719** | **81,815** |
| C++ | 14,858 | 81,344 |
| Rust (Axum) | 14,016 | 73,709 |
| Go (Gin) | 14,066 | 65,450 |
| Java (Undertow) | 14,863 | 70,038 |
| JavaScript (Express) | 13,980 | 58,165 |
| JavaScript (Fastify) | 15,054 | 70,671 |
| Python (FastAPI) | 1,816 | 12,474 |

These are the recorded results for [`health-server`](benchmarks/results.md#health-server) and [`health-server-multi`](benchmarks/results.md#health-server-multi), captured on one Windows 11 x64 machine on October 30, 2026. Each language has one server trial in that capture, so treat the figures as a useful snapshot rather than a stable ranking. The harness checks the HTTP responses and records the selected framework and machine details in the [benchmark report](benchmarks/results.md).

### One line enables all-core server workers

The normal server uses one event loop:

```ts
const app = createServer();
```

To start one event loop per logical CPU, import `os` and change that line to:

```ts
const app = createServer({ threads: os.cpus() });
```

The server creates and manages its worker event loops; you do not need to write thread creation, request dispatch, or a worker pool. Each loop has its own server instance and module-level state, so do not use this option to assume mutable globals are shared between workers. `os.cpus()` reports logical CPUs; choose an explicit thread count when the machine's available CPU quota is smaller.

### A small server

This is a complete `std/network` health server. Use `createServer()` for the single-worker form, or the all-core line above for the multicore form.

```ts
import { createServer } from "std/network";
import console from "std/console";
import os from "std/os";

const app = createServer({ threads: os.cpus() });

app.get("/healthz", (req, res) => {
  res.text("ok");
});

app.get("/", (req, res) => {
  res.json({ service: "cp3-health", status: "ok" });
});

app.use((req, res, next) => {
  res.status(404).text("not found");
});

app.listen(3000, () => {
  console.log("listening on http://localhost:3000");
});
```

The complete network API, including middleware, request data, response helpers, and server lifecycle, is in the [network guide](docs/content/03-standard-library/13-network.md). The [echo API example](examples/servers/echo-api/main.cp3) shows request-body handling.

## Quick start

With the CP3 CLI and a supported native application toolchain installed:

1. Create an application:

   ```sh
   cp3 init health-server --yes
   cd health-server
   ```

2. Replace `src/index.cp3` with the server example above.

3. Build and start the app:

   ```sh
   cp3 run src/index.cp3
   ```

For an optimized release executable, use:

```sh
cp3 build src/index.cp3 --release
```

The `cp3` commands build your application. You do **not** need to build the CP3 compiler from source to use the language. CMake instructions belong to compiler development; see the [compiler and CLI guide](docs/content/04-compiler-cli/00-overview.md) if you want to work on CP3 itself.

Check that the server is responding:

```sh
curl http://localhost:3000/healthz
```

It returns `ok`.

## More benchmarks

The full suite compares CP3 with C++, Rust, Go, JavaScript, Java, and Python on compute, collections, strings, buffers, and server workloads. Some tasks favor CP3, while others expose areas still being improved. Every benchmark has a defined workload and expected output; read the [full results](benchmarks/results.md) and [measurement methodology](benchmarks/README.md) before drawing broader conclusions.

Run a focused benchmark from a CP3 source checkout:

```sh
python benchmarks/runner/run_benchmark.py --only health-server health-server-multi --reps 7
```


## Story Time

You can completly skip this area if you want. First of all many people may say "How many claude token did you use?", so yes i use ai to code lots of it. I accept it Completely,  more than 70% of code is written by AI , but dont think that saying "Hey Claude Make a new Lanugage Named CP3 which , make no mistakes" work till this date , acctually you can see the docs/rough/Readme (6).md that is written manually then also these dumb ai cant create proper lanuguage so it was a tricky and complex process. Explaining how to deisgn lexer, Explaining why we need parser and not put a Qwen 4b model in the compiler q4 to convert the code to cpp (Yes this was a suggession of a Frontier Model charging 100s of USD a month) , how to show errors , why we need test cases if our 'compiler is Great' etc. 

So Feel free to enjoy the language , Acctually I dont like learning new languages. Initially I learned MERN & then made on of my SaaS, but scaling issues came lots of bill, too much ram cunsumption, Thouhgt to switch to `GO` whic hshould have been best idea but there were 2 problems one to learn GO which is quite tough for me who still till date doesnt know how to center a div or how to do proper binary search and uses stack overflow / chatGPT to write code snippits. Then i read more there were still 2 problems in GO one is GC which can cause sudden slowness in server means its not that efficient and secondly it wasnt as powerful as rust/cpp see the results.md but i can show you bit here also , See Below.


```text
  ◆ health-server  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────────────────────────────────
  cp3            1/1    536.8    0.36    0.50  15,719        -       0      baseline      9.5     1616
  js-fastify     1/1    555.7    0.19    0.56  15,054        -       0  1.04x slower     68.1        -
  java           1/1   1191.1    0.34    0.75  14,863        -       0  1.06x slower    269.2        4
  cpp-drogon     1/1    528.4    0.22    0.34  14,858        -       0  1.06x slower     10.9       42
  go-gin         1/1    509.4    0.17    0.29  14,066        -       0  1.12x slower     21.2    15630
  rust-axum      1/1    538.2    0.13    0.26  14,016        -       0  1.12x slower      6.9     1098
  js-express     1/1    568.9    0.36    0.53  13,980        -       0  1.12x slower     62.4        -
  python-fastapi 1/1   1644.7    0.89    1.18   1,816        -       0  8.66x slower      4.1        -
``` 
```text
  ◆ health-server-multi  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────────────────────────────────
  cp3            1/1    520.6    0.31    0.43  81,815        -       0      baseline     21.1     1620
  cpp-drogon     1/1    523.5    0.23    0.35  81,344        -       0  1.01x slower     14.0       42
  rust-axum      1/1    546.6    0.17    0.46  73,709        -       0  1.11x slower      8.4     1098
  js-fastify     1/1   1032.6    0.27    0.45  70,671        -       0  1.16x slower     54.3        -
  java           1/1   1134.4    0.37    0.75  70,038        -       0  1.17x slower    346.8        4
  go-gin         1/1    521.4    0.13    0.32  65,450        -       0  1.25x slower     23.6    15630
  js-express     1/1   1076.1    0.46    0.75  58,165        -       0  1.41x slower     50.7        -
  python-fastapi 1/1   3166.0    1.00    1.65  12,474        -       0  6.56x slower      4.1        -
```
_Updated 2026-10-30T16:57:25 by `python benchmarks/runner/run_benchmark.py`._

So now there is a bad issue i need to learn a new language and its not that powerful also shoudn't I learn cpp/rust, Yes thats what i did Searched Youtube for a 20h CPP tutorial, and made a small projects then server and few parts of my SaaS, Then I realised that most of the time i am not using manual memory things like malloww w50% of time i am using stack based things and otehr wwise using libraries like vector etc. and reaming 25% time only i am using pointers, malloc etc. May be because I am not building GTA VII so i dont need that level of perfromance. But here were 2 Problems in CPP first of all It scares a new developer , Reading those `::` is wild and not clean , second is why to write .hpp files they are very irritating (Senior People don't abuse me please , this is my personal opinion. Also CPP Support Direct Import/Export I know but then also most reouces on internet is for .cpp/.hpp only). So I thought cant we use all of the things and make a new thing.

I didnt make a language I just made a Breeding Center.

Used CPP as internal language , Because i dont know how to write LLVM Intermediate Representation (IR) , So I simply did what i knew.
I Also knew I cant make any better server package than uWS so why cant we take it also.
Similarly use Vectors, LibSodium , LibUV etc all the things are previously made So there will be no bugs in them or vurnebility. 

I combined them and this repo was born.

So You have the one of the highest perfroming Language & One of the most Easy Language .
One Small Note : Mojo is also doing same iwth Python as i have seen but i dont like python because i have a curved moniter and my plastic scale is not curve to measure the tabs. and mojo kept Memory Model quite complicated for TS devs, Here We do Something like Apple's Switch ARC does .This is not the best explanation but you get the Idea. 



## Explore CP3 

- [JS/TS developer guide](docs/content/00-welcome/03-javascript-typescript-developers.md) — familiar syntax and differences.
- [Language guide](docs/content/01-language/00-overview.md) — declarations, types, functions, classes, modules, and errors.
- [Runtime model](docs/content/02-runtime/00-overview.md) — references, arrays, closures, concurrency, and lifetimes.
- [Standard library](docs/content/03-standard-library/00-overview.md) — console, process, filesystem, network, collections, and buffers.
- [Compiler and CLI](docs/content/04-compiler-cli/00-overview.md) — project builds, diagnostics, code generation, and verification.
- [Examples](examples/README.md) — runnable programs, concurrency examples, and servers.
- [Benchmarks](benchmarks/README.md) — tasks, runners, methodology, and results.

The complete guide is also bundled with the compiler. Run `cp3 docs` to start the local documentation viewer.

## Contributing

Language and compiler changes should include focused positive and negative tests. Performance changes should preserve benchmark sources and expected output, and include repeated measurements plus generated-code evidence for claimed optimizations. See [tests and verification](docs/content/04-compiler-cli/08-tests-and-verification.md) and [generated C++ inspection](docs/content/04-compiler-cli/10-generated-cpp-inspection.md).

---

CP3 is an open-source language project under active development. Reports with a small reproducer and measured results are especially useful.
