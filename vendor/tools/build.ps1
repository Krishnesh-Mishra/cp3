# =============================================================================
# vendor/tools/build.ps1 - build cp3c and package a per-user Windows MSI.
#
# One command:
#   0. syncs version-bearing files from version.txt via update-version.ps1;
#   1. configures the canonical build tree (build/ by default, DECISIONS D55);
#   2. builds bin\cp3c.exe with Ninja (MinGW Makefiles fallback);
#   3. stages the distribution under out\windows-<arch>\cp3 via
#      dist\packaging\windows\package.ps1 (headers, runtime units, vendored libs,
#      manifest, smoke test);
#   4. builds out\windows-<arch>\cp3-<version>-windows-<arch>.msi via
#      dist\packaging\windows\build-msi.ps1 (WiX 5, per-user, no admin);
#   5. writes SHA256SUMS.txt for the MSI.
#
# The MSI adds <install>\bin to the current user's PATH, so `cp3` and `cp3c`
# work in new terminals without manual setup. Uninstall removes that entry.
# No administrator rights are required. The old zip/Inno pipeline is no longer
# produced; legacy artifacts are removed from the output directory.
#
# WiX 5.0.2 is pinned in dist\packaging\windows\.config\dotnet-tools.json and
# restored with `dotnet tool restore`. The WiX extension cache lives in
# dist\packaging\windows\.wix (ignored by git), so the repo root stays clean. WiX 7
# is deliberately not used because it requires accepting the OSMF EULA.
#
# Usage (from the repository root):
#   .\tools\build.ps1
#   .\tools\build.ps1 -Config Debug -NoPackage
#   .\tools\build.ps1 -Clean
#   .\tools\build.ps1 -NoMsiUi
#   .\tools\build.ps1 -WixPath C:\tools\wix5\wix.exe
#
# Linux/macOS: use vendor/tools/build.sh on the target machine. Cross-compiling the
# compiler from Windows needs a CMake toolchain file under cmake/toolchains/;
# none is shipped, so this script refuses cross targets instead of silently
# producing a host binary in a cross-named directory.
# =============================================================================
#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet('windows', 'linux', 'macos')]
    [string]$TargetOS = 'windows',

    [Parameter(Mandatory = $false)]
    [ValidateSet('x64', 'arm64', 'x86')]
    [string]$Arch = 'x64',

    [Parameter(Mandatory = $false)]
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',

    [Parameter(Mandatory = $false)]
    [ValidateSet('Ninja', 'MinGW Makefiles', 'Visual Studio 17 2022')]
    [string]$Generator = 'Ninja',

    # Remove the compiler build tree before configuring. build\scratch\ is
    # preserved for the canonical build/ tree (parallel build trees and the
    # vendored third_party sources live there).
    [switch]$Clean,

    [switch]$ShowVerbose,

    # Build only; skip staging and the MSI.
    [switch]$NoPackage,

    # Build the MSI without the WixUI extension (no wizard; silent/basic MSI).
    [switch]$NoMsiUi,

    # Explicit wix.exe; default restores the repo-local WiX 5 tool.
    [string]$WixPath,

    # Do not stage the VS Code extension even when one is present.
    [switch]$NoVsix,

    # Do not run the staged hello-world smoke test (useful when no Clang
    # target toolchain is installed on the build machine).
    [switch]$SkipSmoke,

    # Override the build tree. Relative paths resolve against the repo root.
    [string]$BuildDir,

    # Override the distribution directory. Default: out\windows-<arch>.
    [string]$OutDir,

    # Parallel build jobs. 0 uses the processor count.
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Fail([string]$Message) { throw ("build.ps1: " + $Message) }
function Info([string]$Message) { Write-Host ("build: " + $Message) }
function Warn([string]$Message) { Write-Warning ("build: " + $Message) }

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $RepoRoot

# --- version -----------------------------------------------------------------
$versionUpdater = Join-Path $PSScriptRoot 'update-version.ps1'
if (-not (Test-Path -LiteralPath $versionUpdater)) {
    Fail "missing '$versionUpdater'."
}
Info "syncing version from version.txt"
& $versionUpdater

# --- tools -------------------------------------------------------------------
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakeCommand) {
    Fail "cmake was not found on PATH. Install CMake 3.20+ and reopen the terminal."
}
$cmake = $cmakeCommand.Source

