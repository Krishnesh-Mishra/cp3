# Building CP3 from source

This page is for working on the compiler and the standard library. Using CP3
does not require building it — installers and release packages are described
at the end. Repository-relative paths below are from the repo root.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Compiler and runtime C++ sources (`lexer`, `parser`, `sema`, `codegen`, `runtime`, `cli`, `std/…`) |
| `tests/cases/` | Conformance corpus (`*.cp3` + `expected` output) |
| `tests/runner/run_tests.py` | The conformance/benchmark harness |
| `benchmarks/` | Benchmark cases and runners |
| `docs/` | This documentation; `docs/content/` is the site source |
| `vendor/` | Third-party sources and build scripts (`vendor/tools/`) |
| `dist/packaging/` | MSI/tarball packaging scripts |
| `build/` | Build trees (gitignored): presets use `build/<preset>/`, the packaging scripts use `build/` (D55) |

## Prerequisites

The host-checker reports what is installed and can install the rest on a
consenting host (`winget` on Windows, `apt-get` on Linux):

```sh
python vendor/tools/check-prerequisites.py --report   # findings only
```

### Windows (canonical)

- Windows x64, PowerShell 5.1+
- Python 3
- CMake 3.20+ and Ninja
- MinGW-w64 `g++` with C++20 (the WinLibs/UCRT g++ environment is what the
  canonical `windows-release` preset auto-detects)
- Clang 16+ on `PATH` (or in a standard LLVM location) — cp3 programs are
  compiled by Clang, independently of which compiler built `cp3c` itself
- For the MSI: .NET SDK with `dotnet` on `PATH` (WiX 5.0.2 is pinned in
  `dist/packaging/windows/.config/dotnet-tools.json` and restored
  automatically; the first MSI build needs network access)

### Linux (WSL)

- A WSL distro with a normal user toolchain (the release Linux build ran
  under WSL; paths such as `/mnt/c/Users/…/build/linux-release` are normal)
- Python 3, CMake 3.20+, Ninja, `g++` or `clang++` with C++20
- Clang 16+ for cp3 programs and the staged smoke test
- `sha256sum`, `tar`, `file` (only `file` is used just to warn about vendor
  archive formats)

**Vendor archives**: the checked-in `vendor/third_party/*/lib/*.a` files are
Windows MinGW (COFF) archives. They cannot be linked by a Linux target.
`std/network` and `std/crypto` need Linux-built archives; x64 Linux archives
for xxhash/simdutf/pcre2 are committed and staged. The arm64 outputs are
marked `-untested` until they run on hardware — see
[`ROADMAP.md`](../ROADMAP.md) and the release notes.

## Developer build trees (cmake presets)

The presets in `CMakePresets.json` configure the canonical developer trees.
They are Ninja + Release by default; the runner expects these locations.

### Windows

```powershell
cmake --preset windows-release                 # -> build/windows-release
cmake --build build/windows-release --parallel 20
```

`build/windows-release/bin/cp3c.exe` is the binary
`tests/runner/run_tests.py` auto-detects. `cmake --preset windows-debug`
gives the Debug variant (`build/windows-debug`). CMake auto-detects the
WinLibs UCRT g++ on the current `PATH`; pass
`-DCMAKE_CXX_COMPILER=<path>` to force a different compiler (for example a
Clang) when configuring the preset. Note the clang that cp3 invokes for the
programs it compiles is detected by cp3c itself and is independent of the
compiler that built cp3c.

### Linux (WSL)

```sh
cmake --preset linux-release                   # -> build/linux-release
cmake --build build/linux-release --parallel 20
```

`build/linux-release/bin/cp3c` is the binary the runner auto-detects on
Linux (plus the legacy `build/bin` layout as a fallback).

## Building a release package

Release packaging compiles the compiler, stages a relocatable tree and
produces the installer/archive plus checksums. `version.txt` is the single
version source; the scripts sync it before configuring. The packaging
contract and layout are documented in `dist/packaging/README.md`.

Note: the packaging scripts use `build/` (the canonical D55 tree), which is
separate from the preset `build/<preset>/` trees used by the test runner.
Don't mix the two when a stale `build/CMakeCache.txt` complains about a
generator mismatch — use `--clean` / `-Clean` or a fresh checkout.

### Windows: `vendor/tools/build.ps1` (PowerShell)

```powershell
.\vendor\tools\build.ps1                      # sync version, build, stage, MSI, SHA256SUMS
.\vendor\tools\build.ps1 -Config Debug -NoPackage    # compiler only
.\vendor\tools\build.ps1 -Clean               # wipe build\ (build\scratch\ kept)
.\vendor\tools\build.ps1 -NoMsiUi             # MSI without the wizard UI
.\vendor\tools\build.ps1 -SkipSmoke           # skip the staged hello-world smoke test
```

