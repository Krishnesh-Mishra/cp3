# =============================================================================
# vendor/third_party/build-windows-arm64.ps1 - build the Windows ARM64
# vendored static archives for cp3.
#
# Target: aarch64-w64-mingw32 with pinned llvm-mingw 20261104 (LLVM 23.1.5,
# ucrt). Install it to %USERPROFILE%\tools\llvm-mingw (default) or point
# -RootDir / CP3_MINGW_ROOT_ARM64 at its root. The pinned archive + SHA-256:
#
#   https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip
#   552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f  llvm-mingw-20261104-ucrt-x86_64.zip
#
# Each library is cloned at its pinned tag/SHA (git rev-parse must match),
# built static, and copied to vendor/third_party/<lib>/lib/windows-arm64/
# with a VERSION.txt recording the pin + SHA-256 of the produced archive.
#
# Libraries (the seven vendored by cp3):
#   libsodium    1.0.22-RELEASE  77e1ce5d6dee871c49ef211222ba18ef0c486bda  (autotools from release tarball, static)
#   libuv        v1.53.0         840404ce8ba7cc0204be52389a6cfff9f2c90fb6  (cmake, static)
#   uWebSockets  v20.80.0        3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5
#                 (uSockets @    86097c490263ab662d62e8e7b541390bdec7d149, WITH_OPENSSL=1)
#   xxhash       v0.8.3          (vendored source, single TU)
#   simdutf      v6.1.0          7852150e454ebacbf7e65cbf2c5e3feac5e660ec  (single TU)
#   pcre2        pcre2-10.49     6f9d7c1373262c541324a16a358785b33ef116cf  (cmake, static 8-bit)
#   openssl      openssl-3.5.5   67b5686b4419b4cb8caa502711c41815f5279751  (Configure mingw-arm64,
#                                no-asm, no-module: providers link statically;
#                                custom target in vendor/third_party/openssl/mingw-arm64.conf)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File vendor/third_party/build-windows-arm64.ps1
#   ... -Only xxhash,simdutf           # build a subset
#   ... -RootDir C:\path\to\llvm-mingw -Jobs 20
#
# Nothing is executed for the built compiler here; these are the vendored
# static libraries that let cp3c --target=aarch64-w64-mingw32 (and a future
# native arm64 cp3c) link user programs.
# =============================================================================
[CmdletBinding()]
param(
    [string] $RootDir = $env:CP3_MINGW_ROOT_ARM64,
    [int] $Jobs = [Environment]::ProcessorCount,
    [string[]] $Only = @()
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# Accept -Only xxhash,simdutf,pcre2 (comma string), -Only xxhash simdutf pcre2
# and -Only "xxhash,simdutf" alike.
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } |
          Where-Object { $_ })

# --- llvm-mingw root ---------------------------------------------------------
if (-not $RootDir) {
    $RootDir = Join-Path $env:USERPROFILE "tools\llvm-mingw"
}
$ArmBin = Join-Path $RootDir "bin"
$Clang   = Join-Path $ArmBin "aarch64-w64-mingw32-clang.exe"
$Clangxx = Join-Path $ArmBin "aarch64-w64-mingw32-clang++.exe"
$Ar      = Join-Path $ArmBin "aarch64-w64-mingw32-ar.exe"
$Ranlib  = Join-Path $ArmBin "aarch64-w64-mingw32-ranlib.exe"
foreach ($tool in @($Clang, $Clangxx, $Ar, $Ranlib)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw ("llvm-mingw not found at '$RootDir' (expected $tool).\n" +
               "Install the pinned 20261104 ucrt x86_64 archive to %USERPROFILE%\tools\llvm-mingw,\n" +
               "or pass -RootDir / set CP3_MINGW_ROOT_ARM64. See the header of this script for the URL and SHA-256.")
    }
}

$Toolchain = Join-Path $RepoRoot "vendor\tools\cmake\windows-aarch64-llvm-mingw.cmake"
if (-not (Test-Path -LiteralPath $Toolchain)) {
    throw "toolchain file missing: $Toolchain"
}

