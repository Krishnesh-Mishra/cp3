# third_party/build.ps1
#
# Rebuilds every vendored static library in third_party/ from its pinned
# upstream source and re-runs the smoke matrix.
#
# This script contains only commands that were actually run when the delivered
# artifacts were produced (2026-10-18; PCRE2 was added 2026-10-19). The step
# scripts under
# build/scratch/vend/scripts/ are the verbatim record and are driven from here.
#
# Requirements (all must be on PATH):
#   git, gcc, g++, cmake, ninja, mingw32-make, ar
#   and Git Bash at C:\Program Files\Git\bin\bash.exe
#   (C:\Windows\system32\bash.exe is WSL and is deliberately NOT used).
#
# Toolchain used for the delivered artifacts: MinGW-W64 x86_64 ucrt posix-seh
# GCC 16.1.0 (WinLibs), cmake 4.3.2, ninja, GNU ar. No project needed
# -DCMAKE_POLICY_VERSION_MINIMUM=3.5 with this cmake.
#
# Layout:
#   build/scratch/vend/src/<lib>      upstream clones, detached at the pin
#   build/scratch/vend/build/<lib>    build trees
#   build/scratch/vend/install/<lib>  install prefixes
#   build/scratch/vend/scripts        recorded step scripts
#   build/scratch/vend/smoke          smoke sources/exes, SMOKE-RUNS.txt
#   third_party                       deliverable tree (this directory)
#
# Note: a full rebuild is not guaranteed to reproduce byte-identical archives
# (ar records object file timestamps); see third_party/MANIFEST.md.

[CmdletBinding()]
param(
    # stop after populating third_party/, before the smoke matrix
    [switch]$SkipSmokes
)

$ErrorActionPreference = 'Stop'

$Root    = Split-Path -Parent $PSScriptRoot
$Vend    = Join-Path $Root 'build\scratch\vend'
$SrcDir  = Join-Path $Vend 'src'
$BldDir  = Join-Path $Vend 'build'
$InsDir  = Join-Path $Vend 'install'
$Tp      = Join-Path $Root 'third_party'
$Scripts = Join-Path $Vend 'scripts'
$Bash    = 'C:\Program Files\Git\bin\bash.exe'

function Invoke-Line {
    param([string]$Line)
    Write-Host ("> " + $Line)
    & cmd.exe /c $Line
    if ($LASTEXITCODE -ne 0) { throw ("command failed with exit code {0}: {1}" -f $LASTEXITCODE, $Line) }
}

function Require-Path {
    param([string]$Path, [string]$What)
    if (-not (Test-Path $Path)) { throw ("missing {0}: {1}" -f $What, $Path) }
}

function Assert-Pin {
    param([string]$Dir, [string]$Sha)
    $head = (& git -C $Dir rev-parse HEAD).Trim()
    if ($head -ne $Sha) { throw ("{0} is at {1}, expected pin {2}" -f $Dir, $head, $Sha) }
    Write-Host ("pin ok: {0} at {1}" -f $Dir, $Sha)
}

# ---------------------------------------------------------------------------
# 0. sanity checks
# ---------------------------------------------------------------------------
if ((Split-Path -Leaf $PSScriptRoot) -ne 'third_party') {
    throw ('run this script from third_party/build.ps1; PSScriptRoot is ' + $PSScriptRoot)
}
foreach ($tool in 'git','gcc','g++','cmake','ninja','mingw32-make','ar') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw ('required tool not on PATH: ' + $tool)
    }
}
Require-Path $Bash 'Git Bash (C:\Program Files\Git\bin\bash.exe)'
foreach ($s in 'libsodium_build.sh','libsodium_make.sh','usockets_build.sh','uwebsockets_lib_build.sh',
               'pcre2_build.ps1','pcre2_vendor_copy.ps1','vendor_copy.ps1','run_smokes.ps1') {
    Require-Path (Join-Path $Scripts $s) 'recorded step script'
}

New-Item -ItemType Directory -Force -Path $SrcDir,$BldDir,$InsDir | Out-Null

