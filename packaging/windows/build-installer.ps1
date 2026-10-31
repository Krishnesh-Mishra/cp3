# =============================================================================
# packaging/windows/build-installer.ps1 - produce cp3-setup-<version>-x64.exe.
#
# One command builds the whole chain:
#   1. package.ps1 stages the distribution under <OutDir>\cp3 (unless
#      -SkipStage is given, which reuses an existing stage);
#   2. ISCC (Inno Setup 6) compiles packaging/windows/cp3.iss with the stage
#      paths injected as preprocessor defines.
#
# Usage (from the repository root):
#   powershell -ExecutionPolicy Bypass -File packaging\windows\build-installer.ps1
#   powershell ... build-installer.ps1 -NoVsix            # extension pending
#   powershell ... build-installer.ps1 -BuildDir build\scratch\packaging\build
#   powershell ... build-installer.ps1 -SkipStage -Clean  # reuse the stage
#
# Canonical output: <repo>\build\scratch\packaging\dist\cp3-setup-<version>-x64.exe
# (override with -OutDir). ISCC is located on PATH or in the standard Inno
# Setup 6 locations; when it is missing the script prints the exact winget
# command that installs it.
# =============================================================================
#Requires -Version 5.1
[CmdletBinding()]
param(
    # Stage root produced by package.ps1; default <repo>\build\scratch\packaging\dist\cp3.
    [string] $StageDir,
    # Directory that receives the setup exe; default the same dist root.
    [string] $OutDir,
    # Forwarded to package.ps1.
    [string] $BuildDir = "build",
    [string] $Version,
    [string] $VsixPath,
    [switch] $NoVsix,
    [switch] $SkipSmoke,
    [switch] $Clean,
    # Reuse an existing stage instead of running package.ps1.
    [switch] $SkipStage,
    # Explicit ISCC.exe (Inno Setup compiler).
    [string] $IsccPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Fail([string] $Message) { throw ("build-installer.ps1: " + $Message) }
function Info([string] $Message) { Write-Host ("installer: " + $Message) }

$WindowsDir = $PSScriptRoot
$PackageDir = Split-Path -Parent $WindowsDir
$RepoRoot = Split-Path -Parent $PackageDir

# --- version ----------------------------------------------------------------
if (-not $Version) {
    $versionFile = Join-Path $RepoRoot "version.txt"
    if (-not (Test-Path -LiteralPath $versionFile)) {
        Fail "version.txt is missing at the repository root"
    }
    $Version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.\-]+)?$') {
    Fail "version '$Version' is not <major>.<minor>.<patch>[-pre]"
}

$distRoot = Join-Path $RepoRoot "build\scratch\packaging\dist"
if (-not $OutDir) { $OutDir = $distRoot }
$OutDir = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $RepoRoot $OutDir }
if (-not $StageDir) { $StageDir = Join-Path $distRoot "cp3" }
$StageDir = if ([System.IO.Path]::IsPathRooted($StageDir)) { $StageDir } else { Join-Path $RepoRoot $StageDir }

# --- 1. stage ----------------------------------------------------------------
if (-not $SkipStage) {
    $packageScript = Join-Path $WindowsDir "package.ps1"
    $stageParams = @{
        BuildDir = $BuildDir
        OutDir = $OutDir
        Version = $Version
    }
    if ($VsixPath) { $stageParams.VsixPath = $VsixPath }
    if ($NoVsix) { $stageParams.NoVsix = $true }
    if ($SkipSmoke) { $stageParams.SkipSmoke = $true }
    if ($Clean) { $stageParams.Clean = $true }
    Info "staging via package.ps1"
    & $packageScript @stageParams
} else {
    Info "reusing stage $StageDir (-SkipStage)"
}

if (-not (Test-Path -LiteralPath (Join-Path $StageDir "bin\cp3c.exe"))) {
    Fail "stage '$StageDir' has no bin\cp3c.exe; run packaging\windows\package.ps1 first"
}
if (-not (Test-Path -LiteralPath (Join-Path $StageDir "manifest.json"))) {
    Fail "stage '$StageDir' has no manifest.json; run packaging\windows\package.ps1 first"
}

$compilerVersion = ""
$manifestPath = Join-Path $StageDir "manifest.json"
try {
    $manifest = [System.IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
    if ($manifest.compiler_version) { $compilerVersion = [string]$manifest.compiler_version }
} catch {
    $compilerVersion = ""
}
Info ("stage: $StageDir (compiler $compilerVersion)")

# --- 2. find ISCC --------------------------------------------------------------
$iscc = $IsccPath
if (-not $iscc) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )
    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe")
    }
    $candidates += (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { $iscc = $candidate; break }
    }
}
if (-not $iscc) {
    $command = Get-Command iscc -ErrorAction SilentlyContinue
    if ($command) { $iscc = $command.Source }
}
if (-not $iscc) {
    Fail @"
ISCC.exe (Inno Setup 6) was not found. Install it with:
    winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements
or pass -IsccPath <full path to ISCC.exe>.
"@
}
if (-not (Test-Path -LiteralPath $iscc)) { Fail "ISCC not found at '$iscc'" }
Info "iscc: $iscc"

# --- 3. compile the installer ---------------------------------------------------
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$issPath = Join-Path $WindowsDir "cp3.iss"
$defines = @(
    ("/DStageDir=" + $StageDir),
    ("/DVersion=" + $Version),
    ("/DOutputDir=" + $OutDir)
)
$vsixName = $null
$vsixDir = Join-Path $StageDir "share\vscode"
if (Test-Path -LiteralPath $vsixDir) {
    $stagedVsix = @(Get-ChildItem -LiteralPath $vsixDir -Filter "*.vsix" -File | Sort-Object Name)
    if ($stagedVsix.Count -ge 1) {
        $vsixName = $stagedVsix[0].Name
        $defines += ("/DVsixName=" + $vsixName)
    }
}
if ($vsixName) { Info "vsix: $vsixName" } else { Info "vsix: none staged (the installer will not bundle an extension)" }

& $iscc @defines $issPath
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed with exit code $LASTEXITCODE" }

$artifact = Join-Path $OutDir ("cp3-setup-" + $Version + "-x64.exe")
if (-not (Test-Path -LiteralPath $artifact)) {
    Fail "ISCC reported success but '$artifact' does not exist"
}
$file = Get-Item -LiteralPath $artifact
$hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
Info ("artifact: " + $file.FullName)
Info ("size: " + $file.Length + " bytes (" + [math]::Round($file.Length / 1MB, 2) + " MB)")
Info ("sha256: " + $hash)
Write-Output $artifact
