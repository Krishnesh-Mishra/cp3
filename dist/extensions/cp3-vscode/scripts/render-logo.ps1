# dist/extensions/cp3-vscode/scripts/render-logo.ps1
#
# Renders dist/extensions/cp3-vscode/images/cp3-logo.svg into the 128x128 PNG the
# VS Code marketplace wants (images/cp3-logo.png). The SVG is the source of
# truth; this script only rasterizes it.
#
# The rasterizer is a locally installed Chromium browser in headless mode
# (Chrome or Edge) - the same rendering engine VS Code itself uses for SVG
# file icons, so the PNG and the file icon match. Nothing is downloaded and
# no image generator is involved: the artwork is hand-written vector art.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/render-logo.ps1
#
[CmdletBinding()]
param(
    [string]$Browser = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$svg = Join-Path $root 'images\cp3-logo.svg'
$png = Join-Path $root 'images\cp3-logo.png'

if (-not (Test-Path $svg)) { throw "missing source artwork: $svg" }

# The browser to rasterize with: an explicit -Browser wins, else the first
# locally installed Chromium is used.
if ($Browser -eq "") {
    $candidates = @(
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
        "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
    )
    $Browser = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if ($Browser -eq "" -or -not (Test-Path $Browser)) {
    throw "no Chromium browser found - install Chrome or Edge, or pass -Browser <path>"
}

$url = 'file:///' + ($svg -replace '\\', '/')
$args = @(
    '--headless',
    '--disable-gpu',
    '--no-sandbox',
    '--hide-scrollbars',
    '--force-device-scale-factor=1',
    '--default-background-color=00000000',
    '--window-size=128,128',
    "--screenshot=$png",
    $url
)
& $Browser $args | Out-Null
if ($LASTEXITCODE -ne 0) { throw "the browser failed to rasterize the logo" }
if (-not (Test-Path $png)) { throw "the browser wrote no PNG" }

$item = Get-Item $png
Write-Output "png:    $($item.FullName)"
Write-Output "size:   $($item.Length) bytes"
