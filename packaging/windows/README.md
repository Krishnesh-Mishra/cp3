# cp3 Windows packaging

Builds a single per-user MSI installer for the cp3 toolchain:

```text
out\windows-x64\cp3-0.1.0-windows-x64.msi
```

Double-click it, or install silently with `msiexec`. No administrator rights
are required, and no zip or install script is involved.

## One-command build

From the repository root:

```powershell
.\tools\build.ps1
```

The script syncs `version.txt` into the compiler, packaging and extension
metadata, builds `build\bin\cp3c.exe`, stages the relocatable tree under
`out\windows-x64\cp3`, then builds the MSI and `SHA256SUMS.txt` with
`packaging\windows\build-msi.ps1`. The old zip/Inno artifacts are removed from
the output directory.

Useful variants:

```powershell
# debug compiler, no packaging
.\tools\build.ps1 -Config Debug -NoPackage

# full clean (build\scratch\ is preserved)
.\tools\build.ps1 -Clean

# no wizard (silent/basic MSI only)
.\tools\build.ps1 -NoMsiUi

# package an existing stage directly
packaging\windows\build-msi.ps1 -StageDir out\windows-x64\cp3
```

## Pipeline

```text
cmake --build <build>            build\bin\cp3c.exe
  |
  v
package.ps1                      stages <dist>\cp3 and writes manifest.json
  |
  v
build-msi.ps1                    generates WiX source + runs WiX 5
  |
  v
cp3-<version>-windows-x64.msi    one self-contained, per-user installer
```

`tools\build.ps1` runs `package.ps1` and `build-msi.ps1` in that order.
`package.ps1` can still be run alone for inspection.

## Requirements on the build machine

- Windows x64, PowerShell 5.1+.
- Python 3 for `tools/update-version.ps1`.
- CMake 3.20+, Ninja (or `mingw32-make`), MinGW-w64 GCC (`g++`).
- .NET SDK with `dotnet` on PATH. The repository pins WiX **5.0.2** in
  `packaging\windows\.config\dotnet-tools.json`; `build-msi.ps1` runs
  `dotnet tool restore` automatically. No global WiX install is required. The
  WiX extension cache lives in `packaging\windows\.wix` (ignored by git), so
  the repository root stays clean.
- A MinGW-w64 runtime directory discoverable through `CP3_MINGW_ROOT`, PATH, or
  a standard WinLibs/MSYS2 location: `package.ps1` copies the matching
  `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll` next to
  `bin\cp3c.exe` (use `-NoRuntimeDlls` to skip).
- Clang 16+ (`clang++`) for the staged hello-world smoke test. It is not
  bundled in the package.

WiX 7 is deliberately not used: it requires accepting the Open Source
Maintenance Fee (OSMF) EULA. To use an explicit WiX 5/6 executable:

```powershell
packaging\windows\build-msi.ps1 -WixPath C:\tools\wix5\wix.exe
```

The first build needs network access to restore the local WiX tool and its UI
extension. The resulting MSI is self-contained.

## Installer behaviour

- **Per-user, no UAC.** Default install root is
  `%LOCALAPPDATA%\Programs\cp3`; `Scope="perUser"` and limited privileges mean
  no administrator prompt. Override the directory with the public `INSTALLDIR`
  property if needed.
- **`cp3` works immediately.** The MSI appends `<install>\bin` to the **current
  user's** PATH (`HKCU\Environment`) and never touches the system PATH. Open a
  new terminal and run `cp3` or `cp3c`; no manual PATH edit is needed.
- **Clean uninstall.** Uninstall removes the files, the Start Menu folder, the
  `HKCU\Software\cp3` key, and exactly the PATH entry the installer added.
- **Apps & Features entry.** Settings > Apps > Installed apps shows `cp3` with
  its version and uninstall action.
- **Start Menu shortcut.** `cp3 Command Prompt` opens a terminal in
  `<install>\bin`, where `cp3.cmd` is available even before Explorer refreshes
  its environment.
- **Updates and same-version reinstall.** A fixed `UpgradeCode` and
  deterministic component GUIDs let a newer version replace an older one. A
  rebuilt installer with the same `major.minor.patch` version also replaces
  the installed build, rather than failing as a separate product. Downgrades
  to a lower version remain blocked. `ProductVersion` is the numeric
  `major.minor.patch` prefix of `version.txt`; prerelease suffixes are not valid
  MSI versions.

`CP3_ROOT` is not written to the user environment. The `cp3.cmd` shim exports
it for each child process, and `cp3c.exe` also resolves its install root from
its own location, so a global variable would only risk pointing at the wrong
tree.

## Silent install and uninstall

```powershell
# install for the current user, no UI
msiexec /i .\cp3-0.1.0-windows-x64.msi /qn /norestart

# install into a custom directory
msiexec /i .\cp3-0.1.0-windows-x64.msi /qn INSTALLDIR="D:\tools\cp3"

# write a verbose log
msiexec /i .\cp3-0.1.0-windows-x64.msi /qn /l*v install.log

# uninstall (the MSI path identifies the product)
msiexec /x .\cp3-0.1.0-windows-x64.msi /qn /norestart
```