$Tools = @{}
# Note: no system perl is required. Git for Windows ships cygwin perl
# (x86_64-cygwin) which the openssl Configure needs (unix-like paths), while
# Strawberry's native perl cannot satisfy the unix-checker.
foreach ($tool in @("git", "cmake", "ninja")) {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $found) {
        throw "required tool '$tool' not found on PATH"
    }
    $Tools[$tool] = $found.Source
}
# GNU make for uSockets (Strawberry ships gmake; Git/MSYS2 also work)
$Make = $null
foreach ($candidate in @("C:\Strawberry\c\bin\gmake.exe", (Get-Command "make" -ErrorAction SilentlyContinue).Source, (Get-Command "mingw32-make" -ErrorAction SilentlyContinue).Source)) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) { $Make = $candidate; break }
}
$GitBash = $null
foreach ($candidate in @("C:\Program Files\Git\usr\bin\sh.exe", "C:\Program Files\Git\bin\sh.exe")) {
    if (Test-Path -LiteralPath $candidate) { $GitBash = $candidate; break }
}

# Git-for-Windows sh sees /c/Users/... as C:\Users\...; autoconf also colon-
# splits its candidate lists, so anything passed THROUGH sh must be POSIX.
function ToPosix([string] $path) {
    $q = $path -replace '\\', '/'
    if ($q -match '^([A-Za-z]):(.*)$') {
        return '/' + $Matches[1].ToLower() + $Matches[2]
    }
    return $q
}

# 8.3 short path for $GitBash: GNU make on Windows cannot handle SHELL values
# with spaces (recipes re-invoke $(SHELL) unquoted).
function Get-ShortPathWin([string] $path) {
    $fso = New-Object -ComObject Scripting.FileSystemObject
    if (Test-Path -LiteralPath $path -PathType Container) {
        try { return $fso.GetFolder($path).ShortPath } catch { return $path }
    }
    try { return $fso.GetFile($path).ShortPath } catch { return $path }
}

$WORK = Join-Path $RepoRoot "build\scratch\vendor-windows-arm64"
$SRC = Join-Path $WORK "src"
$OUT = Join-Path $RepoRoot "vendor\third_party"
New-Item -ItemType Directory -Force -Path $SRC | Out-Null

function Info([string] $Message) { Write-Host ("arm64: " + $Message) }

function Clone-Pin([string] $Name, [string] $Url, [string] $Tag, [string] $Sha) {
    $dir = Join-Path $SRC $Name
    if (-not (Test-Path -LiteralPath (Join-Path $dir ".git"))) {
        Info "cloning $Name ($Tag)"
        & $Tools["git"] clone --depth 1 --branch $Tag $Url $dir | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "clone of $Name failed" }
    }
    & $Tools["git"] -C $dir checkout --detach $Sha | Out-Null
    $head = (& $Tools["git"] -C $dir rev-parse HEAD).Trim()
    if ($head -ne $Sha) {
        throw "$Name checkout ($head) does not match pinned SHA $Sha"
    }
    return $dir
}

function Write-ArchiveMeta([string] $Lib, [string[]] $Archives, [string] $Pin) {
    $destDir = Join-Path $OUT "$Lib\lib\windows-arm64"
    $meta = @("$Lib=$Pin", "arch=windows-arm64", "built=$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))")
    foreach ($archive in $Archives) {
        $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Archives.Count -gt 1) {
            $meta += ("sha256={0} ({1})" -f $hash, (Split-Path -Leaf $archive))
        } else {
            $meta += "sha256=$hash"
        }
        Info ("{0}: {1} bytes, sha256={2}" -f $archive, (Get-Item $archive).Length, $hash)
    }
    $meta += "toolchain=llvm-mingw-20261104-ucrt-x86_64"
    Set-Content -LiteralPath (Join-Path $destDir "VERSION.txt") -Value $meta -Encoding ASCII
}

