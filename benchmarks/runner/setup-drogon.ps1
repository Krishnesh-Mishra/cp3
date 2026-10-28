# Installs Drogon 1.9.13 (x64-windows) into the vcpkg tree used by the C++
# health-server entries. Drogon itself lives outside git in the vcpkg tree;
# benchmarks/.cache/build holds only the consumer build trees.
#
# vcpkg 2026-09 deduplicates Visual Studio instances that report the same
# version. This machine has both VS 18 Build Tools and VS 18 Community at
# 18.6; the Build Tools instance has no VC toolset, so plain
# `vcpkg install drogon` fails with "Unable to find a valid Visual Studio
# instance". Pointing vcpkg at Community (and exposing it via VS180COMNTOOLS)
# works around that.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File benchmarks/runner/setup-drogon.ps1
#
# Optional:
#   -VcpkgRoot C:\vcpkg
#   -Triplet x64-windows
#   -VisualStudioPath "C:\Program Files\Microsoft Visual Studio\18\Community"

[CmdletBinding()]
param(
    [string]$VcpkgRoot,
    [string]$Triplet = 'x64-windows',
    [string]$VisualStudioPath
)

$ErrorActionPreference = 'Stop'

if (-not $VcpkgRoot) {
    $candidates = @(
        $env:VCPKG_ROOT,
        'C:\vcpkg',
        (Join-Path $HOME 'dev\vcpkg'),
        (Join-Path $HOME 'vcpkg')
    )
    $VcpkgRoot = $candidates |
        Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'vcpkg.exe')) } |
        Select-Object -First 1
    if (-not $VcpkgRoot) { $VcpkgRoot = 'C:\vcpkg' }
}

$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkg)) {
    throw "vcpkg.exe not found at $vcpkg"
}

if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $VisualStudioPath = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath | Select-Object -First 1)
        if ($VisualStudioPath) { $VisualStudioPath = $VisualStudioPath.Trim() }
    }
}

if (-not $VisualStudioPath) {
    $VisualStudioPath = 'C:\Program Files\Microsoft Visual Studio\18\Community'
}

$vcvarsall = Join-Path $VisualStudioPath 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $vcvarsall)) {
    throw "No Visual C++ toolset found under $VisualStudioPath (missing $vcvarsall)"
}

$env:VCPKG_VISUAL_STUDIO_PATH = $VisualStudioPath

# The legacy VSxx0COMNTOOLS variable makes vcpkg add this instance even when
# another instance with the same version was already seen.
$vsFolder = Split-Path (Split-Path $VisualStudioPath -Parent) -Leaf
$vsMajor = switch ($vsFolder) {
    '18'   { '18' }
    '2022' { '17' }
    '2019' { '16' }
    '2017' { '15' }
    default { $null }
}
if ($vsMajor) {
    $envVarName = "VS${vsMajor}0COMNTOOLS"
    Set-Item -Path "Env:$envVarName" -Value (Join-Path $VisualStudioPath 'Common7\Tools\')
}

# Pick the toolset name vcpkg understands from the newest MSVC directory
# (14.5 -> v145, 14.3/14.4 -> v143, ...).
$msvcRoot = Join-Path $VisualStudioPath 'VC\Tools\MSVC'
if (Test-Path -LiteralPath $msvcRoot) {
    $msvc = Get-ChildItem -LiteralPath $msvcRoot -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($msvc) {
        switch ($msvc.Name.Substring(0, 4)) {
            '14.5' { $env:VCPKG_PLATFORM_TOOLSET = 'v145' }
            '14.4' { $env:VCPKG_PLATFORM_TOOLSET = 'v143' }
            '14.3' { $env:VCPKG_PLATFORM_TOOLSET = 'v143' }
            '14.2' { $env:VCPKG_PLATFORM_TOOLSET = 'v142' }
            '14.1' { $env:VCPKG_PLATFORM_TOOLSET = 'v141' }
        }
    }
}

Write-Host "vcpkg root:  $VcpkgRoot"
Write-Host "Visual Studio: $VisualStudioPath"
if ($env:VCPKG_PLATFORM_TOOLSET) {
    Write-Host "platform toolset: $env:VCPKG_PLATFORM_TOOLSET"
}
Write-Host "installing drogon:$Triplet ..."

& $vcpkg install "drogon:$Triplet" --clean-after-build
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Drogon installed under $(Join-Path $VcpkgRoot "installed\$Triplet")"