if ($Generator -eq 'Ninja' -and -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Warn "Ninja was not found on PATH; falling back to 'MinGW Makefiles'."
    $Generator = 'MinGW Makefiles'
}
if ($Generator -eq 'MinGW Makefiles' -and -not (Get-Command mingw32-make -ErrorAction SilentlyContinue)) {
    Fail "the 'MinGW Makefiles' generator needs mingw32-make on PATH."
}

$gxx = $null
if ($Generator -ne 'Visual Studio 17 2022') {
    $gxx = Get-Command g++ -ErrorAction SilentlyContinue
    if (-not $gxx) {
        Fail "g++ was not found on PATH. Install MinGW-w64 (WinLibs/MSYS2) or use -Generator 'Visual Studio 17 2022'."
    }
    # package.ps1 copies libstdc++-6.dll, libgcc_s_seh-1.dll and
    # libwinpthread-1.dll from this root; point it at the compiler we build with.
    if (-not $env:CP3_MINGW_ROOT) {
        $env:CP3_MINGW_ROOT = Split-Path -Parent (Split-Path -Parent $gxx.Source)
    }
}

# --- build tree --------------------------------------------------------------
$isNative = ($TargetOS -eq 'windows' -and $Arch -eq 'x64')
if (-not $BuildDir) {
    if ($isNative) {
        $BuildDir = 'build'
    } else {
        $BuildDir = "build/scratch/cross-$TargetOS-$Arch"
    }
}
$BuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDir))
}

$toolchain = $null
if (-not $isNative) {
    $toolchain = Join-Path $RepoRoot "cmake/toolchains/$TargetOS-$Arch.cmake"
    if (-not (Test-Path -LiteralPath $toolchain)) {
        Fail ("cross-compiling to $TargetOS-$Arch needs '$toolchain'. " +
              "Build natively on the target OS with vendor/tools/build.sh instead.")
    }
}