function Invoke([string] $Exe, [string[]] $ArgList, [string] $WorkingDir = $RepoRoot) {
    Info ("`${0} {1}" -f ($Exe -replace '^.*[/\\]', ''), ($ArgList -join ' '))
    Push-Location $WorkingDir
    try {
        & $Exe @ArgList
        if ($LASTEXITCODE -ne 0) { throw "command failed (exit $LASTEXITCODE): $Exe" }
    } finally {
        Pop-Location
    }
}

# --- libsodium (official release tarball; ships configure + ltmain.sh) ---------
# The pinned git tag (77e1ce5d6dee871c49ef211222ba18ef0c486bda) is the same
# code as the libsodium-1.0.22 release; the tarball carries the autoconf-
# generated files the git tree lacks (autoconf is not installable here).
#
# The configure/make run is delegated to a generated POSIX script executed by
# Git for Windows sh (not to nested PowerShell quoting), with an MSYS PATH
# that contains no space-laden entries: autoconf caches the first tool path
# it finds unquoted, so "/c/Program Files/..." breaks every recipe (the
# windows-x64 lane used the same dance with mingw32-make + an 8.3 SHELL).
# The script builds out-of-tree against the tarball with mtimes normalized
# (2000/2001) so autoconf never tries to regenerate configure/Makefile.in.
function Build-Libsodium {
    $url = "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22.tar.gz"
    $tarball = Join-Path $SRC "libsodium-1.0.22.tar.gz"
    $dir = Join-Path $SRC "libsodium-1.0.22"
    if (-not (Test-Path -LiteralPath $dir)) {
        Info "downloading $url"
        & curl.exe -fSL --retry 3 -o $tarball $url
        if ($LASTEXITCODE -ne 0) { throw "download of $url failed" }
        $hash = (Get-FileHash -LiteralPath $tarball -Algorithm SHA256).Hash.ToLowerInvariant()
        Info "tarball sha256=$hash (recorded in the archive VERSION.txt)"
        tar.exe -xzf $tarball -C $SRC | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "extracting $tarball failed" }
    }
    if (-not $GitBash) { throw "Git for Windows sh.exe not found (required to run libsodium's configure)" }
    if (-not $Make) { $Make = "C:\Strawberry\c\bin\gmake.exe" }
    $shShort = ToPosix (Get-ShortPathWin $GitBash)          # e.g. C:/PROGRA~1/Git/usr/bin/sh.exe
    $repoWin = ($RepoRoot -replace '\\', '/')                # no spaces in this repo path
    $rootWin = ($RootDir -replace '\\', '/')
    $scriptPath = Join-Path $WORK "build-libsodium-windows-arm64.sh"
    # NOTE: the LF-only script must not contain the token $env: or PowerShell
    # would still see it; everything stays shell-side.
    $posix = @'
#!/bin/sh
# Generated by build-windows-arm64.ps1: build libsodium for windows-arm64.
set -e
REPO_WIN="$1"       # C:/Users/.../cp3 (no spaces)
ARMROOT_WIN="$2"    # C:/Users/.../llvm-mingw (no spaces)
JOBS="$3"
SHELL8="$4"        # 8.3 short path to Git sh, for recipe re-invocation
drive=${REPO_WIN%%:*}
REPO="/$drive${REPO_WIN#*:}"
aroot=${ARMROOT_WIN%%:*}
ARM="/$aroot${ARMROOT_WIN#*:}/bin"
WORK="$REPO/build/scratch/vendor-windows-arm64"
SRC="$WORK/src/libsodium-1.0.22"
BDIR="$WORK/build-gitbash/libsodium"
INSTALL="$REPO_WIN/build/scratch/vendor-windows-arm64/install/libsodium"
# MSYS PATH without space-laden entries: autoconf caches tool paths unquoted.
export PATH="$REPO/build/scratch/bin:$ARM:/usr/bin:/c/Strawberry/c/bin:$PATH"
# Normalize mtimes (2000/2001) so autoconf does not try to regenerate
# configure/Makefile.in (autoconf is not installed here).
find "$SRC" -type f -exec touch -d '2000-01-01 00:00:00' {} +
find "$SRC" -name 'Makefile.in' -exec touch -d '2001-01-01 00:00:00' {} +
touch -d '2001-01-01 00:00:00' "$SRC/configure" "$SRC/aclocal.m4"
rm -rf "$BDIR" "$REPO/build/scratch/vendor-windows-arm64/install/libsodium"
mkdir -p "$BDIR"
cd "$BDIR"
SRC_REL=../../src/libsodium-1.0.22
"$SRC_REL/configure" --host=aarch64-w64-mingw32 \
    --disable-shared --enable-static --disable-dependency-tracking \
    --disable-maintainer-mode \
    --prefix="$INSTALL" \
    CC=aarch64-w64-mingw32-clang CXX=aarch64-w64-mingw32-clang++ \
    AR=aarch64-w64-mingw32-ar RANLIB=aarch64-w64-mingw32-ranlib \
    STRIP=aarch64-w64-mingw32-strip \
    CFLAGS=-O3
