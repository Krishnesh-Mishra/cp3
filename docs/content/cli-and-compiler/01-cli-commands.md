---
title: CLI commands
---
# CLI commands

The CP3 CLI runs project workflows and compiler utilities. The installed executable may be named `cp3` or `cp3c` — use whichever your environment installs. This page lists every command and the options worth knowing; the supported spelling for the full option list is always `cp3 help`:

```sh
cp3 help
```

`--help` is **not** accepted by the built-in commands themselves — `cp3 build --help` is a usage error. `cp3 help` (or `cp3 -h` / `cp3 --help`) prints usage. Help is accepted only by `cp3 docs`, `cp3 native-install`, and `cp3 toolchain help` (or `cp3 toolchain --help`); `cp3 lsp`/`cp3 dap` accept only `--stdio`.

## Everyday project workflow

```sh
cp3 init hello-app --yes
cd hello-app
cp3 check src/index.cp3
cp3 run src/index.cp3
```

`init` scaffolds the project (`package.json` + `src/index.cp3`). `check` parses and type-checks without generating C++. `run` builds and launches the program. `build` produces the optimized executable without running it. When a project manifest is present, commands use its `main` entry; an explicit source path overrides it.

## Commands

| Command | Purpose |
| --- | --- |
| `cp3 run <file.cp3> [args]` | Build (incremental, cached), execute, and propagate the program's exit status. |
| `cp3 build [file.cp3]` | Release build: compile and link, without running. |
| `cp3 warm [file.cp3]` | Build everything up front (per-module translation units, PCH, runtime units, executable) so the next `run`/`dev` is warm. The program is **not** run. |
| `cp3 check <file.cp3>` | Front-end only: parse, resolve, type-check. No C++ files are written. |
| `cp3 dev [file.cp3]` | Watch, build and restart on save (`--once` for a single pass). |
| `cp3 test [file.cp3...]` | Build and execute test files — those under `tests/` and `test/`, or `*.test.cp3` in the project root; prints `N passed, M failed` and exits non-zero on failure. |
| `cp3 fmt <file.cp3>` | Format CP3 source (zero-config). |
| `cp3 clean [--all]` | Remove `.cp3/build`, `.cp3/cache`, `.cp3/out`. `--all` also removes `.cp3/packages`, `.cp3/native` and `.cp3/lock.json`. It keeps `.cp3/native` by default. |
| `cp3 init [dir]` | Scaffold a new project (`package.json` + `src/index.cp3`). |
| `cp3 toolchain ...` | Choose and inspect the Clang compiler (`list`, `detect`, `use`, `clear`, `current`). |
| `cp3 install [src]` | Install a local `.cp3pak`/directory, or a GitHub release. |
| `cp3 update [name]` | Re-resolve locked GitHub dependencies to their latest release (`--dry-run` previews; `--json` for machines). |
| `cp3 upgrade` | Upgrade cp3 itself from the latest GitHub release (`--dry-run`, `--yes`); the asset is verified against release SHA256SUMS before install. |
| `cp3 publish [dir]` | Build a package and report/copy the `.cp3pak` (no registry). |
| `cp3 recompile <pak>` | Add this platform's cached binary to a `.cp3pak`. |
| `cp3 link <path>` / `cp3 unlink <name>` | Link/unlink a local package directory or `.cp3pak`; `--list` lists links, `--dry-run` previews. |
| `cp3 exec <pkg> [args]` | Run a package's bundled tool. |
| `cp3 lsp [--stdio]` | Language server over stdio (editor integrations). |
| `cp3 dap [--stdio]` | Debug adapter over stdio. |
| `cp3 docs [--port n]` | Serve the documentation embedded in the compiler (loopback; `--port 0` picks a free port). |
| `cp3 native-install <sdk>` | Generate CP3 bindings for a native SDK (requires libclang). |
| `cp3 version` | Print the compiler version. |
| `cp3 <file.cp3> [args]` | Shorthand for `cp3 run <file.cp3> [args]`; extras become program arguments. |

## Options

| Option | Applies to | Effect |
| --- | --- | --- |
| `--release` | `build` (default), `run` | Optimized whole-program build: `-O3`, LTO, native ISA, loop unrolling, PGO when a profile exists. |
| `--incremental` | `run` (default), `warm` (forced) | Fast cached build: `-O0`, no LTO/PGO, per-module translation units with a shared PCH. |
| `--debug` | `build`, `run` | `-O0` plus debug info. |
| `--no-lto` / `--no-native` / `--no-unroll` | release | Disable individual release optimizations. |
| `--strict-fp` | release | Keep math errno behavior. |
| `--opt-level <0-3\|s\|z>` | release | Override the `-O` level. |
| `--pgo <file>` | release | Consume a Clang `.profdata` profile. |
| `--pgo-generate[=<dir>]` | release only | Instrumented build for profile collection. |
| `--no-pgo` | release | Disable automatic `.cp3/pgo/default.profdata` use. |
| `--toolchain <clang[@version]\|path>` | all build commands | Use this Clang for this command only. |
| `--strict-toolchain` | build commands | Fail if a saved exact toolchain pin is missing. |
| `--project` / `--global` | `toolchain` | Save the choice in `package.json` / for all projects. |
| `--show-runtime` (alias `--show`) | `build`, `run` | Mirror the runtime sources used by this build into `.cp3/build/_runtime/` (show-only). |
| `--report` | `check` | Print the cycle-handling report (which classes may collect automatically, the manual `weak`-edge share). |
| `--max-memory <MB\|N%>` | build commands | Cap codegen-phase memory (default: 70% of system RAM). |
| `--verbose` | build commands | Print the selected toolchain and pipeline steps. |
| `--once` | `dev` | Build/run once, then exit. |
| `--all` | `clean` | Also remove `.cp3/packages`, `.cp3/native`, `.cp3/lock.json`. |
| `--yes`, `-y` | `init` | Accept defaults, never prompt. |
| `--type` / `--type-system` / `--author` / `--description` | `init` | Pre-select the scaffold answers. |
| `--auto` | `install` | Auto-pick the GitHub release asset. |
| `--dry-run` | `publish`, `update`, `upgrade` | Print what would happen. |
| `--json` | `publish`, `link`, `unlink`, `install`, `update` | Machine-readable output. |

Exit codes: `0` success, `1` compile error, `2` usage error. `cp3 run` propagates the program's exit code.

## Package scripts and hooks

Commands named like built-ins are reserved for the built-in commands. A script in `package.json` can be run by its name:

```sh
cp3 start
```

The name is looked up in the nearest `package.json`'s `scripts` object. `run`, `build`, `check`, `dev`, `fmt`, `clean`, and `install` honor npm-style lifecycle hooks from the same object: `pre<name>` runs before the command (it aborts on failure), `post<name>` runs only after a successful command (never after a failed one). Hooks are shell scripts; they are not run by `lsp`, `dap`, `native-install`, or `docs`, and nested CP3 calls made from inside a hook carry `CP3_SCRIPT_GUARD=1` and skip hooks.

`cp3 init` writes `start`, `verify`, and `build` script entries into the manifest (invoking `cp3c run|check|build` on the entry file). On a terminal it asks five questions with arrow-key menus for the two choice fields; `--yes` skips them. Existing scaffold files are never overwritten without `--force`.

## Related pages

- [Quick start](../quick-start.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Native toolchains](./05-native-toolchains.md)
- [Build profiles and PGO](./11-build-profiles-and-pgo.md)
- [Tests and verification](./08-tests-and-verification.md)
