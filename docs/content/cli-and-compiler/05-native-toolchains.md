---
title: Native toolchains
---
# Native toolchains

CP3 lowers your program to C++ and needs a Clang compiler to finish the job. Picking the right one — and keeping it — is the subject of this page.

## What you install

- **Clang, version 16 or newer.** On Windows, CP3 targets **MinGW-w64 GNU** (never the MSVC triple); the Windows Clang you install is used with the MinGW sysroot CP3 locates automatically.
- The compiler carries and manages your choice: `cp3 toolchain` selects, inspects, and stores it. **CP3 itself is built with CMake; your programs are never built with CMake.**

Before any compiler is accepted for a build, CP3 verifies it: the candidate is asked to compile, link, and run a one-line C++20 program. A Clang that fails that smoke test is treated as unusable, even if it appears on `PATH`.

## Inspect what you have

```sh
cp3 toolchain list          # candidates + the effective choice
cp3 toolchain list --json   # machine-readable
cp3 toolchain detect        # what automatic discovery finds
cp3 toolchain current       # one line: the effective choice + its source
```

A candidate that is installed but cannot serve the active target is reported as such — "found" is not "ready". Record the exact version and path when comparing builds.

## Choose a toolchain

```sh
cp3 toolchain use clang            # family only
cp3 toolchain use clang@18         # a specific major version
cp3 toolchain use /opt/llvm/bin/clang++   # an explicit executable
```

Scope the saved choice:

```sh
cp3 toolchain use clang@18 --project   # remembered in this project's package.json
cp3 toolchain use clang --global      # remembered for all projects
cp3 toolchain clear --project          # forget the project choice
cp3 toolchain                       # interactive picker (on a TTY)
```

- **Project scope** is for shared, reproducible builds — contributors and CI agree on one compiler. It is stored as a `"toolchain"` object in `package.json`.
- **Global scope** is for your own machine preference. It lives outside the repository (`%APPDATA%\cp3\toolchain.conf` on Windows, `$XDG_CONFIG_HOME/cp3/toolchain.conf` — or `~/.config/cp3/toolchain.conf` — elsewhere; `CP3_CONFIG_DIR` overrides the directory).

## Precedence, and strict pins

When several settings disagree, the resolver applies them in this order:

1. `--toolchain <...>` on the command line;
2. the `CP3_TOOLCHAIN` environment variable;
3. the project's `package.json` `"toolchain"` object;
4. the global configuration file;
5. automatic detection — the newest *usable* installed Clang wins.

`cp3 toolchain list` shows the effective choice and where it came from, so you never have to guess.

A saved **exact** pin (`clang@18`) that no longer matches anything on the machine normally falls back with a warning. `--strict-toolchain` turns that into a hard failure instead — use it in CI when a changed compiler must be rejected, never silently swapped.

## If no compiler is found

Invoking `cp3 toolchain` on a terminal opens the interactive selector. When nothing usable is installed, it offers to install Clang through your platform's package manager (`winget install LLVM.LLVM` on Windows, `brew install llvm` / `xcode-select --install` on macOS, `apt-get|dnf|pacman` on Linux), and only proceeds after an interactive yes — CP3 never downloads a compiler on its own. A non-interactive machine prints the manual install instructions instead.

## Requirements and platform notes

- The release profile additionally selects the target's machine architecture flags; a binary built with `-march=native`/`-mcpu=native` may not run on other CPUs. Use `--no-native` (or ship a generic build) when the deployment target differs.
- CP3 targets the MinGW-w64 toolchain on Windows with the UCRT; MSVC-linked runtimes are not supported targets for generated programs.
- macOS uses the system libc++/STL; Linux uses the system STL.

## Troubleshooting

1. Identify the stage first: a `CP3xxxx` code is a front-end error; a `.cpp` file with a C++ diagnostic is the native compile step; an undefined symbol is a link failure; a stack trace after launch is the program.
2. For a native compile failure, preserve the first diagnostic, the full command (`--verbose` prints it), the generated source path, and the toolchain version.
3. For a link failure, keep the search paths (`-L` dirs), the link order, and the complete link command.
4. Do not hand-edit generated `.cpp` files; fix the CP3 source (or report the compiler bug with a reproducer).

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Build profiles and PGO](./11-build-profiles-and-pgo.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [How a build flows](./02-compilation-pipeline.md)
