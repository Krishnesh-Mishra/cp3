---
title: Create and run your first project
chapter: Welcome
---
# Create and run your first project

The CLI scaffolds a small project and gives you a direct check, run, and build path.

## Create the project

```sh
cp3 init hello-app
cd hello-app
```

The scaffold includes project metadata and a `src/index.cp3` entry. The default application entry imports `std/console` and prints a greeting. For scripts and automation, pass `--yes` to accept the defaults without prompts.

## Check before building

```sh
cp3 check src/index.cp3
```

`check` lexes, parses, resolves modules, and type-checks the source. It does not generate C++ or link an executable. A diagnostic points to the source position and includes a stable code.

## Run the source

```sh
cp3 run src/index.cp3
```

`run` compiles the program and executes the result. The compiler caches intermediate build products in the project’s `.cp3/build` directory so later builds can reuse unchanged work.

## Build a release executable

```sh
cp3 build src/index.cp3 --release
```

The release profile enables optimization options supported by the selected toolchain. Read the [CLI command reference](#04-compiler-cli/01-cli-commands) and the command's `--help` output for switches available in your installed version.

## Required tools

Building CP3 from its repository requires CMake and a C++20-capable compiler. Building CP3 programs additionally uses the configured native toolchain, normally Clang. The language compiler can show the selected toolchain with `cp3 toolchain list` and can change it through `cp3 toolchain`.

## If something fails

- If `cp3` is not found, use the installed launcher or invoke `cp3c` directly.
- If the native compiler is missing, configure one with `cp3 toolchain`.
- If an import cannot be resolved, verify its spelling and whether the module is part of the [supported module list](#03-standard-library/00-overview).
- If a type check fails, open the generated diagnostic help page and read the closest guide sections it suggests.