# ---------------------------------------------------------------------------
# 1. clone pinned sources (shallow tag clone; each pin verified against SHA)
# ---------------------------------------------------------------------------
$pins = @(
    [pscustomobject]@{ Name='libsodium';   Url='https://github.com/jedisct1/libsodium.git';     Tag='1.0.22-RELEASE'; Sha='77e1ce5d6dee871c49ef211222ba18ef0c486bda' },
    [pscustomobject]@{ Name='libuv';       Url='https://github.com/libuv/libuv.git';            Tag='v1.53.0';        Sha='840404ce8ba7cc0204be52389a6cfff9f2c90fb6' },
    [pscustomobject]@{ Name='uWebSockets'; Url='https://github.com/uNetworking/uWebSockets.git';Tag='v20.80.0';       Sha='3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5' },
    [pscustomobject]@{ Name='oneTBB';      Url='https://github.com/uxlfoundation/oneTBB.git';   Tag='v2023.1.0';      Sha='3046c8b0c29df995980003ea24f4d78c80ec0c8d' },
    [pscustomobject]@{ Name='pcre2';       Url='https://github.com/PCRE2Project/pcre2.git';     Tag='pcre2-10.49';    Sha='6f9d7c1373262c541324a16a358785b33ef116cf' }
)
foreach ($p in $pins) {
    $dir = Join-Path $SrcDir $p.Name
    if (-not (Test-Path $dir)) {
        Invoke-Line ('git clone --depth 1 --branch {0} {1} {2}' -f $p.Tag, $p.Url, $dir)
    }
    Invoke-Line ('git -C {0} checkout --detach {1}' -f $dir, $p.Sha)
    Assert-Pin $dir $p.Sha
}

# ---------------------------------------------------------------------------
# 2. libsodium 1.0.22 (autotools; 1.0.22 has no CMake build)
#    libsodium_build.sh: mtime reordering, relative srcdir, Windows prefix
#    libsodium_make.sh : build + install, SHELL override for native mingw32-make
# ---------------------------------------------------------------------------
Invoke-Line ('"{0}" {1}' -f $Bash, (Join-Path $Scripts 'libsodium_build.sh'))
Invoke-Line ('"{0}" {1}' -f $Bash, (Join-Path $Scripts 'libsodium_make.sh'))

# ---------------------------------------------------------------------------
# 3. libuv v1.53.0 (CMake + Ninja, static)
# ---------------------------------------------------------------------------
$uvBld = (Join-Path $BldDir 'libuv') -replace '\\','/'
Remove-Item -Recurse -Force (Join-Path $BldDir 'libuv') -ErrorAction SilentlyContinue
Invoke-Line ('cmake -G Ninja -S {0} -B {1} -DCMAKE_BUILD_TYPE=Release -DLIBUV_BUILD_SHARED=OFF -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF -DCMAKE_INSTALL_PREFIX={2}' -f ((Join-Path $SrcDir 'libuv') -replace '\\','/'), $uvBld, ((Join-Path $InsDir 'libuv') -replace '\\','/'))
Invoke-Line ('cmake --build {0} --parallel 8' -f $uvBld)
Invoke-Line ('cmake --install {0}' -f $uvBld)

# ---------------------------------------------------------------------------
# 4. oneTBB v2023.1.0 (CMake + Ninja, static, tests off; upstream emits
#    "static is not supported" warnings - expected, see MANIFEST.md)
# ---------------------------------------------------------------------------
$tbbBld = (Join-Path $BldDir 'oneTBB') -replace '\\','/'
Remove-Item -Recurse -Force (Join-Path $BldDir 'oneTBB') -ErrorAction SilentlyContinue
Invoke-Line ('cmake -G Ninja -S {0} -B {1} -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DTBB_TEST=OFF -DCMAKE_INSTALL_PREFIX={2}' -f ((Join-Path $SrcDir 'oneTBB') -replace '\\','/'), $tbbBld, ((Join-Path $InsDir 'oneTBB') -replace '\\','/'))
Invoke-Line ('cmake --build {0} --parallel 8' -f $tbbBld)
Invoke-Line ('cmake --install {0}' -f $tbbBld)

