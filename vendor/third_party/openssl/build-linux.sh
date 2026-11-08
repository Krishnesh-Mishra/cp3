#!/usr/bin/env bash
# Build the Linux OpenSSL static archives for cp3 (libssl.a, libcrypto.a).
#
# Pinned upstream: openssl 3.5.5, tag openssl-3.5.5, commit
#   67b5686b4419b4cb8caa502711c41815f5279751
# (vendor/third_party/openssl/VERSION.txt - one pinned source for every
# platform; headers are platform-independent and already vendored once).
#
# Run on a native Linux host of the requested architecture, or on x64 with an
# aarch64-linux-gnu cross toolchain (gcc-aarch64-linux-gnu) via --arch arm64.
# Output: vendor/third_party/openssl/lib/linux-<arch>/{libssl.a,libcrypto.a}
# plus a VERSION.txt next to them, mirroring the libsodium/libuv pattern.
#
# Usage:
#   vendor/third_party/openssl/build-linux.sh [--arch x64|arm64]
#       [--work-dir DIR] [--out-dir DIR] [--jobs N]
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
esac
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')}
OUT="$ROOT/vendor/third_party/openssl"
TAG=openssl-3.5.5
SHA=67b5686b4419b4cb8caa502711c41815f5279751

usage() {
    cat <<'EOF'
Usage: vendor/third_party/openssl/build-linux.sh [--arch x64|arm64]
                                                  [--work-dir DIR]
                                                  [--out-dir DIR]
                                                  [--jobs N]

Builds pinned OpenSSL 3.5.5 static archives for Linux <arch> from source,
producing vendor/third_party/openssl/lib/linux-<arch>/{libssl.a,libcrypto.a}
and a VERSION.txt with the pin + build facts. For arm64 from x64, pass
--arch arm64 (needs gcc-aarch64-linux-gnu).
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
WORK="$ROOT/build/scratch/openssl-linux-$ARCH"

if [[ "$ARCH" == arm64 ]]; then
    TARGET=linux-aarch64
    PREFIX=aarch64-linux-gnu-
    HOST_TRIPLE=aarch64-linux-gnu
else
    TARGET=linux-x86_64
    PREFIX=
    HOST_TRIPLE=x86_64-linux-gnu
fi

# WSL can import Windows environment variables such as CC=/mnt/c/.../gcc.
for variable in CC CXX AR RANLIB; do
    value=${!variable:-}
    if [[ "$value" == /mnt/[a-zA-Z]/* ]]; then
        unset "$variable"
    fi
done

for tool in git perl make; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-linux.sh: required tool '$tool' is missing" >&2
        exit 1
    }
done
if [[ -n "$PREFIX" ]]; then
    command -v "${PREFIX}gcc" >/dev/null 2>&1 || {
        echo "build-linux.sh: '${PREFIX}gcc' is missing (install gcc-aarch64-linux-gnu)" >&2
        exit 1
    }
fi

SRC="$WORK/src"
BUILD="$WORK/build"
DEST="$OUT/lib/linux-$ARCH"
mkdir -p "$SRC" "$BUILD" "$DEST"

if [[ ! -d "$SRC/openssl/.git" ]]; then
    git clone --depth 1 --branch "$TAG" https://github.com/openssl/openssl.git "$SRC/openssl"
fi
git -C "$SRC/openssl" checkout --detach "$SHA"
[[ "$(git -C "$SRC/openssl" rev-parse HEAD)" == "$SHA" ]] || {
    echo "build-linux.sh: openssl checkout does not match pinned SHA $SHA" >&2
    exit 1
}

echo "[openssl $ARCH] ./Configure $TARGET no-shared no-tests no-apps -O3"
rm -rf "$BUILD"
mkdir -p "$BUILD"
(
    cd "$BUILD"
    "$SRC/openssl/Configure" "$TARGET" no-shared no-tests no-apps -O3 \
        ${PREFIX:+--cross-compile-prefix="$PREFIX"}
    make -j"$JOBS"
)

cp "$BUILD/libssl.a" "$DEST/libssl.a"
cp "$BUILD/libcrypto.a" "$DEST/libcrypto.a"
printf '%s %s %s\n' \
    "openssl=$SHA" \
    "arch=$ARCH" "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$DEST/VERSION.txt"

echo "OpenSSL $ARCH archives written to $DEST"
echo "Next: record bytes + SHA-256 in vendor/third_party/MANIFEST.md and"
echo "vendor/third_party/openssl/VERSION.txt"
