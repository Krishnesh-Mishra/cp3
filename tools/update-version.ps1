# =============================================================================
# tools/update-version.ps1 - sync every version-bearing file from version.txt.
#
# version.txt at the repository root is the single source of truth. The shared
# implementation is tools/update-version.py so Windows and Linux apply exactly
# the same replacements.
#
# Usage:
#   .\tools\update-version.ps1
#   .\tools\update-version.ps1 -Check   # exit 1 if anything is stale
# =============================================================================
#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$updater = Join-Path $PSScriptRoot 'update-version.py'
if (-not (Test-Path -LiteralPath $updater)) {
    throw "update-version.ps1: missing '$updater'."
}

$python = Get-Command python -ErrorAction SilentlyContinue
$pythonArgs = @()
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
    if ($python) { $pythonArgs += '-3' }
}
if (-not $python) {
    throw "update-version.ps1: Python 3 is required to sync version.txt (install Python or add it to PATH)."
}

$pythonArgs += $updater
if ($Check) { $pythonArgs += '--check' }

& $python.Source @pythonArgs
if ($LASTEXITCODE -ne 0) {
    throw "update-version.ps1: version sync failed (exit $LASTEXITCODE)."
}
