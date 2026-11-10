# dist/extensions/cp3-vscode/scripts/package.ps1
#
# Builds the self-contained VSIX for the cp3 VS Code extension:
#   1. npm install (unless node_modules already exists or -SkipInstall)
#   2. esbuild production bundle -> dist/extension.js
#   3. vsce package -> dist/cp3-vscode-0.2.2.vsix
#
# The bundle is self-contained (vscode-languageclient is bundled in), so vsce
# runs with --no-dependencies and .vscodeignore keeps node_modules and the
# sources out of the .vsix.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
#
# Falls back to `npx --yes @vscode/vsce` when vsce is not installed locally.

[CmdletBinding()]
param(
    [switch]$SkipInstall,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vsixName = 'cp3-vscode-0.2.2.vsix'

Push-Location $root
try {
    if (-not $SkipInstall -and -not (Test-Path (Join-Path $root 'node_modules'))) {
        if (Test-Path (Join-Path $root 'package-lock.json')) {
            npm ci --no-audit --no-fund
        } else {
            npm install --no-audit --no-fund
        }
        if ($LASTEXITCODE -ne 0) { throw "npm install failed with exit code $LASTEXITCODE" }
    }

    if (-not $SkipBuild) {
        node esbuild.mjs --production
        if ($LASTEXITCODE -ne 0) { throw "esbuild failed with exit code $LASTEXITCODE" }
    }

    $dist = Join-Path $root 'dist'
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    $vsix = Join-Path $dist $vsixName
    if (Test-Path $vsix) { Remove-Item $vsix -Force }

    # --no-dependencies: the esbuild bundle is self-contained.
    # --allow-missing-repository / --no-rewrite-relative-links: the project
    # has no remote configured yet, so there is no URL to validate README
    # links against; both checks are turned off explicitly.
    $vsceArgs = @(
        'package',
        '--no-dependencies',
        '--allow-missing-repository',
        '--no-rewrite-relative-links',
        '--out', $vsix
    )
    $localVsce = Join-Path $root 'node_modules\.bin\vsce.cmd'
    if (Test-Path $localVsce) {
        & $localVsce @vsceArgs
    } else {
        npx --yes @vscode/vsce @vsceArgs
    }
    if ($LASTEXITCODE -ne 0) { throw "vsce package failed with exit code $LASTEXITCODE" }

    $item = Get-Item $vsix
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($vsix)
        try {
            $hashBytes = $sha256.ComputeHash($stream)
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha256.Dispose()
    }
    $hash = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLowerInvariant()
    Write-Output "vsix:   $($item.FullName)"
    Write-Output "size:   $($item.Length) bytes"
    Write-Output "sha256: $hash"
} finally {
    Pop-Location
}
