# CP3

CP3 is a statically typed language with JavaScript and TypeScript shaped syntax. The compiler translates `.cp3` source into C++ and builds a native executable with a configured C++ toolchain.

## Documentation

The language guide is maintained as chapter-organized Markdown in [`docs/content`](docs/content), with the viewer in [`docs/viewer/index.html`](docs/viewer/index.html). CMake embeds both the viewer and every Markdown page into `cp3c` at build time, so the compiler does not need a separate docs installation:

```sh
cp3 docs
```

The local server binds to `127.0.0.1` and prints its URL. Select another port with `cp3 docs --port 4300`.

## Build the compiler

Requirements: CMake, a C++20 compiler, and Python 3 for the repository's test and benchmark runners.

```sh
cmake -S . -B build
cmake --build build
```

The compiler executable is `build/bin/cp3c` (Windows: `build/bin/cp3c.exe`). The `cp3` command is available when the project shim or installed launcher is on `PATH`.

## First project

```sh
cp3 init hello-app
cd hello-app
cp3 check src/index.cp3
cp3 run src/index.cp3
```

Use `cp3 help` for the complete command list and `cp3 toolchain` to inspect or choose a Clang toolchain.

## Repository layout

| Path | Contents |
| --- | --- |
| `docs/content/` | Markdown chapters and standard-library reference |
| `docs/viewer/index.html` | Source for the built-in documentation viewer |
| `src/` | Compiler, CLI, semantic analysis, code generation, and runtime |
| `tests/` | Language conformance and CLI test runners |
| `examples/` | Small CP3 programs and demonstrations |
| `benchmarks/` | Cross-language performance workloads and runner |

