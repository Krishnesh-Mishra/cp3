#!/usr/bin/env sh
# =============================================================================
# cp3.sh - the user-facing `cp3` command shipped as <install>/bin/cp3.
#
# A thin shim around bin/cp3c. It exports CP3_ROOT so the compiler finds the
# shipped include/, src/ and vendor/third_party/ trees even when this file is reached
# through PATH. Only `cp3 doctor` is owned here; every other command -
# including `cp3 init` - is forwarded verbatim to bin/cp3c with the exit code
# preserved.
# =============================================================================
set -eu

CP3_BIN=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CP3_ROOT=$(CDPATH= cd -- "$CP3_BIN/.." && pwd)
CP3C="$CP3_BIN/cp3c"
export CP3_ROOT

if [ "$#" -eq 0 ]; then
    exec "$CP3C"
fi

case "$1" in
    doctor)
        shift
        echo "cp3 doctor"
        echo "  install root : $CP3_ROOT"
        echo "  shim         : $CP3_BIN/cp3"
        if [ -x "$CP3C" ]; then
            version=$("$CP3C" --version 2>/dev/null | head -n 1 || true)
            echo "  cp3c         : $CP3C"
            echo "  cp3c version : ${version:-unknown}"
        else
            echo "  cp3c         : MISSING ($CP3C)"
        fi
        # Ask cp3c itself: it owns the resolution order (--toolchain >
        # CP3_TOOLCHAIN > project package.json > global toolchain.conf >
        # auto-detect). Fall back to PATH discovery only when cp3c is
        # missing or cannot answer (older cp3c without `toolchain current`).
        tc=""
        if [ -x "$CP3C" ]; then
            tc=$("$CP3C" toolchain current 2>/dev/null || true)
        fi
        if [ -z "$tc" ]; then
            if command -v clang++ >/dev/null 2>&1; then
                tc="Clang ($(command -v clang++))"
            else
                tc="(none found - run 'cp3 toolchain' to select one, or put clang++ on PATH)"
            fi
        fi
        echo "  toolchain    : $tc"
        if command -v ccache >/dev/null 2>&1; then
            echo "  ccache       : $(command -v ccache)"
        else
            echo "  ccache       : not found (optional)"
        fi
        if command -v code >/dev/null 2>&1; then
            echo "  VS Code CLI  : $(command -v code)"
        else
            echo "  VS Code CLI  : not found (optional)"
        fi
        if [ -d "$CP3_ROOT/include" ]; then inc=yes; else inc=no; fi
        if [ -d "$CP3_ROOT/src" ]; then src=yes; else src=no; fi
        if [ -d "$CP3_ROOT/vendor/third_party" ]; then tp=yes; else tp=no; fi
        echo "  shipped trees: include=$inc src=$src vendor_third_party=$tp"
        echo "  note         : Clang is required to build cp3 programs. If clang++ is"
        echo "                  not on PATH, select one with: cp3 toolchain"
        echo "                  VS Code is optional."
        if [ -x "$CP3C" ]; then exit 0; fi
        exit 1
        ;;
    *)
        exec "$CP3C" "$@"
        ;;
esac
