# third_party/smokes/run_smokes.ps1
#
# Ledger F smoke matrix for the vendored internal-only libraries
# (xxhash, simdutf, unordered_dense, fast_float, moodycamel), following the
# exact pattern of third_party/build.ps1 section 10 (the uWS/libuv smoke
# matrix):
#
#   * each program is compiled from a clean directory using ONLY third_party
#     includes and archives, linked, and run to exit code 0;
#   * 3 link variants per program: dynamic, -static-libgcc -static-libstdc++,
#     and fully static -static -static-libgcc -static-libstdc++;
#   * evidence is written to build/scratch/vend/smoke/SMOKE-RUNS.txt
#     (the same file build.ps1 step 10 records to).
#
# This file is the NEW smoke runner for Ledger F. It is deliberately NOT
# referenced by third_party/build.ps1 yet: the lead wires it in (copy it to
# build/scratch/vend/scripts/run_smokes.ps1 alongside the existing step
# scripts, or invoke it directly) once the Ledger F archives are rebuilt by
# build.ps1. Until then it can be run by hand:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File third_party/smokes/run_smokes.ps1
#
# Requirements: g++ on PATH (same MinGW-W64 toolchain as build.ps1).
# It compiles and RUNS the smokes; do not run it as part of a code-only change.
#
# Smoke sources (this directory):
#   xxhash_smoke.cpp          XXH64("") / XXH64("Hello World!") pinned vectors
#   simdutf_smoke.cpp         validate_utf8 valid/invalid inputs
#   unordered_dense_smoke.cpp ankerl map/set insert/find/erase/growth
#   fast_float_smoke.cpp      fast_float::from_chars double/float + error contract
#   concurrentqueue_smoke.cpp enqueue/dequeue + 4x4 MPMC totals

[CmdletBinding()]
param(
    # override the smoke work/evidence dir (default: build/scratch/vend/smoke,
    # matching build.ps1). Relative paths are resolved against the repo root.
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'

$Tp    = Split-Path -Parent $PSScriptRoot            # third_party/
$Root  = Split-Path -Parent $Tp                       # repo root
$Smoke = $PSScriptRoot                                # this directory
if ([string]::IsNullOrEmpty($OutDir)) {
    $OutDir = Join-Path $Root 'build\scratch\vend\smoke'
} elseif (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $Root $OutDir
}
$Log = Join-Path $OutDir 'SMOKE-RUNS.txt'

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    throw 'required tool not on PATH: g++'
}
foreach ($lib in 'xxhash','simdutf','unordered_dense','fast_float','moodycamel') {
    if (-not (Test-Path (Join-Path $Tp $lib))) { throw ("missing vendored lib: " + $lib) }
}
foreach ($src in 'xxhash_smoke.cpp','simdutf_smoke.cpp','unordered_dense_smoke.cpp',
                 'fast_float_smoke.cpp','concurrentqueue_smoke.cpp') {
    if (-not (Test-Path (Join-Path $Smoke $src))) { throw ("missing smoke source: " + $src) }
}

# Clean work dir, exactly like build.ps1 step 10 ("compiled from a clean
# directory"). Logs/evidence from previous runs are preserved by appending
# to SMOKE-RUNS.txt only after this run's header block.
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem -Path $OutDir -File | Remove-Item -Force

function Write-Log {
    param([string]$Line)
    Write-Host $Line
    Add-Content -Path $Log -Value $Line -Encoding ASCII
}

function Invoke-Smoke {
    param(
        [string]$Name,          # e.g. 'xxhash'
        [string]$Source,        # smoke .cpp file name in this directory
        [string[]]$CompileArgs  # -I paths + -L/-l archive flags (third_party only)
    )
    $variants = @(
        @{ Name='dynamic';    Extra=@() },
        @{ Name='static-lib'; Extra=@('-static-libgcc','-static-libstdc++') },
        @{ Name='static';     Extra=@('-static','-static-libgcc','-static-libstdc++') }
    )
    foreach ($v in $variants) {
        $exe  = Join-Path $OutDir ("{0}_{1}.exe" -f $Name, $v.Name)
        $args = @('-std=c++20','-O2','-Wall','-Wextra') + $CompileArgs +
                @((Join-Path $Smoke $Source)) + $v.Extra + @('-o', $exe)
        $cmd  = 'g++ ' + ($args -join ' ')
        Write-Log ("> " + $cmd)
        & g++ @args
        if ($LASTEXITCODE -ne 0) { throw ("compile failed ({0}): {1}" -f $LASTEXITCODE, $cmd) }
        Write-Log ("> " + $exe)
        & $exe
        $code = $LASTEXITCODE
        Write-Log ("exit {0}: {1} ({2})" -f $code, $Name, $v.Name)
        if ($code -ne 0) { throw ("smoke failed with exit code {0}: {1} ({2})" -f $code, $Name, $v.Name) }
    }
}

Write-Log ("Ledger F smoke matrix - {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Log ("toolchain: " + ((& g++ --version | Select-Object -First 1)))
Write-Log ''

Invoke-Smoke 'xxhash' 'xxhash_smoke.cpp' @(
    "-I$(Join-Path $Tp 'xxhash\include')",
    "-L$(Join-Path $Tp 'xxhash\lib\windows-x64')", '-lxxhash'
)
Invoke-Smoke 'simdutf' 'simdutf_smoke.cpp' @(
    "-I$(Join-Path $Tp 'simdutf\include')",
    "-L$(Join-Path $Tp 'simdutf\lib\windows-x64')", '-lsimdutf'
)
Invoke-Smoke 'unordered_dense' 'unordered_dense_smoke.cpp' @(
    "-I$(Join-Path $Tp 'unordered_dense\include')"
)
Invoke-Smoke 'fast_float' 'fast_float_smoke.cpp' @(
    "-I$(Join-Path $Tp 'fast_float\include')"
)
Invoke-Smoke 'concurrentqueue' 'concurrentqueue_smoke.cpp' @(
    '-pthread',
    "-I$(Join-Path $Tp 'moodycamel\include')"
)

Write-Log ''
Write-Log 'Ledger F smoke matrix: all green (5 programs x 3 link variants)'
