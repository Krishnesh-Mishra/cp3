#!/usr/bin/env python3
"""Synchronize every version-bearing source from the repository-root version.txt.

version.txt is the single source of truth. This script updates the compiler
identity, CMake project version, runtime version, VS Code extension metadata,
packaging defaults, and the current (non-historical) documentation examples.

Historical verification records under benchmarks/ and examples/ are deliberately
left alone: they describe the compiler version that produced a past run.

Usage:
    python vendor/tools/update-version.py
    python vendor/tools/update-version.py --check   # exit 1 if anything is stale
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
VERSION_FILE = ROOT / "version.txt"
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?$"
)


def fail(message: str) -> "NoReturn":
    raise SystemExit("update-version: " + message)


def read_version() -> tuple[str, str, str]:
    if not VERSION_FILE.is_file():
        fail("missing version.txt at the repository root")
    text = VERSION_FILE.read_text(encoding="utf-8").strip()
    match = SEMVER.fullmatch(text)
    if not match:
        fail(
            "version.txt must contain <major>.<minor>.<patch>[-pre], "
            f"got {text!r}"
        )
    numeric = f"{match.group(1)}.{match.group(2)}.{match.group(3)}"
    stage = match.group(4) or ""
    return text, numeric, stage


def read_text(path: Path) -> str:
    # newline="" keeps CRLF/LF exactly as the repository stores it.
    with path.open("r", encoding="utf-8", newline="") as handle:
        return handle.read()


def write_text(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        handle.write(text)


class Sync:
    def __init__(self, check: bool) -> None:
        self.check = check
        self.changed = 0

    def replace(
        self,
        relative: str,
        pattern: str,
        replacement: str,
        *,
        count: int = 0,
    ) -> None:
        path = ROOT / relative
        if not path.is_file():
            fail(f"missing {relative}")
        old = read_text(path)
        new, hits = re.subn(pattern, replacement, old, count=count)
        if hits == 0:
            fail(f"version pattern not found in {relative}")
        if new == old:
            return
        self.changed += 1
        if self.check:
            print(f"out-of-sync {relative}")
            return
        write_text(path, new)
        print(f"updated {relative}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="report stale files without writing them",
    )
    args = parser.parse_args()

    full, numeric, stage = read_version()
    sync = Sync(args.check)

    # Compiler identity and build metadata.
    sync.replace(
        "CMakeLists.txt",
        r"(?m)^(project\(cp3c\r?\n\s+VERSION )[^\r\n]+",
        rf"\g<1>{numeric}",
    )
    sync.replace(
        "src/common/include/cp3/meta/version.hpp",
        r'(inline constexpr const char\* kVersion = ")[^"]*(")',
        rf"\g<1>{numeric}\g<2>",
    )
    sync.replace(
        "src/common/include/cp3/meta/version.hpp",
        r'(inline constexpr const char\* kVersionStage = ")[^"]*(")',
        rf"\g<1>{stage}\g<2>",
    )
    sync.replace(
        "src/common/include/cp3/meta/version.hpp",
        r'(// e\.g\. "cp3c )[^"]+(")',
        rf"\g<1>{full}\g<2>",
    )
    sync.replace(
        "src/runtime/src/core/runtime.cpp",
        r'(?s)(std::string version\(\) \{\s*return ")[^"]*(")',
        rf"\g<1>{full}\g<2>",
    )

    # VS Code extension metadata and artifact name.
    sync.replace(
        "dist/extensions/cp3-vscode/package.json",
        r'(?m)^(\s*"version"\s*:\s*")[^"]*(")',
        rf"\g<1>{full}\g<2>",
        count=1,
    )
    sync.replace(
        "dist/extensions/cp3-vscode/package-lock.json",
        r'(?m)^(\s*"version"\s*:\s*")[^"]*(")',
        rf"\g<1>{full}\g<2>",
        count=2,
    )
    sync.replace(
        "dist/extensions/cp3-vscode/scripts/package.ps1",
        r"cp3-vscode-[0-9][0-9A-Za-z.\-]*\.vsix",
        f"cp3-vscode-{full}.vsix",
    )

    # Current packaging and extension documentation examples.
    sync.replace(
        "dist/packaging/README.md",
        r"single version source \(`[^`]+`\)",
        f"single version source (`{full}`)",
    )
    sync.replace(
        "dist/packaging/windows/README.md",
        r"cp3-[0-9][0-9A-Za-z.\-]*-windows-x64\.msi",
        f"cp3-{full}-windows-x64.msi",
    )
    sync.replace(
        "dist/packaging/linux/README.md",
        r"cp3-[0-9][0-9A-Za-z.\-]*-linux-(x64|arm64|x86)\.tar\.gz",
        rf"cp3-{full}-linux-\g<1>.tar.gz",
    )
    sync.replace(
        "dist/extensions/cp3-vscode/README.md",
        r"cp3-[0-9][0-9A-Za-z.\-]*-windows-x64\.msi",
        f"cp3-{full}-windows-x64.msi",
    )
    sync.replace(
        "dist/extensions/cp3-vscode/README.md",
        r"cp3-vscode-[0-9][0-9A-Za-z.\-]*\.vsix",
        f"cp3-vscode-{full}.vsix",
    )

    # Current architecture/decision notes (not historical run records).
    sync.replace(
        "docs/.rough/ARCHITECTURE.md",
        r"currently `cp3c [^`]+`",
        f"currently `cp3c {full}`",
    )
    sync.replace(
        "docs/.rough/DECISIONS.md",
        r"`--version` prints `cp3c [^`]+`",
        f"`--version` prints `cp3c {full}`",
    )

    if sync.changed == 0:
        print(f"update-version: {full} is in sync")
        return 0
    if args.check:
        print(
            f"update-version: {sync.changed} file(s) out of sync with version.txt"
        )
        return 1
    print(f"update-version: synced {full} ({sync.changed} file(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
