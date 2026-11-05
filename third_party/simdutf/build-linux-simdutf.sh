#!/usr/bin/env bash
# Build the simdutf static archive for Linux x64 or arm64.
#
# simdutf is built from its single amalgamated translation unit
# (src/simdutf.cpp, upstream tag v6.1.0) with plain g++ -O3 — no CMake,
# no separate per-arch SIMD files. The headers are already vendored once,
# shared across every triple, at third_party/simdutf/include/; this
# script only produces the per-arch archive + VERSION.txt.
#
# Run on a native Linux host of the requested architecture, or supply a
# matching cross toolchain through CXX/AR/RANLIB and use --arch arm64
# from x64 (requires aarch64-linux-gnu-g++).
#
# Usage:
#   third_party/simdutf/build-linux-simdutf.sh [--arch x64|arm64]
#       [--work-dir DIR] [--out-dir DIR]
#
# Output: third_party/simdutf/lib/linux-<arch>/libsimdutf.a (+ VERSION.txt)
#
# This script was NOT run for this packaging change (no Linux toolchain
# on the authoring host); see docs/design/linux-packaging-simdutf.md.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
esac
WORK="$ROOT/build/scratch/vendor-linux-simdutf-$ARCH"
OUT="$ROOT/third_party"

usage() {
    cat <<'EOF'
Usage: third_party/simdutf/build-linux-simdutf.sh [--arch x64|arm64]
                                                  [--work-dir DIR]
                                                  [--out-dir DIR]

Builds the pinned simdutf v6.1.0 static archive from the single
translation unit src/simdutf.cpp (g++ -O3, -I include -I src) and
installs it to third_party/simdutf/lib/linux-<arch>/libsimdutf.a.
For arm64 from x64, install an aarch64-linux-gnu cross toolchain first.
You may override CXX, AR, and RANLIB in the environment.
EOF
}

while (($#)); do
    case "$1" in
        --arch) ARCH=$2; shift 2 ;;
        --work-dir) WORK=$2; shift 2 ;;
        --out-dir) OUT=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build-linux-simdutf.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done
case "$ARCH" in x64|arm64) ;; *) echo "unsupported Linux arch '$ARCH'" >&2; exit 2 ;; esac

if [[ "$ARCH" == arm64 ]]; then
    CXX=${CXX:-/usr/bin/aarch64-linux-gnu-g++}
    AR=${AR:-/usr/bin/aarch64-linux-gnu-ar}
    RANLIB=${RANLIB:-/usr/bin/aarch64-linux-gnu-ranlib}
else
    CXX=${CXX:-/usr/bin/g++}
    AR=${AR:-/usr/bin/ar}
    RANLIB=${RANLIB:-/usr/bin/ranlib}
fi

# WSL can import Windows environment variables such as AR=/mnt/c/.../ar,
# which would write COFF objects. Reject Windows-mounted tools, mirroring
# third_party/build-linux.sh.
for variable in CXX AR RANLIB; do
    value=${!variable}
    if [[ "$value" == /mnt/[a-zA-Z]/* ]]; then
        case "$ARCH:$variable" in
            arm64:CXX) CXX=/usr/bin/aarch64-linux-gnu-g++ ;;
            arm64:AR) AR=/usr/bin/aarch64-linux-gnu-ar ;;
            arm64:RANLIB) RANLIB=/usr/bin/aarch64-linux-gnu-ranlib ;;
            x64:CXX) CXX=/usr/bin/g++ ;;
            x64:AR) AR=/usr/bin/ar ;;
            x64:RANLIB) RANLIB=/usr/bin/ranlib ;;
        esac
    fi
done

for tool in git "$CXX" "$AR"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-linux-simdutf.sh: required tool '$tool' is missing" >&2
        exit 1
    }
done

SRC="$WORK/src"
BUILD="$WORK/build"
DEST="$OUT/simdutf/lib/linux-$ARCH"

# Pinned upstream (third_party/simdutf/VERSION.txt, third_party/build.ps1
# $pins, third_party/MANIFEST.md "Ledger F"). Checkout is verified by SHA.
clone_pin() {
    local name=$1 url=$2 tag=$3 sha=$4 dir="$SRC/$1"
    if [[ ! -d "$dir/.git" ]]; then
        git clone --depth 1 --branch "$tag" "$url" "$dir"
    fi
    git -C "$dir" checkout --detach "$sha"
    [[ "$(git -C "$dir" rev-parse HEAD)" == "$sha" ]] || {
        echo "build-linux-simdutf.sh: $name checkout does not match pinned SHA $sha" >&2
        exit 1
    }
}

mkdir -p "$SRC" "$BUILD" "$DEST"
clone_pin simdutf https://github.com/simdutf/simdutf.git v6.1.0 7852150e454ebacbf7e65cbf2c5e3feac5e660ec

# Sanity: the amalgamated TU and the include tree must be where the
# recipe expects them.
[[ -f "$SRC/simdutf/src/simdutf.cpp" ]] || {
    echo "build-linux-simdutf.sh: missing src/simdutf.cpp in $SRC/simdutf" >&2
    exit 1
}
[[ -f "$SRC/simdutf/include/simdutf.h" ]] || {
    echo "build-linux-simdutf.sh: missing include/simdutf.h in $SRC/simdutf" >&2
    exit 1
}

echo "[simdutf $ARCH] g++ -O3 single-TU build"
# Single translation unit. Include order matters: the public headers
# first (-I include), then the source tree (-I src) for the internal
# headers the TU pulls in. No -march/-mtune: runtime CPU detection in
# simdutf picks the SIMD kernel, so the baseline archive stays
# portable across all x64 / arm64 hosts. -std=c++17 is the upstream
# minimum for v6.1.0.
"$CXX" -O3 -std=c++17 \
    -I "$SRC/simdutf/include" -I "$SRC/simdutf/src" \
    -c "$SRC/simdutf/src/simdutf.cpp" -o "$BUILD/simdutf.o"

"$AR" rcs "$BUILD/libsimdutf.a" "$BUILD/simdutf.o"
"$RANLIB" "$BUILD/libsimdutf.a"

cp "$BUILD/libsimdutf.a" "$DEST/libsimdutf.a"
# Same key=value shape as the other Linux vendor VERSION.txt files
# (libsodium/libuv/uWebSockets under lib/linux-*/).
printf '%s %s %s\n' \
    'simdutf=7852150e454ebacbf7e65cbf2c5e3feac5e660ec' \
    "arch=$ARCH" "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$DEST/VERSION.txt"

rm -f "$DEST/README.md"
echo "Linux $ARCH simdutf archive written to $DEST/libsimdutf.a"
