# cp3 Windows packaging

Builds and ships the Windows installer for the cp3 toolchain:

```
build\scratch\packaging\dist\cp3-setup-0.1.0-x64.exe
```

Everything here is per-user (no admin) and reproducible from a built tree.

## Pipeline

```
cmake --build <build>            build\bin\cp3c.exe (any Release/Debug tree)
  |
  v
package.ps1                      stages the distribution under <dist>\cp3
  |                              and writes manifest.json (SHA-256 per file)
  v
build-installer.ps1              runs package.ps1, then ISCC (Inno Setup 6)
  |
  v
cp3-setup-<version>-x64.exe      one self-contained, per-user installer
```

From the repository root:

```powershell
# default tree is .\build; the packaging wave uses its own tree:
powershell -ExecutionPolicy Bypass -File packaging\windows\build-installer.ps1 `
    -BuildDir build\scratch\packaging\build

# stage only (no installer), for inspection:
powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1 `
    -BuildDir build\scratch\packaging\build

# while the VS Code extension is not built yet:
powershell ... build-installer.ps1 -BuildDir <dir> -NoVsix
```

Requirements on the build machine:

- Windows x64, PowerShell 5.1+.
- A MinGW-w64 toolchain whose `g++` is on PATH: `package.ps1` copies the
  matching `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll`
  next to `bin\cp3c.exe` so the installed compiler starts without a toolchain
  on PATH (use `-NoRuntimeDlls` to skip).
- Inno Setup 6 for the installer step. Install it with:

```powershell
winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements
```

`build-installer.ps1` finds `ISCC.exe` on PATH or under
`%LOCALAPPDATA%\Programs\Inno Setup 6\`; `-IsccPath <path>` overrides.

## Stage layout (`package.ps1` output)

| Path | Contents |
| --- | --- |
| `bin\cp3c.exe` | the compiler (also the LSP server through `cp3c lsp`) |
| `bin\cp3.cmd` | the `cp3` command: `init`, `doctor`, plus everything `cp3c` offers (including `editor-plugin`) |
| `bin\*.dll` | MinGW runtime DLLs the compiler links against |
| `include\` | header trees handed to the C++ toolchain: `runtime\`, `common\`, `std\<family>\` |
| `src\` | runtime translation units compiled into every cp3 program (`src\runtime\src\`, `src\std\<family>\`) |
| `third_party\` | vendored static libraries (`libsodium`, `libuv`, `uWebSockets`) |
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

`cp3c.exe` locates the trees it hands to g++/clang++ in this order
(`src/cli/src/toolchain/util.cpp`):

1. `CP3_ROOT` (the shim exports it as `<install>`);
2. the running executable's ancestors (an installed `bin\cp3c.exe` resolves to
   `<install>` one level up);
3. its own compile-time path (an in-checkout compiler, unchanged behavior).

Header roots are mapped from the checkout form `src\<x>\include` to the
installed form `include\<x>` when the checkout directory does not exist, so
one `include\` tree serves every case while runtime sources stay under
`src\`.

## Installer behaviour (`cp3.iss`)

- Per-user install, default `%LOCALAPPDATA%\Programs\cp3`, `PrivilegesRequired=lowest`
  (no UAC prompt). `/DIR=` overrides the target.
- Copies the stage verbatim; writes an uninstaller (`unins000.exe`) and the
  usual Apps & Features entry (HKCU).
- Appends `<target>\bin` to the **user** PATH (`HKCU\Environment`). The value
  is written back as `REG_EXPAND_SZ` when it contains `%variables%` and as
  `REG_SZ` otherwise, so the registry type never changes behind the user's
  back; the system PATH is never touched. The append is idempotent (a second
  install detects the existing entry) and uninstall removes exactly that
  entry, leaving every other entry (empty ones included) byte-identical.
- When the `code` CLI exists (PATH first, then the standard VS Code install
  locations) and a `.vsix` was staged, runs
  `code --install-extension <vsix> --force`. Without VS Code it leaves a log
  line and points at `cp3 editor-plugin`.
- Installs silently with Inno's `/SILENT` or `/VERYSILENT`
  (`/SUPPRESSMSGBOXES` recommended); `/LOG=<file>` writes the full log.

### Installer command-line flags

| Flag | Effect |
| --- | --- |
| `/SILENT`, `/VERYSILENT` | Inno quiet modes (progress window / nothing) |
| `/SUPPRESSMSGBOXES` | never show message boxes (combine with the above) |
| `/DIR=<path>` | install target (default `%LOCALAPPDATA%\Programs\cp3`) |
| `/LOG=<file>` | write the installer log |
| `/NORESTART` | never reboot (harmless here; nothing needs a reboot) |
| `/TESTMODE=1` | no side effects: no PATH edit, no VS Code action (files only) |
| `/NOPATH=1` | skip only the PATH append |
| `/NOVSCODE=1` | skip only the VS Code extension step |

`/TESTMODE=1` is the flag the packaging wave uses for verification installs;
it is equivalent to `/NOPATH=1 /NOVSCODE=1`.

## End-user story

1. Run `cp3-setup-0.1.0-x64.exe` (optionally silent: `/VERYSILENT /SUPPRESSMSGBOXES`).
2. Open a new terminal (the PATH entry is picked up by new shells).
3. `cp3 doctor` - reports the compiler, the C++ toolchain it will use, ccache,
   VS Code and the shipped trees.
4. `cp3 init my-app` then `cd my-app` and `cp3 run src/main.cp3`.
5. `cp3 editor-plugin` installs the bundled extension if the installer did
   not already do it (the wizard also reports status and exports the VSIX).

Prerequisites on the user machine: a C++ toolchain on PATH (MinGW-w64 `g++` is
the tested one; MSVC `cl` and `clang++` are detected too) and Windows 10+ with
the UCRT. VS Code is optional: cp3 libraries and CLI work without it. LLVM is
not bundled in this round.

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

After an install, `manifest.json` lists every component with its SHA-256; the
report in `build\scratch\packaging\INSTALLER-REPORT.md` records the exact
acceptance commands used by the packaging wave.
