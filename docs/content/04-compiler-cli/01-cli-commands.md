---
title: CLI commands
chapter: Chapter 4 · Compiler and tools
---
# CLI commands

The CP3 CLI runs project workflows and compiler utilities. The installed executable may be named `cp3` or `cp3c`; use the name installed for your environment. Flags can change between compiler versions, and the supported spelling for the full option list is `cp3 help`:

```sh
cp3 help
```

`cp3 help` (also `cp3 -h` / `cp3 --help`) prints usage and every flag. `--help` is **not** accepted by the built-in commands themselves — `cp3 build --help` is a usage error (`unknown option '--help'`, exit code 2). `--help` is accepted only by `cp3 docs`, `cp3 native-install`, and `cp3 toolchain help` (or `cp3 toolchain --help`); `cp3 lsp`/`cp3 dap` accept only `--stdio`.

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
| `cp3 warm [file]` | Build every artifact for the development loop (plural translation units, the shared PCH, the runtime translation units, and the executable) and cache them under `.cp3/`; the program is not run. Use before a long editing session so the first `cp3 run`/`cp3 dev` pass is fully warm. |
| `cp3 dev [file]` | Watch a project and rerun its development check/build workflow; `--once` performs one pass. |
| `cp3 fmt <file>` | Format CP3 source using the project formatter. |
| `cp3 clean` | Remove `.cp3/build` and `.cp3/out`. |
| `cp3 clean --all` | Also remove `.cp3/packages`, `.cp3/lock.json`, and `.cp3/native` caches. |
| `cp3 <file.cp3> [args]` | Same as `cp3 run <file.cp3> [args]`; extras become program arguments. |

`clean` removes generated state, not source files. The `--all` option can remove package, package-lock, and native SDK caches, so use it when you intend to clear those too.

## Build profiles and inspection switches

Build and run accept profile and native compiler options. Common profile flags include:

- `--release`: optimized application build; this is the default for `build`.
- `--incremental`: favors fast cached development builds and disables LTO/PGO in the current CLI.
- `--debug`: uses `-O0` and debug information.
- `--no-lto`, `--no-native`, and `--no-unroll`: turn off selected release optimizations.
- `--strict-fp`: retains math errno behavior where the release profile would otherwise disable it.
- `--opt-level <0-3|s|z>`: overrides the optimization level.
- `--pgo <file>` and `--pgo-generate[=<dir>]`: consume or produce Clang profile data; `--pgo-generate` requires the release profile (`--release`), the two are mutually exclusive, and `--pgo-generate=<dir>` requires a non-empty directory.
- `--no-pgo`: disables automatic PGO profile use.
- `--report`: accepted by the option parser for every built-in command but acted on by `cp3 check`: prints the cycle-decision report (which classes can reach themselves through strong edges, the escape sites, and the manual `weak`-edge share) with reasons. Passing it to other commands has no effect.
- `--max-memory <MB|N%>`: caps codegen-phase memory (default is 70% of system RAM); accepts a megabyte number or a percentage of physical memory.
- `--toolchain <clang[@version]|path>`: selects a Clang toolchain for one command.
- `--strict-toolchain`: fails if a saved exact toolchain choice is missing.
- `--verbose`: prints selected toolchain and pipeline details.
- `--show-runtime`: preserves runtime headers and translation units used by the application build.

Use `cp3 help` for the complete option set. Profile switches change generated machine code, so benchmark comparisons need the same profile and toolchain.

## Toolchain management

```sh
cp3 toolchain list
cp3 toolchain detect
cp3 toolchain current
cp3 toolchain use clang@18 --project
cp3 toolchain clear --project
```

`list` reports candidates and the effective selection; `detect` shows automatic discovery; `current` prints a single line naming the effective toolchain and where the choice came from (the packaging shims consume it verbatim); `use` stores a project or global choice; `clear` removes a stored choice. `cp3 toolchain list --json` is available for scripts. `cp3 toolchain help` (or `cp3 toolchain --help`) prints the toolchain usage. See [Native toolchains](./05-native-toolchains.md) for scope and precedence.

## Package commands

The CLI includes local and remote package workflows:

- `install` installs a local package archive/directory or supported remote release;
- `update` re-resolves the GitHub dependencies locked in `.cp3/lock.json` to the latest release of the same repository and reinstalls them (`--dry-run` reports without installing; `--json` for machine output); `file:` and linked dependencies are left alone; like `install`, `update` does not contact the network unless there is a GitHub dependency to consider;
- `upgrade` reinstalls cp3 itself from the latest public GitHub release of this repository (`--dry-run` prints the plan; `--yes` skips the confirmation, which is required off a TTY). The asset is verified against the release's SHA256SUMS before any installer is launched:
  - Windows: runs the release MSI with `msiexec /qb` (the MSI replaces the per-user installation in place);
  - Linux: extracts the release tarball and runs its `install.sh --force`, replacing the per-user install;
  - ARM64 releases are not published yet, and `cp3 upgrade` says so rather than installing.
  - At most one network check per 24 h feeds the one-line reminder printed at the end of ordinary commands (`cp3: X.Y.Z -> vA.B.C available; run \`cp3 upgrade\``); it never blocks, and `CP3_NO_UPDATE_CHECK=1` disables it entirely.
  - GitHub's unauthenticated API is rate-limited (60 requests/hour). If you use the GitHub CLI, `gh auth login` is enough - cp3 reuses the CLI's stored credential. Otherwise set `CP3_GITHUB_TOKEN` (a token with no scopes is fine) or try later. `GH_TOKEN` and `GITHUB_TOKEN` are also honored.
- `publish` builds package output and reports or copies the `.cp3pak` archive;
- `recompile` adds a native binary for the current platform to a package archive;
- `link` and `unlink` manage local package links;
- `exec` runs a bundled package tool.

Package operations can create caches or write output archives. Consult `cp3 help` for the accepted flags before using them in automation, and use `--dry-run` when the command supports it and you want to inspect the planned work first.

## Native and editor commands

`native-install` creates bindings from a native SDK using libclang support. It depends on the SDK headers and platform libraries; read [Native packages and ABI boundaries](./07-native-interop.md) before exposing foreign values to application code.

`lsp` starts the language server, usually over stdio for an editor integration. `dap` starts the debug adapter. `editor-plugin` manages the supported editor extension workflow. Their availability and options are versioned with the installed CLI.

## Documentation commands

`cp3 docs` serves the Markdown guide embedded in the compiler on loopback. `cp3 docs --port 0` asks the operating system for a free port and prints the selected address. The server runs until interrupted. See [Diagnostics and generated help](./06-diagnostics.md) for a per-error help file, which is separate from the full docs server.

## Argument forwarding and exit status

Commands may accept `--` to separate CP3 CLI options from arguments that should be passed to the program or an external tool. Consult `cp3 help` and the command's documented behavior before relying on argument forwarding. The CLI itself uses exit code `0` for success, `1` for compile errors, and `2` for usage errors; `run` can propagate the built program's exit code.

In shell scripts, preserve the command exit status and quote file paths and arguments. Avoid passing user data through a shell command string when a structured argument list is available.

## Project scripts

`cp3 init` writes `start`, `verify`, and `build` script entries into `package.json` (`cp3c run`, `cp3c check`, and `cp3c build` of the entry file). These are command strings provided as project metadata; CP3's own commands can be invoked directly and do not require an npm runtime. Read the generated script before connecting it to another task runner.

A script in `package.json` can be run directly by name, without an `npm run` wrapper:

```sh
cp3 start
```

The command word is looked up in the nearest `package.json`'s `scripts` object. Built-in command words are reserved: a script whose name matches a built-in command (other than the legacy `build`/`check`/`dev` names) or `help`/`version` is rejected with a diagnostic telling you to rename it. For `build`, `check`, and `dev`, the built-in command runs and a same-named script does not get invoked as the command. Extra command-line arguments are not accepted by a package script (usage error). The script text is authored for a shell and runs through the platform shell from the project root.

The built-in commands `run`, `build`, `check`, `dev`, `fmt`, `clean`, and `install` support npm-style lifecycle hooks from the same `scripts` object: `pre<command>` runs before the command (a failing `pre` hook aborts it), and `post<command>` runs after a successful command only (a failed command never triggers its `post` hook). For example, `prebuild` runs before `cp3 build` and `postbuild` runs after a successful `cp3 build`. Hooks are shell scripts, not CP3 programs. Hooks are not run by `lsp`, `dap`, `native-install`, or `docs`, and nested CP3 calls made from within a hook carry `CP3_SCRIPT_GUARD=1` and skip hooks themselves.

On a terminal, `cp3 init` asks five questions (type, name, description, author, and the type-system selection) with arrow-key menus for the two choice fields; `--yes` skips the questions entirely and the flags `--type`, `--type-system`, `--description`, and `--author` pre-select the answers for scripts. Existing scaffold files are left unchanged unless `--force` is passed.

## Related pages

- [Create and run your first project](../00-welcome/04-first-project.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Native toolchains](./05-native-toolchains.md)
- [Build profiles and PGO](./11-build-profiles-and-pgo.md)
- [Tests and compiler verification](./08-tests-and-verification.md)