make -j"$JOBS" SHELL="$SHELL8"
make      SHELL="$SHELL8" install
echo "install tree: $INSTALL"
'@
    [System.IO.File]::WriteAllText($scriptPath, ($posix -replace "`r`n", "`n"), [System.Text.Encoding]::ASCII)
    $env:SHELL = $shShort
    & $GitBash $scriptPath $repoWin $rootWin "$Jobs" $shShort
    if ($LASTEXITCODE -ne 0) { throw "libsodium build failed (exit $LASTEXITCODE)" }
    $archive = Join-Path $WORK "install\libsodium\lib\libsodium.a"
    if (-not (Test-Path -LiteralPath $archive)) { throw "libsodium.a was not produced at $archive" }
    $destDir = Join-Path $OUT "libsodium\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $archive -Destination (Join-Path $destDir "libsodium.a") -Force
    Write-ArchiveMeta "libsodium" (Join-Path $destDir "libsodium.a") "77e1ce5d6dee871c49ef211222ba18ef0c486bda"
}

# --- libuv ----------------------------------------------------------------------
function Build-LibUV {
    $dir = Clone-Pin "libuv" "https://github.com/libuv/libuv.git" "v1.53.0" "840404ce8ba7cc0204be52389a6cfff9f2c90fb6"
    $bdir = Join-Path $WORK "build\libuv"
    Invoke $Tools["cmake"] @("-S", $dir, "-B", $bdir, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DCP3_MINGW_ROOT_ARM64=$RootDir",
        "-DLIBUV_BUILD_SHARED=OFF",
        "-DLIBUV_BUILD_TESTS=OFF",
        "-DLIBUV_BUILD_BENCH=OFF") | Out-Null
    Invoke $Tools["cmake"] @("--build", $bdir, "--parallel", $Jobs) | Out-Null
    $archive = Get-ChildItem -LiteralPath $bdir -Recurse -Filter "libuv.a" | Select-Object -First 1
    if (-not $archive) { throw "libuv.a was not produced" }
    $destDir = Join-Path $OUT "libuv\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $archive.FullName -Destination (Join-Path $destDir "libuv.a") -Force
    Write-ArchiveMeta "libuv" (Join-Path $destDir "libuv.a") "840404ce8ba7cc0204be52389a6cfff9f2c90fb6"
}