Output: `build\out\windows-x64\cp3\` (staged tree),
`build\out\windows-x64\msi\cp3-<version>-windows-x64.msi`, and
`SHA256SUMS.txt`. The MSI installs per-user under
`%LOCALAPPDATA%\Programs\cp3` with no UAC prompt and adds
`<install>\bin` to the user `PATH`; uninstall removes the entry. WiX 5.0.2
is pinned in `dist/packaging/windows/.config/dotnet-tools.json`; the first
build restores it from the network.

### Linux: `vendor/tools/build.sh` (bash, under WSL)

```sh
./vendor/tools/build.sh                        # sync version, build, stage, tarball
./vendor/tools/build.sh --config Debug --no-package   # compiler only
./vendor/tools/build.sh --clean
./vendor/tools/build.sh --out-dir build/out/linux-x64
dist/packaging/linux/package.sh --build-dir build --out-dir build/out/linux-x64
```

Output: `build/out/linux-<arch>/cp3/` (staged tree), `install.sh`,
`build/out/linux-<arch>/tarball/cp3-<version>-linux-<arch>.tar.gz` and
`SHA256SUMS.txt`. The tarball installs per-user:

```sh
tar -xzf cp3-<version>-linux-x64.tar.gz
./install.sh                  # $HOME/.local/lib/cp3 + $HOME/.local/bin/cp3
./install.sh --prefix /opt/cp3 --force
```

Because the default `vendor/third_party/` tree holds Windows COFF archives,
the Linux packager warns (instead of shipping them silently) when a required
Linux-built archive is missing; rebuild those archives for Linux before
packaging `std/network` and `std/crypto`. See `dist/packaging/README.md`.

## Test entry points

Run everything from the repository root.

```sh
python tests/runner/run_tests.py --jobs N        # corpus (N parallel cp3c subprocesses)
python tests/runner/run_tests.py --bench --jobs N # corpus, then the benchmark pass (default)
python tests/runner/run_tests.py --no-bench      # correctness only
python tests/runner/run_tests.py --filter json --no-bench
python tests/runner/run_tests.py --jobs 1        # strictly sequential
```

- `--jobs N` sets the corpus concurrency; the default is one per logical
  CPU. `--bench` is now the default after the corpus pass and is the
  explicit way to request it; the benchmark pass itself always runs
  sequentially. `--bench-args` forwards arguments to the benchmark runner.
- The runner auto-detects `build/windows-release/bin/cp3c.exe` on Windows
  and `build/linux-release/bin/cp3c` elsewhere, plus the legacy
  `build/bin` layout; if nothing usable is found it configures and builds
  one into that tree first (Ninja when available). `--cp3c PATH` uses an
  explicit binary and is never silently rebuilt.
- Before any case runs, a `console.log("Hello World")` probe must compile
  and run; a failing probe aborts with exit code 2.
- Per-case timeout defaults to 60 s (`--timeout`). Transient
  compile/timeout failures are retried serially. Cases under `_ambiguous/`
  are open-question markers and never count as pass/fail
  (`--strict-ambiguous` opts in to running them as ordinary cases).
- JSON output for scripts: `--json`.

## Benchmark entry points

```sh
python tests/runner/run_tests.py --bench --jobs N           # bench pass after the corpus
python benchmarks/runner/run_suite.py --only health-server --reps N
python benchmarks/runner/run_suite.py --only hello-world,string-churn --reps 7
```

`--only` takes a comma-separated list of task names (run with no arguments
or `--help` to print the full list). `--reps N` sets the measured repetitions
per language (default 3). Other useful flags: `--except`, `--seconds`,
`--concurrency`, `--clients`/`--single-clients`, `--warmup`, `--strict`
(exit 1 when the gate prints any WARN), `--results <dir>` (tee raw output).
`benchmarks/runner/run_benchmark.py` is a thin forwarding wrapper around
`run_suite.py` with the same flags. The runner auto-builds cp3c into the
canonical preset tree when missing, or pass `--cp3c PATH`.

Methodology, per-task outputs and the recorded results snapshot live in
`benchmarks/README.md` and `benchmarks/results.md`; do not compare numbers
across machines or across compiler rebuilds without the same build.

## Release checklist (packaging owners)

1. `version.txt` is bumped; `vendor/tools/update-version.{ps1,sh}` sync the
   compiler `--version`, CMake project version, runtime, VS Code extension
   metadata and docs (the build scripts run it automatically).
2. The full corpus passes on the reference platform (Windows) and the Linux
   corpus rerun is clean — see `docs/release/v0.2.md` for the v0.2 state.
3. Windows: `.\vendor\tools\build.ps1`; Linux: `./vendor/tools/build.sh`.
4. The MSI/tarball and `SHA256SUMS.txt` are attached/posted with the tag.
5. arm64 packages carry `-untested` until they are verified on hardware.