# ---------------------------------------------------------------------------
# 5. uSockets (uWebSockets v20.80.0 has no CMakeLists; its own Makefile is the
#    supported build: WITH_LIBUV=1, WITH_LTO=0, TLS off via LIBUS_NO_SSL)
# ---------------------------------------------------------------------------
Invoke-Line ('"{0}" {1}' -f $Bash, (Join-Path $Scripts 'usockets_build.sh'))

# ---------------------------------------------------------------------------
# 6. populate third_party/ (reset include/lib dirs; copy headers, archives,
#    licenses from the install prefixes / source trees)
# ---------------------------------------------------------------------------
Invoke-Line ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File {0}' -f (Join-Path $Scripts 'vendor_copy.ps1'))

# ---------------------------------------------------------------------------
# 7. PCRE2 10.49 (core regex backend, DECISIONS D70; CMake + Ninja, static
#    8-bit only, tests/grep/JIT off - see MANIFEST.md for the exact flags).
#    pcre2_build.ps1 builds/installs into the vend prefix; pcre2_vendor_copy.ps1
#    copies include/ + lib/libpcre2-8.a + LICENCE.md into third_party/pcre2/
#    and writes its VERSION.txt ("<repo URL> <tag> <full SHA> <date built>").
# ---------------------------------------------------------------------------
Invoke-Line ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File {0}' -f (Join-Path $Scripts 'pcre2_build.ps1'))
Invoke-Line ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File {0}' -f (Join-Path $Scripts 'pcre2_vendor_copy.ps1'))

# ---------------------------------------------------------------------------
# 8. libuWebSockets.a (header-only upstream; built from one documented TU
#    against the vendored third_party headers, then copied into place)
# ---------------------------------------------------------------------------
Invoke-Line ('"{0}" {1}' -f $Bash, (Join-Path $Scripts 'uwebsockets_lib_build.sh'))

# ---------------------------------------------------------------------------
# 9. VERSION.txt pins ("<repo URL> <tag> <full SHA> <date built>"). The date is
#    the date the delivered artifacts were built; update it if you regenerate.
#    (pcre2/VERSION.txt is written by pcre2_vendor_copy.ps1 in step 7.)
# ---------------------------------------------------------------------------
Set-Content -Path (Join-Path $Tp 'libsodium\VERSION.txt')   -Value 'https://github.com/jedisct1/libsodium.git 1.0.22-RELEASE 77e1ce5d6dee871c49ef211222ba18ef0c486bda 2026-10-18' -Encoding ASCII
Set-Content -Path (Join-Path $Tp 'libuv\VERSION.txt')       -Value 'https://github.com/libuv/libuv.git v1.53.0 840404ce8ba7cc0204be52389a6cfff9f2c90fb6 2026-10-18' -Encoding ASCII
Set-Content -Path (Join-Path $Tp 'uWebSockets\VERSION.txt') -Value 'https://github.com/uNetworking/uWebSockets.git v20.80.0 3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5 2026-10-18' -Encoding ASCII
Set-Content -Path (Join-Path $Tp 'oneTBB\VERSION.txt')      -Value 'https://github.com/uxlfoundation/oneTBB.git v2023.1.0 3046c8b0c29df995980003ea24f4d78c80ec0c8d 2026-10-18' -Encoding ASCII

# ---------------------------------------------------------------------------
# 10. smoke matrix (each program is compiled from a clean directory using ONLY
#    third_party includes and archives, linked, and run to exit code 0):
#    3 link variants per program: dynamic, -static-libgcc -static-libstdc++,
#    and fully static -static -static-libgcc -static-libstdc++.
#    Evidence is written to build/scratch/vend/smoke/SMOKE-RUNS.txt.
# ---------------------------------------------------------------------------
if (-not $SkipSmokes) {
    Invoke-Line ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File {0}' -f (Join-Path $Scripts 'run_smokes.ps1'))
}

Write-Host ''
Write-Host 'third_party rebuild complete.'