# --- clean -------------------------------------------------------------------
function Remove-Tree([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer) {
        try { $item.Attributes = [System.IO.FileAttributes]::Normal } catch { }
        Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
        return
    }
    # Clear read-only attributes first; cmd's rd handles long paths and most
    # odd leaf names better than Remove-Item on Windows PowerShell 5.1.
    Get-ChildItem -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Attributes = [System.IO.FileAttributes]::Normal } catch { }
    }
    cmd.exe /c "rd /s /q `"$Path`"" 2>$null | Out-Null
    if (Test-Path -LiteralPath $Path) {
        try {
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
        } catch {
            Fail "could not remove '$Path'; close any program using it and retry."
        }
    }
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    $canonical = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'build'))
    if ($BuildDir -eq $canonical) {
        Info "cleaning compiler artifacts in $BuildDir (preserving build\scratch)"
        Get-ChildItem -LiteralPath $BuildDir -Force |
            Where-Object { $_.Name -ne 'scratch' } |
            ForEach-Object { Remove-Tree $_.FullName }
    } else {
        Info "cleaning build tree $BuildDir"
        Remove-Tree $BuildDir
    }
}

# --- generator cache ---------------------------------------------------------
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $line = Get-Content -LiteralPath $cache |
        Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' } |
        Select-Object -First 1
    if ($line) {
        $cached = $line.Substring('CMAKE_GENERATOR:INTERNAL='.Length).Trim()
        if ($cached -and $cached -ne $Generator) {
            if ($PSBoundParameters.ContainsKey('Generator')) {
                Fail ("build tree '$BuildDir' was configured with '$cached', not '$Generator'. " +
                      "Run with -Clean or pass -Generator '$cached'.")
            }
            Warn "reusing cached generator '$cached' (requested '$Generator')."
            $Generator = $cached
        }
    }
}

# --- configure ---------------------------------------------------------------
$cmakeArgs = @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    '-G', $Generator,
    "-DCMAKE_BUILD_TYPE=$Config"
)
if ($Generator -ne 'Visual Studio 17 2022') {
    $cmakeArgs += '-DCMAKE_CXX_COMPILER=g++'
}
if ($toolchain) {
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}

Info "configuring $Generator / $Config -> $BuildDir"
& $cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Fail "CMake configuration failed with exit code $LASTEXITCODE." }

# --- build -------------------------------------------------------------------
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
if ($Jobs -le 0) { $Jobs = 4 }

$buildArgs = @('--build', $BuildDir, '--parallel', "$Jobs")
if ($Generator -eq 'Visual Studio 17 2022') {
    $buildArgs += @('--config', $Config)
}
if ($ShowVerbose) { $buildArgs += '--verbose' }

Info "building with $Jobs parallel jobs"
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { Fail "build failed with exit code $LASTEXITCODE." }

# --- locate the compiler -----------------------------------------------------
$binDir = Join-Path $BuildDir 'bin'
$exe = Get-ChildItem -LiteralPath $binDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'cp3c.exe' -or $_.Name -eq 'cp3c' } |
    Select-Object -First 1
if (-not $exe) {
    Fail "build finished but no cp3c executable was found under '$binDir'."
}
# package.ps1 expects <BuildDir>\bin\cp3c.exe; multi-config generators put it
# under bin\<Config>. Normalize without touching the build output otherwise.
$canonicalExe = Join-Path $binDir 'cp3c.exe'
if ($exe.FullName -ne $canonicalExe) {
    Copy-Item -LiteralPath $exe.FullName -Destination $canonicalExe -Force
    $exe = Get-Item -LiteralPath $canonicalExe
}
Info "compiler: $($exe.FullName)"

# --- package -----------------------------------------------------------------
if ($NoPackage) {
    Info "packaging skipped (-NoPackage)"
    return
}
if ($TargetOS -ne 'windows') {
    Fail "Windows packaging can only package a Windows compiler; run vendor/tools/build.sh on $TargetOS."
}

$versionFile = Join-Path $RepoRoot 'version.txt'
if (-not (Test-Path -LiteralPath $versionFile)) {
    Fail "version.txt is missing at the repository root."
}
$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()

if (-not $OutDir) { $OutDir = "build/out/windows-$Arch" }
$OutDir = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutDir))
}

$packageScript = Join-Path $RepoRoot 'dist/packaging/windows/package.ps1'
if (-not (Test-Path -LiteralPath $packageScript)) {
    Fail "missing '$packageScript'."
}
$packageArgs = @{
    BuildDir = $BuildDir
    OutDir   = $OutDir
    Version  = $version
}
if ($NoVsix) { $packageArgs.NoVsix = $true }
if ($SkipSmoke) { $packageArgs.SkipSmoke = $true }
if ($Clean) { $packageArgs.Clean = $true }

Info "staging distribution -> $OutDir"
& $packageScript @packageArgs

$stage = Join-Path $OutDir 'cp3'
if (-not (Test-Path -LiteralPath (Join-Path $stage 'bin\cp3c.exe'))) {
    Fail "package.ps1 finished but '$stage\bin\cp3c.exe' is missing."
}

# --- MSI ---------------------------------------------------------------------
$msiScript = Join-Path $RepoRoot 'dist/packaging/windows/build-msi.ps1'
if (-not (Test-Path -LiteralPath $msiScript)) {
    Fail "missing '$msiScript'."
}
$msiArgs = @{
    StageDir = $stage
    OutDir   = $OutDir
    Version  = $version
    Arch     = $Arch
}
if ($NoMsiUi) { $msiArgs.NoUi = $true }
if ($WixPath) { $msiArgs.WixPath = $WixPath }

Info "building per-user MSI"
& $msiScript @msiArgs

$msi = Join-Path $OutDir ("cp3-" + $version + "-windows-" + $Arch + ".msi")
if (-not (Test-Path -LiteralPath $msi)) {
    Fail "build-msi.ps1 finished but '$msi' is missing."
}

# out/ is generated. Remove artifacts from the old zip/Inno pipeline so the
# deliverable is unambiguous: one MSI.
foreach ($pattern in @('cp3-*-windows-*.zip', 'cp3-setup-*.exe', 'install.ps1')) {
    Get-ChildItem -LiteralPath $OutDir -Filter $pattern -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Force
            Info "removed legacy artifact: $($_.Name)"
        }
}

# --- checksums ---------------------------------------------------------------
$hash = (Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash.ToLowerInvariant()
$sumsPath = Join-Path $OutDir 'SHA256SUMS.txt'
[System.IO.File]::WriteAllText(
    $sumsPath,
    ($hash + '  ' + [System.IO.Path]::GetFileName($msi) + "`r`n"),
    (New-Object System.Text.UTF8Encoding($false)))

Info "distribution: $stage"
Info "installer:    $msi"
Info "checksums:    $sumsPath"
Info "install:      double-click the MSI (or: msiexec /i `"$msi`" /qn)"
