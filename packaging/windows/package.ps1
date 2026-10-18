# =============================================================================
# packaging/windows/package.ps1 - stage an installable cp3 distribution.
#
# Inputs: a built compiler (<BuildDir>\bin\cp3c.exe) plus the repository's
# header trees, runtime translation units, vendored libraries and (optionally)
# the VS Code extension. Output: the layout frozen in PKG-CONTRACTS section 4
# under <OutDir>\cp3, plus manifest.json with SHA-256 hashes of every
# component. Re-running always produces a fresh, complete stage (idempotent).
#
# Usage (from the repository root):
#   powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1
#   powershell ... package.ps1 -BuildDir build\scratch\packaging\build
#   powershell ... package.ps1 -NoVsix          # extension workstream pending
#
# Layout produced (PKG-CONTRACTS section 4 plus the deltas recorded in
# build\scratch\packaging\INSTALLER-REPORT.md):
#   bin\cp3c.exe  bin\cp3.cmd  bin\<mingw runtime dlls>
#   include\<runtime|common|std\family>\**   header trees for user programs
#   src\runtime\src\**  src\std\<family>\**  runtime units compiled per program
#   third_party\<lib>\**                     vendored static libraries
#   share\vscode\cp3-vscode-<version>.vsix    when built (unless -NoVsix)
#   manifest.json  README.txt
#
# The include\ mapping is what src/cli's resolveIncludeRoot() falls back to
# when "<root>\src\<x>\include" does not exist, so the staged tree is fully
# relocatable: bin\cp3c.exe finds everything relative to its own location.
# =============================================================================
#Requires -Version 5.1
[CmdletBinding()]
param(
    # Built tree that owns bin\cp3c.exe (repo-relative by default).
    [string] $BuildDir = "build",
    # Directory that receives the stage; default <repo>\build\scratch\packaging\dist.
    [string] $OutDir,
    # Product version; default packaging\VERSION (single source).
    [string] $Version,
    # Explicit .vsix to stage; default extensions\cp3-vscode\dist\*.vsix.
    [string] $VsixPath,
    # Stage without a VS Code extension (extension workstream not landed yet).
    [switch] $NoVsix,
    # Stage without the MinGW runtime DLLs beside cp3c.exe (not recommended).
    [switch] $NoRuntimeDlls,
    # Do not run the staged compiler against a hello-world program.
    [switch] $SkipSmoke,
    # Also wipe the whole output directory (old installers) before staging.
    [switch] $Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Fail([string] $Message) { throw ("package.ps1: " + $Message) }
function Info([string] $Message) { Write-Host ("package: " + $Message) }

$WindowsDir = $PSScriptRoot
$PackageDir = Split-Path -Parent $WindowsDir
$RepoRoot = Split-Path -Parent $PackageDir

# --- version ----------------------------------------------------------------
if (-not $Version) {
    $versionFile = Join-Path $PackageDir "VERSION"
    if (-not (Test-Path -LiteralPath $versionFile)) {
        Fail "packaging\VERSION is missing (it is the single source of the version)"
    }
    $Version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.\-]+)?$') {
    Fail "version '$Version' is not <major>.<minor>.<patch>[-pre]"
}

# --- inputs -----------------------------------------------------------------
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $RepoRoot $BuildDir
}
$buildExe = Join-Path $buildRoot "bin\cp3c.exe"
if (-not (Test-Path -LiteralPath $buildExe)) {
    Fail "no compiler at '$buildExe'; build it first (cmake --build '$buildRoot')"
}
$shimSource = Join-Path $WindowsDir "cp3.cmd"
if (-not (Test-Path -LiteralPath $shimSource)) { Fail "missing '$shimSource'" }

if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "build\scratch\packaging\dist" }
$OutDir = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $RepoRoot $OutDir }
$stage = Join-Path $OutDir "cp3"

if ($Clean -and (Test-Path -LiteralPath $OutDir)) {
    Info "cleaning $OutDir"
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
if (Test-Path -LiteralPath $stage) {
    Info "removing previous stage $stage"
    Remove-Item -LiteralPath $stage -Recurse -Force
}
foreach ($dir in @("bin", "include", "src", "third_party", "share\vscode")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stage $dir) | Out-Null
}

