#!/usr/bin/env sh
# =============================================================================
# install.sh - per-user installer for the Linux cp3 tarball.
#
# The tarball contains cp3/ (the relocatable tree) and this script. Running it
# copies the tree to <prefix>/lib/cp3 and creates <prefix>/bin/cp3, which
# forwards to <prefix>/lib/cp3/bin/cp3. No root privileges are required with
# the default prefix ($HOME/.local).
#
# Usage:
#   ./install.sh
#   ./install.sh --prefix /opt/cp3 --force
# =============================================================================
set -eu

PREFIX=${PREFIX:-$HOME/.local}
FORCE=0

usage() {
    cat <<'EOF'
Usage: ./install.sh [--prefix <dir>] [--force]

  --prefix <dir>   installation prefix (default: $HOME/.local)
  --force          replace an existing installation
  -h, --help       show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX=$2; shift 2 ;;
        --force)  FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/cp3
DEST="$PREFIX/lib/cp3"
WRAPPER="$PREFIX/bin/cp3"

[ -x "$SOURCE/bin/cp3c" ] || {
    echo "install.sh: missing '$SOURCE/bin/cp3c'; run this from the unpacked tarball" >&2
    exit 1
}

if [ -e "$DEST" ] && [ "$FORCE" -ne 1 ]; then
    echo "install.sh: '$DEST' already exists; re-run with --force to replace it" >&2
    exit 1
fi

mkdir -p "$PREFIX/lib" "$PREFIX/bin"
if [ -e "$DEST" ]; then
    echo "install: removing previous install at $DEST"
    rm -rf "$DEST"
fi
cp -R "$SOURCE" "$DEST"

cat > "$WRAPPER" <<EOF
#!/usr/bin/env sh
exec "$DEST/bin/cp3" "\$@"
EOF
chmod +x "$WRAPPER"

echo "install: copied cp3 to $DEST"
echo "install: wrapper at $WRAPPER"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *)
        echo ""
        echo "Add the wrapper directory to PATH:"
        echo "  export PATH=\"$PREFIX/bin:\$PATH\""
        ;;
esac
echo ""
echo "Then run: cp3 doctor"