# --- xxhash (vendored single TU) ---------------------------------------------------
function Build-XXHash {
    $source = Join-Path $OUT "xxhash\include\xxhash.c"
    if (-not (Test-Path -LiteralPath $source)) { throw "vendored xxhash.c missing at $source" }
    $obj = Join-Path $WORK "build\xxhash.o"
    $archive = Join-Path $WORK "build\libxxhash.a"
    New-Item -ItemType Directory -Force -Path (Split-Path $obj) | Out-Null
    Invoke $Clang @("-O3", "-std=c99", "-Wall", "-c", $source, "-o", $obj) | Out-Null
    Invoke $Ar @("rcs", $archive, $obj) | Out-Null
    Invoke $Ranlib @($archive) | Out-Null
    $destDir = Join-Path $OUT "xxhash\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $archive -Destination (Join-Path $destDir "libxxhash.a") -Force
    if (Test-Path (Join-Path $OUT "xxhash\VERSION.txt")) {
        Copy-Item -LiteralPath (Join-Path $OUT "xxhash\VERSION.txt") -Destination (Join-Path $destDir "VERSION.txt") -Force
    }
    Write-ArchiveMeta "xxhash" (Join-Path $destDir "libxxhash.a") "e626a72bc2321cd320e953a0ccf1584cad60f363"
}

# --- simdutf (pinned single TU clone) ------------------------------------------------
function Build-Simdutf {
    $dir = Clone-Pin "simdutf" "https://github.com/simdutf/simdutf.git" "v6.1.0" "7852150e454ebacbf7e65cbf2c5e3feac5e660ec"
    $source = Join-Path $dir "src\simdutf.cpp"
    $include = Join-Path $dir "include"
    $srcDir = Join-Path $dir "src"
    if (-not (Test-Path -LiteralPath $source)) { throw "missing $source" }
    $obj = Join-Path $WORK "build\simdutf.o"
    $archive = Join-Path $WORK "build\libsimdutf.a"
    New-Item -ItemType Directory -Force -Path (Split-Path $obj) | Out-Null
    Invoke $Clangxx @("-O3", "-std=c++17",
        "-I$include", "-I$srcDir",
        "-c", $source, "-o", $obj) | Out-Null
    Invoke $Ar @("rcs", $archive, $obj) | Out-Null
    Invoke $Ranlib @($archive) | Out-Null
    $destDir = Join-Path $OUT "simdutf\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $archive -Destination (Join-Path $destDir "libsimdutf.a") -Force
    Write-ArchiveMeta "simdutf" (Join-Path $destDir "libsimdutf.a") "7852150e454ebacbf7e65cbf2c5e3feac5e660ec"
}

# --- pcre2 ---------------------------------------------------------------------------
function Build-Pcre2 {
    $dir = Clone-Pin "pcre2" "https://github.com/PCRE2Project/pcre2.git" "pcre2-10.49" "6f9d7c1373262c541324a16a358785b33ef116cf"
    $bdir = Join-Path $WORK "build\pcre2"
    Invoke $Tools["cmake"] @("-S", $dir, "-B", $bdir, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DCP3_MINGW_ROOT_ARM64=$RootDir",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DPCRE2_BUILD_PCRE2_8=ON",
        "-DPCRE2_BUILD_PCRE2_16=OFF",
        "-DPCRE2_BUILD_PCRE2_32=OFF",
        "-DPCRE2_BUILD_TESTS=OFF",
        "-DPCRE2_BUILD_PCRE2GREP=OFF",
        "-DPCRE2_DEBUG=OFF") | Out-Null
    Invoke $Tools["cmake"] @("--build", $bdir, "--parallel", $Jobs) | Out-Null
    $archive = Get-ChildItem -LiteralPath $bdir -Recurse -Filter "libpcre2-8.a" | Select-Object -First 1
    if (-not $archive) { throw "libpcre2-8.a was not produced" }
    $destDir = Join-Path $OUT "pcre2\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $archive.FullName -Destination (Join-Path $destDir "libpcre2-8.a") -Force
    Write-ArchiveMeta "pcre2" (Join-Path $destDir "libpcre2-8.a") "6f9d7c1373262c541324a16a358785b33ef116cf"
}

