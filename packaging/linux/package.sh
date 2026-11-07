#!/usr/bin/env bash
# =============================================================================
# packaging/linux/package.sh - stage a relocatable Linux cp3 distribution.
#
# Inputs: a natively built compiler (<build-dir>/bin/cp3c) plus the repository's
# header trees, runtime translation units, vendored libraries and (optionally)
# the VS Code extension. Output:
#
#   <out-dir>/cp3/                         relocatable install tree
#   <out-dir>/install.sh                   per-user installer (no root)
#   <out-dir>/cp3-<version>-linux-<arch>.tar.gz
#   <out-dir>/SHA256SUMS.txt
#
# The default out-dir is <repo>/out/linux-<arch>. Run tools/build.sh to build
# and package in one command.
#
# Usage:
#   packaging/linux/package.sh
#   packaging/linux/package.sh --build-dir build --out-dir out/linux-x64
#   packaging/linux/package.sh --skip-smoke
#
# Third-party archives: the checked-in third_party/*/lib/*.a files are the
# Windows (MinGW/COFF) vendor builds. A Linux package needs Linux-built
# archives; pass --third-party-dir <tree> after rebuilding them on Linux. The
# script warns when it detects COFF archives instead of silently producing a
# package whose std/network or std/crypto rows cannot link.
# =============================================================================
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
VERSION=""
BUILD_DIR="$ROOT/build"
OUT_DIR=""
THIRD_PARTY="$ROOT/third_party"
NO_VSIX=0
SKIP_SMOKE=0
CLEAN=0

ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64) ARCH=x64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    i386|i686) ARCH=x86 ;;
esac

