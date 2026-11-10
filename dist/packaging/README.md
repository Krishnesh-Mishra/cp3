# cp3 packaging

Build the compiler once, then stage a relocatable distribution for the host
OS/arch:

```text
build/out/
  windows-x64/
    cp3/                                  relocatable install tree
    cp3-<version>-windows-x64.msi         per-user MSI (no admin)
    SHA256SUMS.txt
  linux-x64/
    cp3/
    install.sh                            per-user installer (no root)
    cp3-<version>-linux-x64.tar.gz
    SHA256SUMS.txt
```

On Windows the MSI is the deliverable. `cp3/` is the staged tree used to build
it, not a separate installer. The old zip/`install.ps1`/Inno artifacts are no
longer produced.

`version.txt` at the repository root is the single version source (`0.2.1`).
`vendor/tools/update-version.ps1` / `vendor/tools/update-version.sh` sync the compiler's
`--version` string, CMake project version, runtime, VS Code extension metadata,
packaging defaults, and current docs from it. `vendor/tools/build.ps1` and
`vendor/tools/build.sh` run the sync automatically before configuring.

## One command per OS

### Windows

```powershell
.\tools\build.ps1
```

The script first syncs `version.txt`, then configures `build/` (D55), builds
`build\bin\cp3c.exe`, runs `packaging\windows\package.ps1` into
`build\out\windows-x64`, and builds
`build\out\windows-x64\cp3-<version>-windows-x64.msi` with WiX 5. The staged tree is
smoke-tested with a hello-world program before the MSI is written.

```powershell
# debug compiler, no packaging
.\tools\build.ps1 -Config Debug -NoPackage

# full clean (build\scratch\ is preserved)
.\tools\build.ps1 -Clean

# no installer wizard (silent/basic MSI only)
.\tools\build.ps1 -NoMsiUi
```

The MSI installs per-user under `%LOCALAPPDATA%\Programs\cp3` with no UAC
prompt, adds `<install>\bin` to the current user's PATH (so `cp3` and `cp3c`
work in new terminals), and removes that entry on uninstall. It also adds an
Apps & Features entry and a Start Menu `cp3 Command Prompt` shortcut.

```powershell
msiexec /i .\cp3-<version>-windows-x64.msi /qn
msiexec /x .\cp3-<version>-windows-x64.msi /qn
```

WiX 5.0.2 is pinned in `packaging\windows\.config\dotnet-tools.json` and
restored automatically by the build; no global WiX install is required. The
extension cache stays in `packaging\windows\.wix` (ignored by git), keeping the
repository root clean. WiX 7 is refused because it requires the OSMF EULA.
`-WixPath <wix.exe>` selects an explicit WiX 5/6 tool. The first MSI build
needs network access to restore the local tool and its UI extension; the
resulting MSI is self-contained.

### Linux

```sh
./vendor/tools/build.sh
```

The script first syncs `version.txt`, then configures `build/`, builds
`build/bin/cp3c`, and runs `dist/packaging/linux/package.sh`, producing
`build/out/linux-<arch>/` with the tree, `install.sh`, and
`cp3-<version>-linux-<arch>.tar.gz`.

```sh
# debug compiler, no packaging
./vendor/tools/build.sh --config Debug --no-package

# full clean (build/scratch/ is preserved)
./vendor/tools/build.sh --clean

# package an existing build only
dist/packaging/linux/package.sh --build-dir build --out-dir build/out/linux-x64
```

The tarball installs per-user:

```sh
tar -xzf cp3-<version>-linux-x64.tar.gz
./install.sh                  # $HOME/.local/lib/cp3 + $HOME/.local/bin/cp3
./install.sh --prefix /opt/cp3 --force
```

## Layout contract

Both packages stage the same relocatable tree:

| Path | Contents |
| --- | --- |
| `bin/cp3c` (`cp3c.exe`) | the compiler (also the LSP server via `cp3c lsp`) |
| `bin/cp3` (`cp3.cmd`) | the `cp3` command: `init`, `doctor`, and everything `cp3c` offers |
| `include/` | header trees handed to Clang: `runtime/`, `common/`, `std/<family>/` |
| `src/` | runtime translation units compiled into every cp3 program |
| `vendor/third_party/` | vendored static libraries (`libsodium`, `libuv`, `uWebSockets`, `oneTBB`, `pcre2`) |
| `share/vscode/` | bundled VS Code extension, when present |
| `manifest.json` | name, version, compiler version, `built_at`, per-file size + SHA-256 |
| `README.txt` | end-user quick start |

`cp3c` finds the trees relative to its own location, so the tree is fully
relocatable (`src/cli/src/toolchain/runtime/util.cpp`): `CP3_ROOT` first, then
the executable's ancestors, then the compile-time checkout path. The `cp3`
shims export `CP3_ROOT` explicitly.

## Requirements

Windows build machine:

- Windows x64, PowerShell 5.1+.
- Python 3 for `vendor/tools/update-version.ps1` (also used by the test tooling).
- CMake 3.20+, Ninja (or `mingw32-make`), MinGW-w64 GCC (`g++`).
- .NET SDK with `dotnet` on PATH. WiX 5.0.2 is pinned in
  `packaging\windows\.config\dotnet-tools.json`; the MSI build restores it
  automatically. The extension cache stays in `packaging\windows\.wix`
  (ignored by git). The first build needs network access for the local tool and
  its UI extension.
- Clang 16+ (`clang++`) for the staged smoke test. It may be on `PATH` or in a
  standard LLVM location.

Linux build machine:

- Python 3 for `vendor/tools/update-version.sh`.
- CMake 3.20+, Ninja (or Make), GCC/Clang with C++20.
- Clang 16+ (`clang++`) for the staged smoke test.
- `sha256sum`, `tar`, `file` (the last is only used to warn about vendor
  archive formats).

End-user machines need Clang 16+ to compile cp3 programs. The compiler,
headers, runtime sources and vendor libraries are shipped in the package.

## Linux third-party caveat

The checked-in `vendor/third_party/*/lib/*.a` archives are the Windows MinGW (COFF)
vendor builds. They cannot be linked by a Linux target. A Linux package built
from the default `vendor/third_party/` tree is complete for the compiler and the
stdlib families that do not need vendor archives, but `std/network` and
`std/crypto` need Linux-built archives.

Rebuild the vendor tree for Linux, then package with the default third-party
tree (vendor/third_party); the packager keeps only the matching architecture:

```sh
bash dist/packaging/linux/package.sh
```

The packager detects COFF archives and prints a warning instead of silently
shipping them.