# --- uWebSockets + uSockets (WITH_OPENSSL=1) -------------------------------------------
function Build-USockets {
    $dir = Clone-Pin "uWebSockets" "https://github.com/uNetworking/uWebSockets.git" "v20.80.0" "3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5"
    & $Tools["git"] -C $dir submodule update --init --depth 1 uSockets | Out-Null
    $usockets = Join-Path $dir "uSockets"
    $uq = Join-Path $usockets ".git"
    if (-not (Test-Path -LiteralPath $uq)) {
        & $Tools["git"] -C $usockets fetch --depth 1 origin 86097c490263ab662d62e8e7b541390bdec7d149 | Out-Null
        & $Tools["git"] -C $usockets checkout --detach 86097c490263ab662d62e8e7b541390bdec7d149 | Out-Null
    }
    $suHead = (& $Tools["git"] -C $usockets rev-parse HEAD).Trim()
    if ($suHead -ne "86097c490263ab662d62e8e7b541390bdec7d149") {
        throw "uSockets checkout ($suHead) does not match pinned SHA 86097c490263ab662d62e8e7b541390bdec7d149"
    }
    # The Windows Makefile hardcodes -flto on the SSL sni_tree compilation unit
    # (windows-x64 note: emits symbol-free LTO slim objects); strip it, like the
    # windows-x64 build did.
    $makefile = Join-Path $usockets "Makefile"
    $content = Get-Content -LiteralPath $makefile -Raw
    $patched = $content -replace '-flto', ''
    if ($patched -ne $content) {
        Set-Content -LiteralPath $makefile -Value $patched -Encoding ASCII
        Info "stripped -flto from uSockets Makefile (windows-x64 parity)"
    }
    if (-not $Make) {
        throw "no make found (Strawberry gmake or Git/MSYS2 make is required for uSockets)"
    }
    if (-not $GitBash) {
        throw "Git for Windows sh.exe not found (required as SHELL for the uSockets Makefile)"
    }
    $libuvInc = ToPosix (Join-Path $OUT "libuv\include")
    $opensslInc = ToPosix (Join-Path $OUT "openssl\include")
    # GNU make on Windows cannot handle SHELL values with spaces (recipes
    # re-invoke $(SHELL) unquoted); use the 8.3 short path. Repo/tool paths
    # have no spaces, but backslashes must become forward slashes for sh.
    $shShort = ToPosix (Get-ShortPathWin $GitBash)
    $env:SHELL = $shShort
    $oldPath = $env:PATH
    # sh is invoked directly (not through a Git Bash startup), so Git's
    # coreutils are not auto-added; prepend Git's 8.3 usr/bin so GNU make
    # (native) and sh both see it space-free and colon-safe.
    $gitUsrShort = Get-ShortPathWin (Split-Path -Parent $GitBash)
    $env:PATH = "$gitUsrShort;$env:PATH"
    try {
        Invoke $Make @("-C", $usockets, "CC=$((ToPosix $Clang))", "CXX=$((ToPosix $Clangxx))", "AR=$((ToPosix $Ar))",
            "WITH_OPENSSL=1", "WITH_LIBUV=1", "WITH_LTO=0",
            "CFLAGS=-I$libuvInc -I$opensslInc",
            "SHELL=$shShort") | Out-Null
    } finally {
        $env:PATH = $oldPath
    }
    $archive = Join-Path $usockets "uSockets.a"
    if (-not (Test-Path -LiteralPath $archive)) { throw "uSockets.a was not produced" }
    $destDir = Join-Path $OUT "uWebSockets\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $archive -Destination (Join-Path $destDir "libuSockets.a") -Force
    Write-ArchiveMeta "uWebSockets" (Join-Path $destDir "libuSockets.a") "3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5"
}