usage() {
    cat <<'EOF'
Usage: packaging/linux/package.sh [options]

  --build-dir <dir>       build tree containing bin/cp3c (default: build)
  --out-dir <dir>         output directory (default: out/linux-<arch>)
  --third-party-dir <dir> vendored trees to stage (default: third_party)
  --version <version>     product version (default: version.txt)
  --arch <arch>           package architecture label (default: uname -m)
  --no-vsix               do not stage extensions/cp3-vscode/dist/*.vsix
  --skip-smoke            do not compile/run a hello-world with the stage
  --clean                 remove the output directory before staging
  -h, --help              show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)       BUILD_DIR=$2; shift 2 ;;
        --out-dir)         OUT_DIR=$2; shift 2 ;;
        --third-party-dir) THIRD_PARTY=$2; shift 2 ;;
        --version)         VERSION=$2; shift 2 ;;
        --arch)            ARCH=$2; shift 2 ;;
        --no-vsix)         NO_VSIX=1; shift ;;
        --skip-smoke)      SKIP_SMOKE=1; shift ;;
        --clean)           CLEAN=1; shift ;;
        -h|--help)         usage; exit 0 ;;
        *) echo "package.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

fail() { echo "package.sh: $*" >&2; exit 1; }
info() { echo "package: $*"; }
warn() { echo "package: warning: $*" >&2; }

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *)  printf '%s\n' "$ROOT/$1" ;;
    esac
}

BUILD_DIR=$(absolute_path "$BUILD_DIR")
THIRD_PARTY=$(absolute_path "$THIRD_PARTY")
if [ -z "$OUT_DIR" ]; then
    OUT_DIR="$ROOT/out/linux-$ARCH"
else
    OUT_DIR=$(absolute_path "$OUT_DIR")
fi

if [ -z "$VERSION" ]; then
    [ -f "$ROOT/version.txt" ] || fail "version.txt is missing at the repository root"
    VERSION=$(tr -d '[:space:]' < "$ROOT/version.txt")
fi
case "$VERSION" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) fail "version '$VERSION' is not <major>.<minor>.<patch>[-pre]" ;;
esac

CP3C="$BUILD_DIR/bin/cp3c"
[ -x "$CP3C" ] || fail "no executable compiler at '$CP3C'; build it first (tools/build.sh)"
[ -d "$THIRD_PARTY" ] || fail "missing third-party tree '$THIRD_PARTY'"

if [ "$CLEAN" -eq 1 ] && [ -d "$OUT_DIR" ]; then
    info "cleaning $OUT_DIR"
    rm -rf "$OUT_DIR"
fi

STAGE="$OUT_DIR/cp3"
if [ -d "$STAGE" ]; then
    info "removing previous stage $STAGE"
    rm -rf "$STAGE"
fi
mkdir -p "$STAGE"/bin "$STAGE"/include "$STAGE"/src "$STAGE"/third_party "$STAGE"/share/vscode

# --- bin ---------------------------------------------------------------------
cp -p "$CP3C" "$STAGE/bin/cp3c"
chmod +x "$STAGE/bin/cp3c"
cp -p "$ROOT/packaging/linux/cp3.sh" "$STAGE/bin/cp3"
chmod +x "$STAGE/bin/cp3"
info "compiler: $("$STAGE/bin/cp3c" --version 2>/dev/null | head -n 1 || echo unknown)"

# --- include: every header tree user programs need ---------------------------
copy_tree() {
    local from=$1
    local to=$2
    local what=$3
    [ -d "$from" ] || fail "$what is missing: '$from'"
    mkdir -p "$to"
    cp -R "$from/." "$to/"
}

copy_tree "$ROOT/src/runtime/include" "$STAGE/include/runtime" "runtime header tree"
copy_tree "$ROOT/src/common/include" "$STAGE/include/common" "common header tree"

for family in "$ROOT"/src/std/*; do
    [ -d "$family" ] || continue
    if [ -d "$family/include" ]; then
        copy_tree "$family/include" "$STAGE/include/std/$(basename "$family")" \
            "std/$(basename "$family") header tree"
    fi
done
info "headers staged under include/"

# --- src: runtime translation units compiled into every program --------------
copy_tree "$ROOT/src/runtime/src" "$STAGE/src/runtime/src" "runtime sources"

third_party_trees=""
family_source_count=0
for family in "$ROOT"/src/std/*; do
    [ -d "$family" ] || continue
    rows=()
    while IFS= read -r -d '' row; do
        rows+=("$row")
    done < <(find "$family" -type f -name native.hpp -print0 | sort -z)
    [ "${#rows[@]}" -gt 0 ] || continue

    for row in "${rows[@]}"; do
        refs=$(grep -oE '"(src|third_party)/[^"]+"' "$row" | tr -d '"' | sort -u || true)
        for ref in $refs; do
            case "$ref" in
                third_party/linux-*)
                    platform=${ref#third_party/}
                    platform=${platform%%/*}
                    [ "$platform" = "linux-$ARCH" ] || continue
                    ;;
                src/*/include)
                    continue
                    ;;
                third_party/*)
                    lib=${ref#third_party/}
                    lib=${lib%%/*}
                    third_party_trees="$third_party_trees $lib"
                    continue
                    ;;
            esac
            source_path="$ROOT/$ref"
            [ -e "$source_path" ] || fail "native row $row references missing '$ref'"
            mkdir -p "$STAGE/$(dirname "$ref")"
            cp -R "$source_path" "$STAGE/$ref"
            family_source_count=$((family_source_count + 1))
        done
    done

    # Support headers next to the copied units (pump.hpp, runtime_loop.hpp,
    # etc.). The include tree is already staged under include/; native.hpp is
    # compiler-side and never shipped.
    while IFS= read -r -d '' header; do
        rel=${header#"$ROOT/"}
        case "$rel" in
            src/std/*/include/*) continue ;;
        esac
        [ "$(basename "$header")" = "native.hpp" ] && continue
        mkdir -p "$STAGE/$(dirname "$rel")"
        cp -p "$header" "$STAGE/$rel"
    done < <(find "$family" -type f \( -name '*.hpp' -o -name '*.h' -o -name '*.inc' \) -print0 | sort -z)
done
info "std runtime units: $family_source_count files"

# Core-language backends live in the CLI tree, not src/std: native_regex.hpp
# selects PCRE2. Its runtime unit (src/runtime/src/core/regex.cpp) is already
# staged with src/runtime/src above; collect the vendored tree it links.
core_row="$ROOT/src/cli/src/detail/native/native_regex.hpp"
[ -f "$core_row" ] || fail "missing core native row '$core_row'"
while IFS= read -r ref; do
    case "$ref" in
        third_party/*)
            lib=${ref#third_party/}
            lib=${lib%%/*}
            third_party_trees="$third_party_trees $lib"
            ;;
    esac
done < <(grep -oE '"third_party/[^"]+"' "$core_row" | tr -d '"' | sort -u)

# --- third_party -------------------------------------------------------------
if [ -z "$third_party_trees" ]; then
    fail "no third_party tree referenced by any native row; the rows were probably not read"
fi
for lib in $(printf '%s\n' $third_party_trees | sort -u); do
    copy_tree "$THIRD_PARTY/$lib" "$STAGE/third_party/$lib" "vendored tree third_party/$lib"
    info "vendored: third_party/$lib"
done

# A Linux release must carry native archives for this exact architecture.
# The root third_party/*.a files are Windows COFF artifacts and are retained
# for Windows builds; Linux variants live in linux-<arch>/.
missing_vendor=0
for archive in \
    "$THIRD_PARTY/libsodium/lib/linux-$ARCH/libsodium.a" \
    "$THIRD_PARTY/libuv/lib/linux-$ARCH/libuv.a" \
    "$THIRD_PARTY/uWebSockets/lib/linux-$ARCH/libuSockets.a" \
    "$THIRD_PARTY/xxhash/lib/linux-$ARCH/libxxhash.a" \
    "$THIRD_PARTY/simdutf/lib/linux-$ARCH/libsimdutf.a" \
    "$THIRD_PARTY/pcre2/lib/linux-$ARCH/libpcre2-8.a"; do
    if [ ! -f "$archive" ]; then
        warn "missing Linux $ARCH native archive: $archive"
        missing_vendor=1
    fi
done
# Drop every other platform's archives (and the Windows COFF archives kept at
# the vendor lib root for Windows builds): the package is for linux-$ARCH only.
for vendor in libsodium libuv uWebSockets xxhash simdutf pcre2; do
    libdir="$STAGE/third_party/$vendor/lib"
    [ -d "$libdir" ] || continue
    find "$libdir" -mindepth 1 -maxdepth 1 -type d \
        ! -name "linux-$ARCH" -exec rm -rf {} +
    find "$libdir" -mindepth 1 -maxdepth 1 -type f -name '*.a' -delete
done
if [ "$missing_vendor" -ne 0 ]; then
    warn "programs using the affected std modules will not link in this package."
    warn "Build the archives with: bash third_party/build-linux.sh --arch $ARCH"
    warn "(PCRE2: see third_party/pcre2/build-linux.sh once added)"
fi

# --- optional VS Code extension ----------------------------------------------
if [ "$NO_VSIX" -eq 1 ]; then
    info "vsix: skipped (--no-vsix)"
else
    vsix=""
    if [ -d "$ROOT/extensions/cp3-vscode/dist" ]; then
        vsix=$(find "$ROOT/extensions/cp3-vscode/dist" -maxdepth 1 -type f -name '*.vsix' | sort | head -n 1 || true)
    fi
    if [ -n "$vsix" ]; then
        cp -p "$vsix" "$STAGE/share/vscode/$(basename "$vsix")"
        info "vsix: $(basename "$vsix")"
    else
        info "vsix: none found (continuing without it)"
    fi
fi

# --- README ------------------------------------------------------------------
cat > "$STAGE/README.txt" <<EOF
cp3 $VERSION - C+++ compiler, runtime and tooling
=================================================

Layout
  bin/cp3c        the compiler (also serves the editor LSP via "cp3c lsp")
  bin/cp3         the cp3 command: run, build, check, init, doctor, ...
  include/        header trees the compiler passes to Clang
  src/            runtime units compiled into every cp3 program
  third_party/    vendored static libraries (std/network, std/crypto, std/hash, std/text, regex)
  share/vscode/   bundled VS Code extension, when present
  manifest.json   every component with its SHA-256

Prerequisites
  Clang 16+ (clang++) on PATH, plus the usual Linux C++ runtime.
  Run "cp3 doctor" to see what the compiler will use. VS Code is optional.

Getting started
  cp3 doctor                  check the installation
  cp3 init my-app             scaffold a project
  cd my-app
  cp3 run src/main.cp3        build and run it

Install (no root)
  ./install.sh                copies to \$HOME/.local/lib/cp3 and links
                              \$HOME/.local/bin/cp3
  ./install.sh --prefix /opt/cp3 --force
EOF

# --- manifest.json -----------------------------------------------------------
compiler_version=$("$STAGE/bin/cp3c" --version 2>/dev/null | head -n 1 || true)
[ -n "$compiler_version" ] || fail "the staged bin/cp3c does not run"
expected_compiler_version="cp3c $VERSION"
[ "$compiler_version" = "$expected_compiler_version" ] || \
    fail "the staged compiler reports '$compiler_version' but version.txt is '$VERSION'; run tools/update-version.sh and rebuild cp3c"
built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

manifest="$STAGE/manifest.json"
{
    printf '{\n'
    printf '  "name": "cp3",\n'
    printf '  "version": "%s",\n' "$(json_escape "$VERSION")"
    printf '  "compiler_version": "%s",\n' "$(json_escape "$compiler_version")"
    printf '  "built_at": "%s",\n' "$built_at"
    printf '  "components": [\n'
    first=1
    while IFS= read -r -d '' file; do
        [ "$file" = "$manifest" ] && continue
        rel=${file#"$STAGE/"}
        size=$(wc -c < "$file" | tr -d '[:space:]')
        hash=$(sha256sum "$file" | cut -d' ' -f1)
        if [ "$first" -eq 0 ]; then
            printf ',\n'
        fi
        first=0
        printf '    {"path": "%s", "bytes": %s, "sha256": "%s"}' \
            "$(json_escape "$rel")" "$size" "$hash"
    done < <(find "$STAGE" -type f -print0 | sort -z)
    printf '\n  ]\n}\n'
} > "$manifest"

# --- smoke -------------------------------------------------------------------
if [ "$SKIP_SMOKE" -eq 1 ]; then
    info "smoke: skipped (--skip-smoke)"
else
    smoke=$(mktemp -d "${TMPDIR:-/tmp}/cp3-package-smoke-XXXXXX")
    trap 'rm -rf "$smoke"' EXIT
    cat > "$smoke/main.cp3" <<'EOF'
import console from "std/console";

console.log("package smoke ok");
EOF
    output=""
    status=0
    output=$(CP3_ROOT="$STAGE" "$STAGE/bin/cp3c" run "$smoke/main.cp3" 2>&1) || status=$?
    if [ "$status" -ne 0 ] || ! printf '%s' "$output" | grep -q "package smoke ok"; then
        printf '%s\n' "$output" >&2
        fail "staged smoke build failed (exit $status)"
    fi
    info "smoke: staged compiler built and ran a hello program"
    rm -rf "$smoke"
    trap - EXIT
fi

# --- installer + archive -----------------------------------------------------
cp -p "$ROOT/packaging/linux/install.sh" "$OUT_DIR/install.sh"
chmod +x "$OUT_DIR/install.sh"

archive="$OUT_DIR/cp3-$VERSION-linux-$ARCH.tar.gz"
rm -f "$archive"
tar -C "$OUT_DIR" -czf "$archive" cp3 install.sh
archive_hash=$(sha256sum "$archive" | cut -d' ' -f1)
printf '%s  %s\n' "$archive_hash" "$(basename "$archive")" > "$OUT_DIR/SHA256SUMS.txt"

total_bytes=$(du -sk "$STAGE" | awk '{print $1}')
info "staged $(find "$STAGE" -type f | wc -l | tr -d '[:space:]') components, ${total_bytes} KB -> $STAGE"
info "archive: $archive"
info "checksums: $OUT_DIR/SHA256SUMS.txt"
info "install: ./install.sh (or unpack cp3/ and add cp3/bin to PATH)"
