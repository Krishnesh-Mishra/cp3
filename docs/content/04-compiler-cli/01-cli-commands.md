---
title: CLI commands
chapter: Chapter 4 · Compiler and tools
---
# CLI commands

The CP3 CLI runs project workflows and compiler utilities. The installed executable may be named `cp3` or `cp3c`; use the name installed for your environment. Start with the command's own help because flags can change between compiler versions:

```sh
cp3 help
cp3 build --help
```

## Everyday project workflow

```sh
cp3 init hello-app --yes
cd hello-app
cp3 check src/index.cp3
cp3 run src/index.cp3
```

`init` creates an application or package scaffold. `check` parses and type-checks source without generating or compiling C++. `run` builds as required and launches the executable. `build` compiles and links without running the result.

When a project manifest is present, commands can use its `main` entry. An explicit source path overrides the inferred entry. Run commands from the intended project root so the nearest `package.json` supplies the expected settings.

## Build commands

| Command | Purpose |
| --- | --- |
| `cp3 check <file>` | Front-end validation: parsing, module resolution, semantic checks, and type analysis. |
| `cp3 build [file]` | Generate native output and link an application, or package a project configured as a package. |
| `cp3 run <file>` | Build if necessary, then execute the program and propagate its exit status. |
| `cp3 dev [file]` | Watch a project and rerun its development check/build workflow; `--once` performs one pass. |
| `cp3 fmt <file>` | Format CP3 source using the project formatter. |
| `cp3 clean` | Remove `.cp3/build` and `.cp3/out`. |
| `cp3 clean --all` | Also remove `.cp3/packages` and `.cp3/native` caches. |

`clean` removes generated state, not source files. The `--all` option can remove package and native SDK caches, so use it when you intend to clear those too.

## Build profiles and inspection switches

Build and run accept profile and native compiler options. Common profile flags include:

- `--release`: optimized application build; this is the default for `build`.
- `--incremental`: favors fast cached development builds and disables LTO/PGO in the current CLI.
- `--debug`: uses `-O0` and debug information.
- `--no-lto`, `--no-native`, and `--no-unroll`: turn off selected release optimizations.
- `--strict-fp`: retains math errno behavior where the release profile would otherwise disable it.
- `--opt-level <0-3|s|z>`: overrides the optimization level.
- `--pgo <file>` and `--pgo-generate[=<dir>]`: consume or produce Clang profile data.
- `--no-pgo`: disables automatic PGO profile use.
- `--toolchain <clang[@version]|path>`: selects a Clang toolchain for one command.
- `--strict-toolchain`: fails if a saved exact toolchain choice is missing.
- `--verbose`: prints selected toolchain and pipeline details.
- `--show-runtime`: preserves runtime headers and translation units used by the application build.

Use `cp3 build --help` for the complete option set. Profile switches change generated machine code, so benchmark comparisons need the same profile and toolchain.

## Toolchain management

```sh
cp3 toolchain list
cp3 toolchain detect
cp3 toolchain use clang@18 --project
cp3 toolchain clear --project
```

`list` reports candidates and the effective selection; `detect` shows automatic discovery; `use` stores a project or global choice; `clear` removes a stored choice. `cp3 toolchain list --json` is available for scripts. See [Native toolchains](05-native-toolchains.md) for scope and precedence.

## Package commands

The CLI includes local and remote package workflows:

- `install` installs a local package archive/directory or supported remote release;
- `publish` builds package output and reports or copies the `.cp3pak` archive;
- `recompile` adds a native binary for the current platform to a package archive;
- `link` and `unlink` manage local package links;
- `exec` runs a bundled package tool.

Package operations can create caches or write output archives. Read each command's help before using it in automation, and use `--dry-run` when the command supports it and you want to inspect the planned work first.

## Native and editor commands

`native-install` creates bindings from a native SDK using libclang support. It depends on the SDK headers and platform libraries; read [Native packages and ABI boundaries](07-native-interop.md) before exposing foreign values to application code.

`lsp` starts the language server, usually over stdio for an editor integration. `dap` starts the debug adapter. `editor-plugin` manages the supported editor extension workflow. Their availability and options are versioned with the installed CLI.

## Documentation commands

`cp3 docs` serves the Markdown guide embedded in the compiler on loopback. `cp3 docs --port 0` asks the operating system for a free port and prints the selected address. The server runs until interrupted. See [Diagnostics and generated help](06-diagnostics.md) for a per-error help file, which is separate from the full docs server.

## Argument forwarding and exit status

Commands may accept `--` to separate CP3 CLI options from arguments that should be passed to the program or an external tool. Check that command's help before relying on argument forwarding. The CLI itself uses exit code `0` for success, `1` for compile errors, and `2` for usage errors; `run` can propagate the built program's exit code.

In shell scripts, preserve the command exit status and quote file paths and arguments. Avoid passing user data through a shell command string when a structured argument list is available.

## Project scripts

`cp3 init` writes `start`, `verify`, and `build` script entries into `package.json` (`cp3c run`, `cp3c check`, and `cp3c build` of the entry file). These are command strings provided as project metadata; CP3's own commands can be invoked directly and do not require an npm runtime. Read the generated script before connecting it to another task runner.

On a terminal, `cp3 init` asks five questions (type, name, description, author, and the type-system selection) with arrow-key menus for the two choice fields; `--yes` skips the questions entirely and the flags `--type`, `--type-system`, `--description`, and `--author` pre-select the answers for scripts. Existing scaffold files are left unchanged unless `--force` is passed.

## Related pages

- [Create and run your first project](../00-welcome/04-first-project.md)
- [Project settings and build cache](09-project-settings-and-cache.md)
- [Native toolchains](05-native-toolchains.md)
- [Build profiles and PGO](11-build-profiles-and-pgo.md)
- [Tests and compiler verification](08-tests-and-verification.md)
