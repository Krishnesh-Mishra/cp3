#!/usr/bin/env sh
# =============================================================================
# cp3.sh - the user-facing `cp3` command shipped as <install>/bin/cp3.
#
# A thin shim around bin/cp3c. It exports CP3_ROOT so the compiler finds the
# shipped include/, src/ and third_party/ trees even when this file is reached
# through PATH. `cp3 init` and `cp3 doctor` are owned here (cp3c does not
# implement them); every other command is forwarded verbatim with the exit code
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
    init)
        shift
        target=""
        force=0
        while [ "$#" -gt 0 ]; do
            case "$1" in
                --force|-f)
                    force=1
                    shift
                    ;;
                -*)
                    echo "cp3: usage: cp3 init [target] [--force]" >&2
                    exit 2
                    ;;
                *)
                    if [ -n "$target" ]; then
                        echo "cp3: usage: cp3 init [target] [--force]" >&2
                        exit 2
                    fi
                    target=$1
                    shift
                    ;;
            esac
        done
        if [ -z "$target" ]; then target=$PWD; fi
        mkdir -p "$target" || exit 1
        target=$(CDPATH= cd -- "$target" && pwd) || exit 1
        if [ -n "$(ls -A "$target" 2>/dev/null)" ] && [ "$force" -ne 1 ]; then
            echo "cp3: target \"$target\" is not empty; refusing to scaffold over it." >&2
            echo "      Re-run with --force to scaffold into it anyway." >&2
            exit 2
        fi
        name=$(basename -- "$target")
        if [ -z "$name" ]; then name=cp3-app; fi
        json_name=$(printf '%s' "$name" | tr -d '"\\')
        mkdir -p "$target/src" || exit 1
        {
            printf '{\n'
            printf '  "name": "%s",\n' "$json_name"
            printf '  "version": "0.1.0",\n'
            printf '  "type": "application",\n'
            printf '  "main": "src/main.cp3"\n'
            printf '}\n'
        } > "$target/package.json"
        {
            printf 'import console from "std/console";\n\n'
            printf 'console.log("Hello from %s!");\n' "$name"
        } > "$target/src/main.cp3"
        {
            printf '# %s\n\n' "$name"
            printf 'A cp3 project scaffolded by `cp3 init`.\n\n'
            printf 'Build and run it with:\n\n'
            printf '    cp3 run src/main.cp3\n'
        } > "$target/README.md"
        echo "cp3: initialized project \"$name\" in \"$target\""
        echo "      Run it with:  cd \"$target\" && cp3 run src/main.cp3"
        ;;
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
        if command -v clang++ >/dev/null 2>&1; then
            echo "  toolchain    : $(command -v clang++)"
        else
            echo "  toolchain    : (none found - install Clang/LLVM and put clang++ on PATH)"
        fi
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
        if [ -d "$CP3_ROOT/third_party" ]; then tp=yes; else tp=no; fi
        echo "  shipped trees: include=$inc src=$src third_party=$tp"
        echo "  note         : Clang (clang++) is required to build cp3 programs."
        if [ -x "$CP3C" ]; then exit 0; fi
        exit 1
        ;;
    *)
        exec "$CP3C" "$@"
        ;;
esac
