---
title: CLI commands
chapter: Chapter 4 · Compiler and tools
---
# CLI commands

The `cp3` command is the user-facing project tool. Run `cp3 help` for the exact installed command list and `cp3 <command> --help` for a command's options.

## Common project workflow

```sh
cp3 init hello-app
cd hello-app
cp3 check src/index.cp3
cp3 run src/index.cp3
```

`init` creates a starter project. `check` runs compile-time analysis without asking the user to execute the program. `run` builds and launches the selected source entry.

## Build and development

- `cp3 build` compiles a project into a native executable.
- `cp3 dev` runs the development workflow with the configured rebuild behavior.
- `cp3 fmt` formats CP3 source.
- `cp3 clean` removes generated build artifacts for the project.
- `cp3 exec` runs an external command through the CLI's process forwarding behavior.

Exact flags vary by command and version; use the command's `--help` output rather than copying flags from another tool.

## Toolchain and packages

`cp3 toolchain` inspects or configures the native compiler toolchain. Package commands include operations for packing, publishing, installing native dependencies, and recompiling packages. Package workflows may require network access and external development tools.

## Documentation and editor integration

`cp3 docs` serves the documentation embedded in the compiler on loopback. `cp3 docs --port 0` asks the operating system for an available port; the command prints the selected URL. `cp3 lsp` starts the language server and `cp3 dap` starts the debug adapter for supported editors.

The docs command serves a local static guide and does not require a remote docs service. See [Diagnostics and generated help](#04-compiler-cli/06-diagnostics) for the per-diagnostic help page.