`/qb` shows a basic progress UI; double-clicking the MSI uses the
`WixUI_InstallDir` wizard, with install-location, Install, and Cancel steps.
When the MSI detects an older or same-version CP3 install, it replaces that
install as part of the chosen install action. `/qn` is fully silent.

## Stage layout (`package.ps1` output)

| Path | Contents |
| --- | --- |
| `bin\cp3c.exe` | the compiler (also the LSP server through `cp3c lsp`) |
| `bin\cp3.cmd` | the `cp3` command: `init`, `doctor`, plus everything `cp3c` offers (including `editor-plugin`) |
| `bin\*.dll` | MinGW runtime DLLs the compiler links against |
| `include\` | header trees handed to Clang: `runtime\`, `common\`, `std\<family>\` |
| `src\` | runtime translation units compiled into every cp3 program (`src\runtime\src\`, `src\std\<family>\`) |
| `third_party\` | vendored static libraries (`libsodium`, `libuv`, `uWebSockets`, `oneTBB`, `pcre2`) |
| `share\vscode\` | the bundled extension, when built (`-NoVsix` stages without it) |
| `manifest.json` | name, version, compiler version, `built_at`, `components[]` with size + SHA-256 |
| `README.txt` | end-user quick start, written into the stage |

The header trees and runtime units come from the native-backend rows
(`src\std\<family>\native.hpp`), which are the build driver's own source of
truth; adding a runtime unit to a row is picked up automatically.

Two deltas against the frozen layout in `PKG-CONTRACTS.md` section 4 are
deliberate and reported in `build\scratch\packaging\INSTALLER-REPORT.md`:
`bin\` also carries the MinGW runtime DLLs (the compiler does not start
without them), and `<dist>\src\` plus `<dist>\third_party\` carry the runtime
sources and the vendored libraries the compiler compiles and links per
program.

### Why the installed compiler is relocatable

`cp3c.exe` locates the trees it hands to Clang in this order
(`src/cli/src/toolchain/runtime/util.cpp`):

1. `CP3_ROOT` (the shim exports it as `<install>`);
2. the running executable's ancestors (an installed `bin\cp3c.exe` resolves to
   `<install>` one level up);
3. its own compile-time path (an in-checkout compiler, unchanged behavior).

Header roots are mapped from the checkout form `src\<x>\include` to the
installed form `include\<x>` when the checkout directory does not exist, so
one `include\` tree serves every case while runtime sources stay under
`src\`.

## End-user story

1. Run `cp3-0.1.0-windows-x64.msi` (or `msiexec /i ... /qn`).
2. Open a new terminal. `cp3` and `cp3c` are already on the user PATH.
3. `cp3 doctor` reports the compiler, the Clang toolchain it will use,
   ccache, VS Code and the shipped trees.
4. `cp3 init my-app`, then `cd my-app` and `cp3 run src/main.cp3`.
5. `cp3 editor-plugin` installs the bundled extension if it was not already
   installed.

Prerequisites on the user machine: Clang 16+ (`clang++`) on PATH or in a
standard LLVM location, plus a MinGW-w64 sysroot for the Windows GNU target,
and Windows 10+ with the UCRT. GCC and MSVC are not target toolchains. VS Code
is optional: cp3 libraries and CLI work without it. LLVM is not bundled;
`cp3 build` offers to install it when missing.

## cp3.cmd commands

| Command | Behaviour |
| --- | --- |
| `cp3 <anything>` | forwarded verbatim to `bin\cp3c.exe`, exit code passed through (`cp3 lsp`, `cp3 build`, `cp3 check`, ... all land here) |
| `cp3 init [target] [--force]` | scaffolds `package.json` (`"main": "src/main.cp3"`), `src/main.cp3`, `README.md`; refuses a non-empty target unless `--force`; exit 2 on refusal |
| `cp3 doctor` | status list; exit 1 when `cp3c.exe` is missing |
| `cp3 editor-plugin` | interactive extension wizard; non-TTY runs `status`. Actions: `status`, `install`, `uninstall`, `export`, `path` (`--editor`, `--vsix`, `--extensions-dir`, `--out`, `--force`, `--yes`, `--dry-run`, `--json`, `--interactive`) |

## Verifying a build

`package.ps1` runs a smoke test by default: it compiles and runs a hello-world
program with the staged compiler and `CP3_ROOT` pointed at the stage, and fails
loudly if the staged tree is incomplete. Skip it with `-SkipSmoke`.

`build-msi.ps1` checks that the stage contains `bin\cp3c.exe` and builds the MSI
with WiX 5. `SHA256SUMS.txt` contains the MSI hash. The generated WiX source is
kept at `build\scratch\packaging\msi\cp3-x64.wxs` for inspection.

A full install/uninstall verification used by the packaging workstream:

```powershell
$msi = "out\windows-x64\cp3-0.1.0-windows-x64.msi"
msiexec /i $msi /qn /norestart /l*v install.log
cp3 --version
cp3 doctor
msiexec /x $msi /qn /norestart /l*v uninstall.log
```

The install log should end with `Installation success or error status: 0`, and
the uninstall should remove the install directory, the Start Menu folder and
the user PATH entry.

## Legacy Inno/zip path

`build-installer.ps1` and `cp3.iss` are kept only as an optional legacy
fallback for machines without WiX. They are not invoked by `tools\build.ps1`
and are not part of the default deliverable. The MSI is the supported Windows
installer.
