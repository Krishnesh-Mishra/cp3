#!/usr/bin/env bash
# Build CP3's std/crypto and std/network archives for Linux x64 or arm64.
# Run on a native Linux host of the requested architecture, or supply a
# matching cross toolchain through CC/CXX/AR and use --arch arm64 on x64.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
esac
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')}
WORK="$ROOT/build/scratch/vendor-linux-$ARCH"
OUT="$ROOT/third_party"

usage() {
    cat <<'EOF'
Usage: vendor/third_party/build-linux.sh [--arch x64|arm64] [--work-dir DIR]
                                  [--out-dir DIR]

Builds pinned static libsodium, libuv, and uSockets archives. For arm64 from
x64, install an aarch64-linux-gnu cross toolchain first. You may override CC,
CXX, AR, and RANLIB in the environment. Output is stored under each vendor
package at vendor/third_party/<vendor>/lib/linux-<arch>/.
EOF
}

WITH_OPENSSL=0
while (($#)); do
    case "$1" in
        --arch) ARCH=$2; shift 2 ;;
        --work-dir) WORK=$2; shift 2 ;;
        --out-dir) OUT=$2; shift 2 ;;
        --with-openssl) WITH_OPENSSL=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build-linux.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done
case "$ARCH" in x64|arm64) ;; *) echo "unsupported Linux arch '$ARCH'" >&2; exit 2 ;; esac

if [[ "$ARCH" == arm64 ]]; then
    CC=${CC:-/usr/bin/aarch64-linux-gnu-gcc}
    CXX=${CXX:-/usr/bin/aarch64-linux-gnu-g++}
    AR=${AR:-/usr/bin/aarch64-linux-gnu-ar}
    RANLIB=${RANLIB:-/usr/bin/aarch64-linux-gnu-ranlib}
    HOST_TRIPLE=aarch64-linux-gnu
    CMAKE_PROCESSOR=aarch64
else
    CC=${CC:-/usr/bin/gcc}
    CXX=${CXX:-/usr/bin/g++}
    AR=${AR:-/usr/bin/ar}
    RANLIB=${RANLIB:-/usr/bin/ranlib}
    HOST_TRIPLE=x86_64-linux-gnu
    CMAKE_PROCESSOR=x86_64
fi