# --- openssl --------------------------------------------------------------------------
# OpenSSL 3.5.5 has no mingw aarch64 target; mingw-arm64 (inheriting
# mingw-common, no-asm) is provided by vendor/third_party/openssl/mingw-arm64.conf
# and loaded via OPENSSL_LOCAL_CONFIG_DIR. The Configure/make run is delegated
# to a generated POSIX script executed by Git for Windows sh. Strawberry's
# native perl FAILS the unix-checker (rel2abs() yields backslashes), so the
# configure uses Git's cygwin perl 5.42; recipes run under Strawberry gmake
# (native) with an 8.3 SHELL path.
function Build-OpenSSL {
    $dir = Clone-Pin "openssl" "https://github.com/openssl/openssl.git" "openssl-3.5.5" "67b5686b4419b4cb8caa502711c41815f5279751"
    $bdir = Join-Path $WORK "build\openssl"
    if (Test-Path -LiteralPath $bdir) { Remove-Item -Recurse -Force $bdir }
    New-Item -ItemType Directory -Force -Path $bdir | Out-Null
    if (-not $GitBash) { throw "Git for Windows sh.exe not found (required to drive the OpenSSL build)" }
    if (-not $Make) { $Make = "C:\Strawberry\c\bin\gmake.exe" }
    $shShort = ToPosix (Get-ShortPathWin $GitBash)
    $repoWin = ($RepoRoot -replace '\\', '/')
    $rootWin = ($RootDir -replace '\\', '/')
    # Git's cygwin perl (x86_64-cygwin) is the only perl here that passes the
    # openssl unix-checker, but it is missing some pure-perl core modules.
    # Stage them from Strawberry (pure .pm) into build/scratch. Stage into an
    # EXISTING dir: Copy-Item -Recurse with a missing -Destination creates the
    # destination AS the source instead of nesting it.
    $perlSupport = Join-Path $WORK "perl-support"
    New-Item -ItemType Directory -Force -Path $perlSupport | Out-Null
    # (pure-perl modules from Strawberry's lib; File/Which lives in vendor/lib)
    $strawberryLib = "C:\Strawberry\perl\lib"
    $strawberryVendor = "C:\Strawberry\perl\vendor\lib"
    if (-not (Test-Path -LiteralPath (Join-Path $perlSupport "ExtUtils\MakeMaker.pm"))) {
        if (-not (Test-Path -LiteralPath "$strawberryLib\ExtUtils\MakeMaker.pm")) { throw "Strawberry ExtUtils missing under $strawberryLib" }
        Copy-Item -LiteralPath "$strawberryLib\ExtUtils" -Destination $perlSupport -Recurse -Force
    }
    if (-not (Test-Path -LiteralPath (Join-Path $perlSupport "Pod\Usage.pm"))) {
        if (-not (Test-Path -LiteralPath "$strawberryLib\Pod\Usage.pm")) { throw "Strawberry Pod missing under $strawberryLib" }
        Copy-Item -LiteralPath "$strawberryLib\Pod" -Destination $perlSupport -Recurse -Force
    }
    foreach ($m in @("Locale\Maketext\Simple.pm", "Params\Check.pm")) {
        $srcM = Join-Path $strawberryLib $m
        $dstM = Join-Path $perlSupport $m
        if (-not (Test-Path -LiteralPath $dstM)) {
            if (-not (Test-Path -LiteralPath $srcM)) { throw "Strawberry perl module missing: $srcM" }
            New-Item -ItemType Directory -Force -Path (Split-Path $dstM) | Out-Null
            Copy-Item -LiteralPath $srcM -Destination $dstM -Force
        }
    }
    $whichDst = Join-Path $perlSupport "File\Which.pm"
    if (-not (Test-Path -LiteralPath $whichDst)) {
        if (-not (Test-Path -LiteralPath "$strawberryVendor\File\Which.pm")) { throw "Strawberry File::Which missing under $strawberryVendor" }
        New-Item -ItemType Directory -Force -Path (Split-Path $whichDst) | Out-Null
        Copy-Item -LiteralPath "$strawberryVendor\File\Which.pm" -Destination $whichDst -Force
    }
    $scriptPath = Join-Path $WORK "build-openssl-windows-arm64.sh"
    $posix = @'
#!/bin/sh
# Generated by build-windows-arm64.ps1: build openssl for windows-arm64.
set -e
REPO_WIN="$1"       # C:/Users/.../cp3 (no spaces)
ARMROOT_WIN="$2"    # C:/Users/.../llvm-mingw (no spaces)
JOBS="$3"
SHELL8="$4"        # 8.3 short path to Git sh, for recipe re-invocation
drive=${REPO_WIN%%:*}
REPO="/$drive${REPO_WIN#*:}"
aroot=${ARMROOT_WIN%%:*}
ARM="/$aroot${ARMROOT_WIN#*:}/bin"
WORK="$REPO/build/scratch/vendor-windows-arm64"
SRC="$WORK/src/openssl"
BDIR="$WORK/build/openssl"
export PATH="$REPO/build/scratch/bin:/usr/bin:/c/Strawberry/c/bin:$ARM:$PATH"
# Git's cygwin perl (x86_64-cygwin) passes the unix-checker but lacks two
# pure-perl core modules; stage them from Strawberry into build/scratch.
# NOTE: PERL5LIB must be a RELATIVE path. Absolute drive-rooted paths get
# rewritten to C:/... at the cygwin->native->cygwin boundary (script ->
# gmake -> recipe sh), and cygwin perl then splits them on ':'. A relative
# path survives untouched and resolves from the build dir ($BDIR).
export PERL5LIB=../../perl-support
# cygwin perl reads the local-targets file via catdir; MSYS paths work.
export OPENSSL_LOCAL_CONFIG_DIR="$REPO/vendor/third_party/openssl"
rm -rf "$BDIR"
mkdir -p "$BDIR"
cd "$BDIR"
perl "$SRC/Configure" mingw-arm64 no-shared no-tests no-apps no-module -O3 \
    CC=aarch64-w64-mingw32-clang CXX=aarch64-w64-mingw32-clang++ \
    AR=aarch64-w64-mingw32-ar RANLIB=aarch64-w64-mingw32-ranlib
# cygwin Configure bakes MSYS-form absolute paths (/C/...) into the generated
# Makefile; native GNU make cannot stat those. Only the Makefile is rewritten:
# C:/... is valid for gmake and MSYS sh. configdata.pm keeps /C/... because it
# embeds PERL5LIB verbatim (':' separator) and runs under cygwin perl.
sed -i -e 's#/C/#C:/#g' -e 's#/c/#c:/#g' Makefile
make -j"$JOBS" SHELL="$SHELL8"
echo "build tree: $BDIR"
'@
    [System.IO.File]::WriteAllText($scriptPath, ($posix -replace "`r`n", "`n"), [System.Text.Encoding]::ASCII)
    $env:SHELL = $shShort
    & $GitBash $scriptPath $repoWin $rootWin "$Jobs" $shShort
    if ($LASTEXITCODE -ne 0) { throw "openssl Configure/make failed (exit $LASTEXITCODE)" }
    $ssl = Join-Path $bdir "libssl.a"
    $crypto = Join-Path $bdir "libcrypto.a"
    if (-not (Test-Path -LiteralPath $ssl) -or -not (Test-Path -LiteralPath $crypto)) {
        throw "openssl archives were not produced in $bdir"
    }
    $destDir = Join-Path $OUT "openssl\lib\windows-arm64"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $ssl -Destination (Join-Path $destDir "libssl.a") -Force
    Copy-Item -LiteralPath $crypto -Destination (Join-Path $destDir "libcrypto.a") -Force
    Write-ArchiveMeta "openssl" @((Join-Path $destDir "libssl.a"), (Join-Path $destDir "libcrypto.a")) "67b5686b4419b4cb8caa502711c41815f5279751"
}

$LIBRARIES = [ordered]@{
    "xxhash"    = { Build-XXHash }
    "simdutf"   = { Build-Simdutf }
    "pcre2"     = { Build-Pcre2 }
    "libsodium" = { Build-Libsodium }
    "libuv"     = { Build-LibUV }
    "uWebSockets" = { Build-USockets }
    "openssl"   = { Build-OpenSSL }
}

foreach ($name in $LIBRARIES.Keys) {
    if ($Only.Count -gt 0 -and ($Only -notcontains $name)) { continue }
    Info "building $name (windows-arm64)"
    & $LIBRARIES[$name]
}
Info "done"
