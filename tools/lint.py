#!/usr/bin/env python3
# =============================================================================
# tools/lint.py - developer lint entry point.
#
# Runs every repository lint check even when an earlier one fails, then exits 1
# if any check failed:
#
#   * tools/lint_layout.py  - STYLE.md layout budgets and naming
#   * tools/check_cp3002.py - zero-placeholder gate
#
# Usage:
#
#     python tools/lint.py
#     cmake --build <build> --target lint
#
# The individual scripts still run on their own; this wrapper is only the
# shared entry point that keeps one failure from hiding the other report.
# =============================================================================

from __future__ import annotations

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKS = ("lint_layout.py", "check_cp3002.py")


def main():
    status = 0
    for name in CHECKS:
        script = os.path.join(ROOT, "tools", name)
        code = subprocess.call([sys.executable, script], cwd=ROOT)
        if code != 0:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
