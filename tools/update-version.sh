#!/usr/bin/env sh
# =============================================================================
# tools/update-version.sh - sync every version-bearing file from version.txt.
#
# version.txt at the repository root is the single source of truth. The shared
# implementation is tools/update-version.py so Windows and Linux apply exactly
# the same replacements.
#
# Usage:
#   ./tools/update-version.sh
#   ./tools/update-version.sh --check   # exit 1 if anything is stale
# =============================================================================
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ -n "${PYTHON:-}" ]; then
    :
elif command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1; then
    PYTHON=python
else
    echo "update-version.sh: Python 3 is required to sync version.txt (install python3)." >&2
    exit 1
fi

exec "$PYTHON" "$ROOT/tools/update-version.py" "$@"
