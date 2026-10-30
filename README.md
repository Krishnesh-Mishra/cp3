# CP3

### Familiar syntax. Static checks. Native executables.

CP3 is a statically typed language with a JavaScript and TypeScript shaped syntax. It compiles `.cp3` programs to C++ and then to native executables, with an automatic reference-managed runtime and a growing standard library.

> **Project status: beta.** CP3 is actively developed. The language, compiler, and standard library are still evolving; check the documented support and benchmark conditions before choosing it for a production workload.

[Get started](#quick-start) · [Why CP3](#why-cp3) · [Performance](#performance-snapshot) · [Examples](examples/README.md) · [Full guide](docs/content/00-welcome/00-welcome.md)

---

## Why CP3?

CP3 aims to make native compilation approachable to developers who already know JavaScript or TypeScript. You write CP3 source; the compiler handles type checking, C++ generation, native compilation, and linking.

| You want… | CP3 provides… |
| --- | --- |
| A syntax that feels familiar | `const`, `let`, typed functions, classes, interfaces, modules, and async syntax. |
| Errors before you run the program | Static analysis and type checking as part of `cp3 check` and builds. |
| A native executable | Ahead-of-time compilation through a configured C++ toolchain; no JavaScript VM is required to run the built program. |
| Managed object lifetimes without `delete` in application code | Runtime-managed references. CP3 does not use a tracing garbage collector; its normal reference model is reference counted. |
| A small, documented platform API | CP3-owned `std/*` modules, plus explicit native SDK tooling. Node and npm APIs are not implicitly available. |

CP3 keeps the source experience high-level while compiling ahead of time. It does **not** promise that every program matches hand-written C++ or Rust, that every allocation is stack allocated, or that native compilation automatically vectorizes every loop. Performance depends on the workload and generated code.

### Memory model, plainly

CP3 manages reference lifetimes automatically. Most ordinary reference-shaped values use shared ownership; copies share the same object. The runtime uses reference counting rather than tracing collection, so strong reference cycles need weak links or another explicit ownership design. The compiler has narrow local-storage and non-atomic-reference optimizations, but they apply only when its current analysis proves a safe case.

This is not Rust's ownership and borrowing system, and it is not a tracing-GC runtime like Go's. Each model has different tradeoffs; start with the [runtime guide](docs/content/02-runtime/00-overview.md) for CP3's current behavior.

## A small CP3 server

This is the health endpoint from [`examples/servers/healthz`](examples/servers/healthz/main.cp3):

```ts
import { createServer } from "std/network";

const app = createServer();

app.get("/healthz", (req, res) => {
  res.text("ok");
});

app.get("/", (req, res) => {
  res.json({ service: "cp3-app", status: "ok" });
});

app.listen(3000, () => {
  console.log("listening on http://localhost:3000");
});
```

The full [echo API example](examples/servers/echo-api/main.cp3) adds middleware, route parameters, typed JSON request bodies, and response handling. The network module's exact surface and error behavior are documented in the [standard-library guide](docs/content/03-standard-library/13-network.md).

## Quick start

### Build CP3 from source

Requirements: CMake, a C++20-capable compiler, and Python 3 for the repository's test and benchmark runners.

```sh
cmake -S . -B build
cmake --build build
```

The compiler executable is `build/bin/cp3c` (`build/bin/cp3c.exe` on Windows). Building CP3 applications also requires a supported native application toolchain; see [Native toolchains](docs/content/04-compiler-cli/05-native-toolchains.md).

### Create and run a project

With the `cp3` launcher on your `PATH`:

```sh
cp3 init hello-app --yes
cd hello-app
cp3 check src/index.cp3
cp3 run src/index.cp3
```

Or invoke the compiler built from this checkout:

```sh
build/bin/cp3c.exe init hello-app --yes
build/bin/cp3c.exe run hello-app/src/index.cp3
```

`cp3 check` validates source without compiling the generated C++. `cp3 run` builds as needed and launches the resulting executable. See [Create your first project](docs/content/00-welcome/04-first-project.md) for project layout, manifest settings, and build profiles.

## Performance snapshot

The benchmark suite implements the same named tasks in CP3, C++, Rust, Go, JavaScript, Java, and Python where a task is supported. It checks output against an oracle and records runtime, variation, memory, and executable size. These are results from one Windows 11 x64 machine and toolchain set, not universal language rankings.

| Workload | CP3 result | Comparison in the recorded run | Measurement note |
| --- | ---: | --- | --- |
| `compute-single` | 151.8 ms | Rust 224.9 ms; C++ 251.4 ms | 7 repetitions; CP3 CV 27.1%; output verified. |
| `string-churn` | 394.8 ms | Rust 427.0 ms; C++ 2744.2 ms | 7 repetitions; CP3 CV 12.3%; output verified. This is one specific string workload. |
| `health-server` | 15,719 requests/s | C++ 14,858; Go 14,066; Rust 14,016 | One captured run; useful as a snapshot, not a stable ranking. |

The full results also show workloads where CP3 is slower, including JSON parsing, binary trees, UTF-8 processing, and some allocation or library tasks. Several results have high run-to-run variation. Read the [benchmark results and methodology](benchmarks/README.md) before drawing conclusions; the [performance guide](docs/content/00-welcome/02-performance.md) explains the numbers and their limits.

Run a focused benchmark from the repository root:

```sh
python benchmarks/runner/run_benchmark.py --only compute-single --reps 7
```

Benchmark source files live in `benchmarks/cases/`. The runner verifies their output and reports the compiler/toolchain conditions with the measurements.

## Explore CP3

- [JS/TS developer guide](docs/content/00-welcome/03-javascript-typescript-developers.md) — a migration-oriented overview of familiar syntax and differences.
- [Language guide](docs/content/01-language/00-overview.md) — declarations, types, functions, classes, generics, modules, and errors.
- [Standard library](docs/content/03-standard-library/00-overview.md) — console, process, filesystem, network, collections, buffers, and more.
- [Runtime model](docs/content/02-runtime/00-overview.md) — values, references, closures, arrays, concurrency, and memory lifetime.
- [Compiler and CLI](docs/content/04-compiler-cli/00-overview.md) — build workflow, diagnostics, code generation, and verification.
- [Examples](examples/README.md) — small programs, interactive demos, async tasks, and servers.
- [Benchmark suite](benchmarks/README.md) — workloads, runners, and measurement details.

The complete guide is also bundled into the compiler. Run `cp3 docs` to start the local viewer, or `cp3 docs --port 0` to let the operating system choose an available port.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Compiler front end, semantic analysis, code generation, CLI, runtime, and standard modules |
| `tests/` | Language, runtime, and CLI conformance tests |
| `examples/` | Runnable language, concurrency, async, and server examples |
| `docs/content/` | Chapter-organized Markdown guide embedded in the compiler |
| `benchmarks/` | Cross-language workloads, runner, and recorded results |

## Contributing

Compiler changes should include focused positive and negative test cases when language behavior changes. Performance changes should preserve benchmark source and expected output, then report repeated results and the generated-code evidence for the claimed optimization. See [Tests and verification](docs/content/04-compiler-cli/08-tests-and-verification.md) and [Generated C++ inspection](docs/content/04-compiler-cli/10-generated-cpp-inspection.md).

---

CP3 is an open-source language project under active development. Feedback, small reproducers, and carefully measured performance reports are welcome.
