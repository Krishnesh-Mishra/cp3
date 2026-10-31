#!/usr/bin/env bash
# =============================================================================
# tools/build.sh - build cp3c on Linux and stage a relocatable distribution.
#
# Mirrors tools/build.ps1 for Windows:
#   0. syncs version-bearing files from version.txt;
#   1. configures build/ (or --build-dir);
#   2. builds bin/cp3c;
#   3. runs packaging/linux/package.sh, producing
#      out/linux-<arch>/cp3, install.sh and cp3-<version>-linux-<arch>.tar.gz.
#
# Usage:
#   ./tools/build.sh
#   ./tools/build.sh --config Debug --no-package
#   ./tools/build.sh --clean
#   ./tools/build.sh --out-dir out/linux-x64
# =============================================================================
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="$ROOT/build"
CONFIG=Release
GENERATOR=""
CLEAN=0
NO_PACKAGE=0
JOBS=""
OUT_DIR=""
SKIP_SMOKE=0
NO_VSIX=0

usage() {
    cat <<'EOF'
Usage: tools/build.sh [options]

  --build-dir <dir>   build tree (default: build)
  --out-dir <dir>     package output (default: out/linux-<arch>)
  --config <type>     Debug, Release or RelWithDebInfo (default: Release)
  --generator <name>  CMake generator (default: Ninja when available)
  --jobs <n>          parallel build jobs (default: CPU count)
  --clean             remove the build tree before configuring
  --no-package        build only; skip packaging
  --skip-smoke        skip the staged hello-world smoke test
  --no-vsix           do not stage the VS Code extension
  -h, --help          show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)  BUILD_DIR=$2; shift 2 ;;
        --out-dir)    OUT_DIR=$2; shift 2 ;;
        --config)     CONFIG=$2; shift 2 ;;
        --generator)  GENERATOR=$2; shift 2 ;;
        --jobs)       JOBS=$2; shift 2 ;;
        --clean)      CLEAN=1; shift ;;
        --no-package) NO_PACKAGE=1; shift ;;
        --skip-smoke) SKIP_SMOKE=1; shift ;;
        --no-vsix)    NO_VSIX=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "build.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

case "$(uname -s)" in
    Linux) ;;
    *)
        echo "build.sh: this script packages Linux; use tools/build.ps1 on Windows." >&2
        exit 1
        ;;
esac

fail() { echo "build.sh: $*" >&2; exit 1; }
info() { echo "build: $*"; }

command -v cmake >/dev/null 2>&1 || fail "cmake was not found on PATH (install CMake 3.20+)."
if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR=Ninja
    else
        GENERATOR="Unix Makefiles"
    fi
fi
if [ "$GENERATOR" = "Ninja" ] && ! command -v ninja >/dev/null 2>&1; then
    fail "the Ninja generator needs ninja on PATH."
fi
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    fail "no C++ compiler found on PATH (install g++ or clang++)."
fi

info "syncing version from version.txt"
"$ROOT/tools/update-version.sh"

case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac

if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    if [ "$BUILD_DIR" = "$ROOT/build" ]; then
        info "cleaning compiler artifacts in $BUILD_DIR (preserving build/scratch)"
        find "$BUILD_DIR" -mindepth 1 -maxdepth 1 ! -name scratch -exec rm -rf {} +
    else
        info "cleaning build tree $BUILD_DIR"
        rm -rf "$BUILD_DIR"
    fi
fi

if [ -z "$JOBS" ]; then
    JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
fi

info "configuring $GENERATOR / $CONFIG -> $BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG"

info "building with $JOBS parallel jobs"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

CP3C="$BUILD_DIR/bin/cp3c"
[ -x "$CP3C" ] || fail "build finished but '$CP3C' is missing"
info "compiler: $CP3C"

if [ "$NO_PACKAGE" -eq 1 ]; then
    info "packaging skipped (--no-package)"
    exit 0
fi

args=(--build-dir "$BUILD_DIR")
if [ -n "$OUT_DIR" ]; then
    args+=(--out-dir "$OUT_DIR")
fi
if [ "$SKIP_SMOKE" -eq 1 ]; then
    args+=(--skip-smoke)
fi
if [ "$NO_VSIX" -eq 1 ]; then
    args+=(--no-vsix)
fi
"$ROOT/packaging/linux/package.sh" "${args[@]}"
