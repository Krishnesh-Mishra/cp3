# dist/extensions/cp3-vscode/scripts/render-logo.ps1
#
# Renders the CP3 wordmark (images/cp3-logo.svg) into the 128x128 PNG the
# VS Code marketplace wants (images/cp3-logo.png). The SVGs are the source of
# truth; this script only rasterizes them. The marketplace page renders on
# white, so the default variant is the blackish-letter \-light\ wordmark -
# pass -Variant dark for the near-white variant used on dark backgrounds.
#
# The rasterizer is a locally installed Chromium browser in headless mode
# (Chrome or Edge) - the same rendering engine VS Code itself uses for SVG
# file icons, so the PNG and the file icon match. Nothing is downloaded and
# no image generator is involved: the artwork is hand-written vector art,
# only re-canvassed to a square and re-tinted for light contexts.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/render-logo.ps1             # light (blackish) - the store page default
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/render-logo.ps1 -Variant dark    # near-white wordmark for dark contexts
#
[CmdletBinding()]
param(
    [ValidateSet("light", "dark")]
    [string]$Variant = "light",
    [string]$Browser = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$svg = Join-Path $root ("images\\cp3-logo" + ($(if ($Variant -eq "light") { "-light" } else { "" }) + ".svg"))
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

# The screenshot is taken of a small wrapper page that shows the SVG scaled to
# exactly the 128x128 target. The SVG itself carries a large intrinsic size
# (it is also used unscaled as a file icon), so pointing the browser straight
# at the SVG would capture only its top-left corner.
$wrapper = Join-Path ([System.IO.Path]::GetTempPath()) "cp3-logo-render-$PID.html"
$svgUrl = 'file:///' + ($svg -replace '\\', '/')
@"
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><style>
  html, body { margin: 0; padding: 0; width: 128px; height: 128px; overflow: hidden; background: transparent; }
  img { display: block; width: 128px; height: 128px; }
</style></head>
<body><img src="$svgUrl" alt=""></body>
</html>
"@ | Set-Content -Path $wrapper -Encoding utf8
try {
    $url = 'file:///' + ($wrapper -replace '\\', '/')
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
} finally {
    Remove-Item $wrapper -Force -ErrorAction SilentlyContinue
}

$item = Get-Item $png
Write-Output "png:    $($item.FullName)"
Write-Output "variant: $Variant (source: $svg)"
Write-Output "size:   $($item.Length) bytes"
