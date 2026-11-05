#!/usr/bin/env bash
# Build the Linux xxHash static archive (libxxhash.a) for cp3.
#
# Single translation unit: include/xxhash.c (which is
# "#define XXH_IMPLEMENTATION" + #include "xxhash.h" at the pinned v0.8.3
# source) compiled with gcc -O3, archived with ar/ranlib. This mirrors the
# delivered windows-x64 archive (lib/windows-x64/libxxhash.a, built from
# the same vendored xxhash.c) and the libsodium/libuv pattern in
# third_party/build-linux.sh: one archive per Linux architecture, stored
# under lib/linux-<arch>/ alongside a copy of VERSION.txt.
#
# This script is code only on a non-Linux host: it has NOT been run to
# produce the archives in lib/linux-x64/ or lib/linux-arm64/ (those dirs
# currently hold README placeholders). Run it on a native Linux host, or
# on x64 with an aarch64 cross toolchain, before adding the linux platform
# rows to src/std/hash/native.hpp / src/std/collections/native.hpp.
# See docs/design/linux-packaging.md for the full recipe, the proposed
# MANIFEST.md rows, and the wiring lines.
#
# Usage:
#   third_party/xxhash/build-linux.sh [--arch x64|arm64] [--out-dir DIR]
#
# Environment overrides: CC, AR, RANLIB (defaults below per arch).
# For arm64 from x64, install e.g. gcc-aarch64-linux-gnu first.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# ROOT is third_party/; the repo root is its parent.
REPO=$(dirname -- "$ROOT")
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
esac
OUT="$ROOT/xxhash/lib/linux-$ARCH"

usage() {
    cat <<'EOF'
Usage: third_party/xxhash/build-linux.sh [--arch x64|arm64] [--out-dir DIR]

Compiles the vendored single TU (third_party/xxhash/include/xxhash.c)
with gcc -O3 and archives it as libxxhash.a under
third_party/xxhash/lib/linux-<arch>/, copying VERSION.txt alongside it.
For arm64 from x64, install an aarch64-linux-gnu cross toolchain first.
You may override CC, AR, and RANLIB in the environment.
EOF
}

while (($#)); do
    case "$1" in
        --arch) ARCH=$2; shift 2 ;;
        --out-dir) OUT=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build-linux.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done
case "$ARCH" in x64|arm64) ;; *) echo "unsupported Linux arch '$ARCH'" >&2; exit 2 ;; esac

if [[ "$ARCH" == arm64 ]]; then
    CC=${CC:-/usr/bin/aarch64-linux-gnu-gcc}
    AR=${AR:-/usr/bin/aarch64-linux-gnu-ar}
    RANLIB=${RANLIB:-/usr/bin/aarch64-linux-gnu-ranlib}
else
    CC=${CC:-/usr/bin/gcc}
    AR=${AR:-/usr/bin/ar}
    RANLIB=${RANLIB:-/usr/bin/ranlib}
fi

# WSL can import Windows environment variables such as AR=/mnt/c/.../ar.
# Reject Windows-mounted tools before compiling (same guard as
# third_party/build-linux.sh).
for variable in CC AR RANLIB; do
    value=${!variable}
    if [[ "$value" == /mnt/[a-zA-Z]/* ]]; then
        case "$ARCH:$variable" in
            arm64:CC) CC=/usr/bin/aarch64-linux-gnu-gcc ;;
            arm64:AR) AR=/usr/bin/aarch64-linux-gnu-ar ;;
            arm64:RANLIB) RANLIB=/usr/bin/aarch64-linux-gnu-ranlib ;;
            x64:CC) CC=/usr/bin/gcc ;;
            x64:AR) AR=/usr/bin/ar ;;
            x64:RANLIB) RANLIB=/usr/bin/ranlib ;;
        esac
    fi
done

for tool in "$CC" "$AR" "$RANLIB"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-linux.sh: required tool '$tool' is missing" >&2
        exit 1
    }
done

SRC="$ROOT/xxhash/include/xxhash.c"
[[ -f "$SRC" ]] || {
    echo "build-linux.sh: vendored source not found: $SRC" >&2
    echo "build-linux.sh: clone the pin in third_party/xxhash/VERSION.txt" >&2
    echo "build-linux.sh: (https://github.com/Cyan4973/xxHash.git v0.8.3" >&2
    echo "build-linux.sh:  e626a72bc2321cd320e953a0ccf1584cad60f363)" >&2
    exit 1
}

# Deliberately no -march=native / -mcpu=native (would bake the build
# host's CPU into the archive and break cross builds), no -fPIC (same
# as the libsodium/libuv Linux archives: static, linked into executables),
# no -mavx/-msse flags (XXH3 SIMD variants are selected by xxhash.h from
# the target baseline; the generic x86-64 / arm64 baselines are correct).
WORK=$(mktemp -d "${TMPDIR:-/tmp}/xxhash-linux-$ARCH.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

echo "[xxhash] $ARCH: $CC -O3 -c include/xxhash.c"
"$CC" -O3 -std=c99 -Wall -c "$SRC" -o "$WORK/xxhash.o"
echo "[xxhash] $ARCH: $AR rcs libxxhash.a xxhash.o; $RANLIB libxxhash.a"
"$AR" rcs "$WORK/libxxhash.a" "$WORK/xxhash.o"
"$RANLIB" "$WORK/libxxhash.a"

mkdir -p "$OUT"
cp "$WORK/libxxhash.a" "$OUT/libxxhash.a"
cp "$ROOT/xxhash/VERSION.txt" "$OUT/VERSION.txt"
echo "Linux $ARCH xxHash archive written: $OUT/libxxhash.a"
echo "Next: record its bytes + SHA-256 in third_party/MANIFEST.md and"
echo "docs/design/linux-packaging.md, then land the native.hpp rows."