function Copy-Tree([string] $From, [string] $To, [string] $What) {
    if (-not (Test-Path -LiteralPath $From)) { Fail "$What is missing: '$From'" }
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    Copy-Item -Path (Join-Path $From "*") -Destination $To -Recurse -Force
}

# --- bin: compiler, shim, MinGW runtime DLLs --------------------------------
Copy-Item -LiteralPath $buildExe -Destination (Join-Path $stage "bin\cp3c.exe") -Force
Copy-Item -LiteralPath $shimSource -Destination (Join-Path $stage "bin\cp3.cmd") -Force

$runtimeDlls = @("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")
if (-not $NoRuntimeDlls) {
    # Use `where` (not Get-Command) so a wrapper shim such as the dev ccache
    # `g++.exe` can sit first on PATH while the real WinLibs `g++.exe` behind it
    # is still reachable for the DLLs.
    $gxxPaths = @((& where.exe g++ 2>$null) | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
    if ($gxxPaths.Count -eq 0) {
        Fail ("no g++ on PATH, so the MinGW runtime DLLs cannot be located; " +
              "install the MinGW-w64 toolchain or pass -NoRuntimeDlls")
    }
    foreach ($dll in $runtimeDlls) {
        $source = $null
        foreach ($gxxPath in $gxxPaths) {
            if ($source) { break }
            $printed = (& $gxxPath ("-print-file-name=" + $dll) 2>$null | Select-Object -First 1)
            if ($printed -and $printed -ne $dll -and (Test-Path -LiteralPath $printed)) { $source = $printed }
            if (-not $source) {
                $candidate = Join-Path (Split-Path -Parent $gxxPath) $dll
                if (Test-Path -LiteralPath $candidate) { $source = $candidate }
            }
        }
        if (-not $source) { Fail "cannot locate '$dll' in the g++ toolchains ($($gxxPaths -join '; ')); pass -NoRuntimeDlls to stage without them" }
        Copy-Item -LiteralPath $source -Destination (Join-Path $stage ("bin\" + $dll)) -Force
        Info "runtime dll: $dll <- $source"
    }
}

# --- include: every header tree user programs need ---------------------------
# Mapping: src\<x>\include -> include\<x> (x = runtime, common, std\<family>).
$headerTrees = @(
    @{ From = "src\runtime\include"; To = "runtime" },
    @{ From = "src\common\include"; To = "common" }
)
$stdDir = Join-Path $RepoRoot "src\std"
if (-not (Test-Path -LiteralPath $stdDir)) { Fail "missing '$stdDir'" }
foreach ($family in (Get-ChildItem -LiteralPath $stdDir -Directory | Sort-Object Name)) {
    $include = Join-Path $family.FullName "include"
    if (Test-Path -LiteralPath $include) {
        $headerTrees += @{ From = ("src\std\" + $family.Name + "\include"); To = ("std\" + $family.Name) }
    }
}
foreach ($tree in $headerTrees) {
    Copy-Tree (Join-Path $RepoRoot $tree.From) (Join-Path $stage ("include\" + $tree.To)) ("header tree " + $tree.From)
}
Info ("headers: " + $headerTrees.Count + " trees -> include\")

# --- src: the runtime translation units compiled into every program ----------
$runtimeSrc = Join-Path $RepoRoot "src\runtime\src"
if (-not (Test-Path -LiteralPath $runtimeSrc)) { Fail "missing '$runtimeSrc'" }
$runtimeFiles = Get-ChildItem -LiteralPath $runtimeSrc -Recurse -File |
    Where-Object { $_.Extension -in ".cpp", ".hpp", ".h", ".inc" }
if ($runtimeFiles.Count -eq 0) { Fail "no runtime sources under '$runtimeSrc'" }
foreach ($file in $runtimeFiles) {
    $rel = $file.FullName.Substring($RepoRoot.Length + 1)
    $dest = Join-Path $stage $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
}
Info ("runtime sources: " + $runtimeFiles.Count + " files")

# --- std families: the translation units their native rows name ---------------
# The rows are the source of truth for what the build driver compiles
# (src/std/<family>/native.hpp, DECISIONS D68/D72). Quoted repo-relative paths
# are read from each row: paths inside an include tree are already staged under
# include\; third_party paths pull in their whole tree; the rest are runtime
# units copied next to their support headers.
$thirdPartyTrees = New-Object System.Collections.Generic.SortedSet[string]
$familySourceCount = 0
foreach ($family in (Get-ChildItem -LiteralPath $stdDir -Directory | Sort-Object Name)) {
    $row = Join-Path $family.FullName "native.hpp"
    if (-not (Test-Path -LiteralPath $row)) { continue }
    $rowText = Get-Content -LiteralPath $row -Raw
    $refs = [regex]::Matches($rowText, '"((?:src|third_party)/[^"]+)"') |
        ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
    foreach ($ref in $refs) {
        $normalized = $ref.Replace("/", "\")
        if ($normalized -like "third_party\*") {
            $parts = $normalized.Split("\")
            if ($parts.Length -lt 2) { Fail "malformed third_party reference '$ref' in $row" }
            $thirdPartyTrees.Add("third_party\" + $parts[1]) | Out-Null
            continue
        }
        if ($normalized -like "*\include") { continue }  # staged under include\
        $source = Join-Path $RepoRoot $normalized
        if (-not (Test-Path -LiteralPath $source)) { Fail "native row $row references missing '$ref'" }
        $dest = Join-Path $stage $normalized
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        Copy-Item -LiteralPath $source -Destination $dest -Force
        $familySourceCount++
    }
    # Support headers of the copied units (for example pump.hpp next to
    # runtime.cpp); native.hpp is compiler-side and never shipped.
    foreach ($header in (Get-ChildItem -LiteralPath $family.FullName -File |
                         Where-Object { $_.Extension -in ".hpp", ".h", ".inc" -and $_.Name -ne "native.hpp" })) {
        $rel = $header.FullName.Substring($RepoRoot.Length + 1)
        $dest = Join-Path $stage $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        Copy-Item -LiteralPath $header.FullName -Destination $dest -Force
    }
}
if ($thirdPartyTrees.Count -eq 0) { Fail "no third_party tree referenced by any native row; the rows were probably not read" }
foreach ($tree in $thirdPartyTrees) {
    Copy-Tree (Join-Path $RepoRoot $tree) (Join-Path $stage $tree) ("vendored tree " + $tree)
    Info "vendored: $tree"
}
Info ("std runtime units: " + $familySourceCount + " files")

# --- optional VS Code extension ---------------------------------------------
$vsixStaged = $null
if ($NoVsix) {
    if ($VsixPath) { Fail "-NoVsix and -VsixPath are mutually exclusive" }
    Info "vsix: skipped (-NoVsix)"
} else {
    $vsix = $VsixPath
    if ($vsix) {
        if (-not [System.IO.Path]::IsPathRooted($vsix)) { $vsix = Join-Path $RepoRoot $vsix }
    } else {
        $vsixDir = Join-Path $RepoRoot "extensions\cp3-vscode\dist"
        $preferred = Join-Path $vsixDir ("cp3-vscode-" + $Version + ".vsix")
        if (Test-Path -LiteralPath $preferred) {
            $vsix = $preferred
        } elseif (Test-Path -LiteralPath $vsixDir) {
            $found = @(Get-ChildItem -LiteralPath $vsixDir -Filter "*.vsix" -File | Sort-Object Name)
            if ($found.Count -ge 1) { $vsix = $found[0].FullName }
        }
    }
    if (-not $vsix -or -not (Test-Path -LiteralPath $vsix)) {
        Fail ("no VS Code extension staged: expected extensions\cp3-vscode\dist\cp3-vscode-" +
              $Version + ".vsix (extension workstream). Pass -NoVsix to stage without it.")
    }
    $vsixStaged = Split-Path -Leaf $vsix
    Copy-Item -LiteralPath $vsix -Destination (Join-Path $stage ("share\vscode\" + $vsixStaged)) -Force
    Info "vsix: $vsixStaged"
}

# --- compiler version (also proves the staged exe starts) --------------------
$compilerVersion = ""
try {
    $compilerVersion = ((& (Join-Path $stage "bin\cp3c.exe") --version 2>$null) | Select-Object -First 1)
} catch {
    $compilerVersion = ""
}
if (-not $compilerVersion -or ($LASTEXITCODE -ne 0)) {
    Fail "the staged bin\cp3c.exe does not run; check the build and the runtime DLLs"
}
$compilerVersion = $compilerVersion.Trim()
Info "compiler: $compilerVersion"

# --- README.txt ---------------------------------------------------------------
$readme = @"
cp3 $Version - C+++ compiler, runtime and tooling
=================================================

Layout
  bin\cp3c.exe     the compiler (also serves the editor LSP via `"cp3c lsp`")
  bin\cp3.cmd      the cp3 command: run, build, check, init, doctor,
                   install vs-code-plugin, lsp, ...
  bin\*.dll        MinGW runtime DLLs the compiler needs to start
  include\         header trees the compiler passes to the C++ toolchain
  src\             runtime units compiled into every cp3 program
  third_party\     vendored static libraries (std/network, std/crypto)
  share\vscode\    bundled VS Code extension, when built
  manifest.json    every component with its SHA-256

Prerequisites
  A C++ toolchain on PATH (g++ or clang++; MinGW-w64 is the tested Windows
  toolchain). Run `"cp3 doctor`" to see what the compiler will use. VS Code
  is optional.

Getting started
  cp3 doctor                  check the installation
  cp3 init my-app             scaffold a project
  cd my-app
  cp3 run                     build and run it

  cp3 install vs-code-plugin  install the bundled editor extension

Uninstall
  Settings > Apps > cp3, or run unins000.exe in the install directory. The
  user PATH entry this installer added is removed again.
"@
[System.IO.File]::WriteAllText((Join-Path $stage "README.txt"), $readme,
    (New-Object System.Text.UTF8Encoding($false)))

# --- manifest.json ------------------------------------------------------------
$builtAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
$manifestPath = Join-Path $stage "manifest.json"
$components = @()
foreach ($file in (Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)) {
    if ($file.FullName -eq $manifestPath) { continue }
    $rel = $file.FullName.Substring($stage.Length + 1).Replace("\", "/")
    $components += [ordered]@{
        path   = $rel
        bytes  = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$manifest = [ordered]@{
    name             = "cp3"
    version          = $Version
    compiler_version = $compilerVersion
    built_at         = $builtAt
    components       = $components
}
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText($manifestPath, ($manifestJson + "`r`n"),
    (New-Object System.Text.UTF8Encoding($false)))

# --- smoke: the staged compiler builds and runs a hello program ---------------
if (-not $SkipSmoke) {
    $smokeDir = Join-Path ([System.IO.Path]::GetTempPath()) ("cp3-package-smoke-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $smokeDir | Out-Null
    $previousRoot = $null
    if (Test-Path Env:\CP3_ROOT) { $previousRoot = $env:CP3_ROOT }
    try {
        $env:CP3_ROOT = $stage
        Set-Content -LiteralPath (Join-Path $smokeDir "main.cp3") -Encoding ASCII `
            -Value "import console from `"std/console`";`r`n`r`nconsole.log(`"package smoke ok`");"
        $output = ""
        $output = (& (Join-Path $stage "bin\cp3c.exe") run (Join-Path $smokeDir "main.cp3") 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0 -or $output -notmatch "package smoke ok") {
            Fail ("staged smoke build failed (exit $LASTEXITCODE):`n" + $output)
        }
        Info "smoke: staged compiler built and ran a hello program"
    } finally {
        if ($null -eq $previousRoot) { Remove-Item Env:\CP3_ROOT -ErrorAction SilentlyContinue }
        else { $env:CP3_ROOT = $previousRoot }
        Remove-Item -LiteralPath $smokeDir -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Info "smoke: skipped (-SkipSmoke)"
}

# --- summary ------------------------------------------------------------------
$totalBytes = 0
foreach ($component in $components) { $totalBytes += $component.bytes }
Info ("staged " + $components.Count + " components, " + [math]::Round($totalBytes / 1MB, 2) + " MB -> $stage")
Info "manifest: $manifestPath"