# WSL can import Windows environment variables such as AR=/mnt/c/.../ar.
# CMake caches the first AR it sees, so reject Windows-mounted tools before
# configuring either vendor project.
for variable in CC CXX AR RANLIB; do
    value=${!variable}
    if [[ "$value" == /mnt/[a-zA-Z]/* ]]; then
        case "$ARCH:$variable" in
            arm64:CC) CC=/usr/bin/aarch64-linux-gnu-gcc ;;
            arm64:CXX) CXX=/usr/bin/aarch64-linux-gnu-g++ ;;
            arm64:AR) AR=/usr/bin/aarch64-linux-gnu-ar ;;
            arm64:RANLIB) RANLIB=/usr/bin/aarch64-linux-gnu-ranlib ;;
            x64:CC) CC=/usr/bin/gcc ;;
            x64:CXX) CXX=/usr/bin/g++ ;;
            x64:AR) AR=/usr/bin/ar ;;
            x64:RANLIB) RANLIB=/usr/bin/ranlib ;;
        esac
    fi
done

for tool in git cmake make "$CC" "$CXX" "$AR"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-linux.sh: required tool '$tool' is missing" >&2
        exit 1
    }
done

SRC="$WORK/src"
BUILD="$WORK/build"
PREFIX="$WORK/install"
mkdir -p "$SRC" "$BUILD" "$PREFIX" "$OUT"

clone_pin() {
    local name=$1 url=$2 tag=$3 sha=$4 dir="$SRC/$1"
    if [[ ! -d "$dir/.git" ]]; then
        git clone --depth 1 --branch "$tag" "$url" "$dir"
    fi
    git -C "$dir" checkout --detach "$sha"
    [[ "$(git -C "$dir" rev-parse HEAD)" == "$sha" ]] || {
        echo "build-linux.sh: $name checkout does not match pinned SHA $sha" >&2
        exit 1
    }
}

clone_pin libsodium https://github.com/jedisct1/libsodium.git 1.0.22-RELEASE 77e1ce5d6dee871c49ef211222ba18ef0c486bda
clone_pin libuv https://github.com/libuv/libuv.git v1.53.0 840404ce8ba7cc0204be52389a6cfff9f2c90fb6
clone_pin uWebSockets https://github.com/uNetworking/uWebSockets.git v20.80.0 3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5
clone_pin uWebSockets https://github.com/uNetworking/uWebSockets.git v20.80.0 3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5
git -C "$SRC/uWebSockets" submodule update --init --depth 1 uSockets
git -C "$SRC/uWebSockets/uSockets" checkout --detach 86097c490263ab662d62e8e7b541390bdec7d149
[[ "$(git -C "$SRC/uWebSockets/uSockets" rev-parse HEAD)" == "86097c490263ab662d62e8e7b541390bdec7d149" ]] || {
    echo "build-linux.sh: uSockets checkout does not match pinned SHA 86097c490263ab662d62e8e7b541390bdec7d149" >&2
    exit 1
}

echo "[1/3] libsodium ($ARCH)"
mkdir -p "$BUILD/libsodium"
if [[ ! -f "$PREFIX/libsodium/lib/libsodium.a" ]]; then
(
    cd "$BUILD/libsodium"
    CC="$CC" AR="$AR" RANLIB="$RANLIB" \
        "../../src/libsodium/configure" --host="$HOST_TRIPLE" \
        --disable-shared --enable-static --disable-dependency-tracking \
        --prefix="$PREFIX/libsodium" CFLAGS='-O3'
    make -j"$JOBS"
    make install
)
else
    echo "  using cached Linux $ARCH libsodium archive"
fi

echo "[2/3] libuv ($ARCH)"
if [[ ! -f "$PREFIX/libuv/lib/libuv.a" ]]; then
rm -rf "$BUILD/libuv"
cmake -S "$SRC/libuv" -B "$BUILD/libuv" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR="$CMAKE_PROCESSOR" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_AR="$(command -v "$AR")" -DCMAKE_RANLIB="$(command -v "$RANLIB")" \
    -DLIBUV_BUILD_SHARED=OFF -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX/libuv"
cmake --build "$BUILD/libuv" --parallel "$JOBS"
cmake --install "$BUILD/libuv"
else
    echo "  using cached Linux $ARCH libuv archive"
fi

echo "[3/3] uSockets ($ARCH)"
if [[ "$WITH_OPENSSL" == 1 ]]; then
    OPENSSL_ARGS=(WITH_OPENSSL=1)
    OPENSSL_CFLAGS="-I$ROOT/vendor/third_party/openssl/include"
    if [[ "$ARCH" == arm64 ]]; then
        OPENSSL_CFLAGS="$OPENSSL_CFLAGS -I$ROOT/vendor/third_party/openssl/include"
    fi
    make -C "$SRC/uWebSockets/uSockets" \
        CC="$CC" CXX="$CXX" AR="$AR" WITH_LIBUV=1 WITH_LTO=0 WITH_OPENSSL=1 \
        CFLAGS="-I$PREFIX/libuv/include -I$ROOT/vendor/third_party/openssl/include" -j"$JOBS"
else
    make -C "$SRC/uWebSockets/uSockets" \
        CC="$CC" AR="$AR" WITH_LIBUV=1 WITH_LTO=0 \
        CFLAGS="-I$PREFIX/libuv/include" -j"$JOBS"
fi

mkdir -p "$OUT/libsodium/lib/linux-$ARCH" "$OUT/libuv/lib/linux-$ARCH" \
    "$OUT/uWebSockets/lib/linux-$ARCH"
cp "$PREFIX/libsodium/lib/libsodium.a" "$OUT/libsodium/lib/linux-$ARCH/libsodium.a"
cp "$PREFIX/libuv/lib/libuv.a" "$OUT/libuv/lib/linux-$ARCH/libuv.a"
cp "$SRC/uWebSockets/uSockets/uSockets.a" "$OUT/uWebSockets/lib/linux-$ARCH/libuSockets.a"
metadata="$WORK/VERSION-linux-$ARCH.txt"
printf '%s %s %s %s %s\n' \
    'libsodium=77e1ce5d6dee871c49ef211222ba18ef0c486bda' \
    'libuv=840404ce8ba7cc0204be52389a6cfff9f2c90fb6' \
    'uWebSockets=3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5' \
    "arch=$ARCH" "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$metadata"
for vendor in libsodium libuv uWebSockets; do
    cp "$metadata" "$OUT/$vendor/lib/linux-$ARCH/VERSION.txt"
done
echo "Linux $ARCH archives written under vendor/third_party/<vendor>/lib/linux-$ARCH/"
