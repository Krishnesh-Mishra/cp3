#!/usr/bin/env bash
# Build the Linux PCRE2 8-bit static archive (libpcre2-8.a) for cp3.
#
# Pinned upstream: pcre2 10.49, tag pcre2-10.49, commit
#   6f9d7c1373262c541324a16a358785b33ef116cf
# (vendor/third_party/pcre2/VERSION.txt - PCRE2_CODE_UNIT_WIDTH=8, static,
# PCRE2_STATIC consumer flag, see vendor/third_party/MANIFEST.md).
#
# Run on a native Linux host of the requested architecture, or on x64 with an
# aarch64-linux-gnu cross toolchain via --arch arm64. Output:
# vendor/third_party/pcre2/lib/linux-<arch>/libpcre2-8.a (+ VERSION.txt).
#
# Usage:
#   vendor/third_party/pcre2/build-linux.sh [--arch x64|arm64]
#       [--work-dir DIR] [--out-dir DIR] [--jobs N]
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
esac
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')}
OUT="$ROOT/vendor/third_party/pcre2"
TAG=pcre2-10.49
SHA=6f9d7c1373262c541324a16a358785b33ef116cf

usage() {
    cat <<'EOF'
Usage: vendor/third_party/pcre2/build-linux.sh [--arch x64|arm64]
                                                [--work-dir DIR]
                                                [--out-dir DIR]
                                                [--jobs N]

Builds pinned PCRE2 10.49 static 8-bit archive for Linux <arch> from source,
producing vendor/third_party/pcre2/lib/linux-<arch>/libpcre2-8.a and a
VERSION.txt with the pin + build facts. For arm64 from x64, use --arch arm64.
EOF
}

while (($#)); do
    case "$1" in
        --arch) ARCH=$2; shift 2 ;;
        --work-dir) WORK=$2; shift 2 ;;
        --out-dir) OUT=$2; shift 2 ;;
        --jobs) JOBS=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build-linux.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done
case "$ARCH" in x64|arm64) ;; *) echo "unsupported Linux arch '$ARCH'" >&2; exit 2 ;; esac

# Computed after --arch parsing: the work dir follows the requested arch.
WORK="$ROOT/build/scratch/pcre2-linux-$ARCH"

if [[ "$ARCH" == arm64 ]]; then
    HOST_TRIPLE=aarch64-linux-gnu
    TOOLCHAIN="$ROOT/vendor/tools/cmake/linux-aarch64-toolchain.cmake"
else
    HOST_TRIPLE=x86_64-linux-gnu
    TOOLCHAIN=""
fi

for tool in git cmake make; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-linux.sh: required tool '$tool' is missing" >&2
        exit 1
    }
done

SRC="$WORK/src"
BUILD="$WORK/build"
DEST="$OUT/lib/linux-$ARCH"
mkdir -p "$SRC" "$BUILD" "$DEST"

if [[ ! -d "$SRC/pcre2/.git" ]]; then
    git clone --depth 1 --branch "$TAG" https://github.com/PCRE2Project/pcre2.git "$SRC/pcre2"
fi
git -C "$SRC/pcre2" checkout --detach "$SHA"
[[ "$(git -C "$SRC/pcre2" rev-parse HEAD)" == "$SHA" ]] || {
    echo "build-linux.sh: pcre2 checkout does not match pinned SHA $SHA" >&2
    exit 1
}

echo "[pcre2 $ARCH] cmake (${HOST_TRIPLE})"
CMAKE_ARGS=(-S "$SRC/pcre2" -B "$BUILD" -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DPCRE2_BUILD_PCRE2_8=ON
            -DPCRE2_BUILD_PCRE2_16=OFF
            -DPCRE2_BUILD_PCRE2_32=OFF
            -DPCRE2_BUILD_TESTS=OFF
            -DPCRE2_BUILD_PCRE2GREP=OFF
            -DPCRE2_DEBUG=OFF)
if [[ -n "$TOOLCHAIN" ]]; then
    CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN")
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" --parallel "$JOBS"

ARCHIVE=$(find "$BUILD" -maxdepth 3 -name 'libpcre2-8.a' -print -quit)
[[ -n "$ARCHIVE" && -f "$ARCHIVE" ]] || {
    echo "build-linux.sh: libpcre2-8.a was not produced in $BUILD" >&2
    exit 1
}
cp "$ARCHIVE" "$DEST/libpcre2-8.a"
printf '%s %s %s\n' \
    "pcre2=$SHA" \
    "arch=$ARCH" "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$DEST/VERSION.txt"

echo "PCRE2 $ARCH archive written to $DEST/libpcre2-8.a"
