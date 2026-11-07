#!/usr/bin/env python3
# =============================================================================
# vendor/tools/check_diagnostics.py - keep the diagnostic code table, the catalog and
# the source tree in sync.
#
# Run directly:
#
#     python vendor/tools/check_diagnostics.py             # report; exit 1 on findings
#     python vendor/tools/check_diagnostics.py --warn-only # report only; always exit 0
#
# WHAT IT CHECKS
# --------------
#   1. `src/common/include/cp3/diagnostic_codes.hpp` is the single source of
#      truth: every `kName = "CP3xxx"` row is parsed from it.
#   2. Every code defined in the header appears in a catalog doc
#      (`docs/cli-tooling/*-diagnostic-catalog.md`). A code that exists but is
#      documented nowhere is an undocumented diagnostic.
#   3. Every `CP3xxx` token mentioned anywhere under `src/**` (the header
#      itself excluded) is defined in the header. A typo in a raw literal, or
#      a code used in a comment that was later removed from the header, is
#      caught here.
#   4. Every `CP3xxx` token mentioned in the catalog is defined in the header,
#      so the docs cannot invent codes either.
#   5. The header does not silently alias: one code defined by two names, or
#      one name mapped to two codes, is an error.
#
# HEURISTICS (deterministic, stdlib-only, no compiler required)
# ------------------------------------------------------------
#   * Token shape is `CP3` followed by exactly three digits. The header
#     comment's range shorthand ("3001-3009", "3100-3199") has no `CP` prefix
#     and is therefore ignored; a full range mention ("CP3201-CP3207") yields
#     its two endpoint tokens, both of which must be defined.
#   * `src/**` means the project's C++ sources: `.cpp`, `.hpp` and `.h` files.
#     Comments and string literals count as mentions on purpose - a stale code
#     in a comment is exactly the kind of drift this tool exists to catch.
#   * The catalog is discovered as `*-diagnostic-catalog.md` under
#     `docs/cli-tooling/`; if several match, all of them are checked.
#
# SEVERITY / EXIT CODES
# ---------------------
#   * "error"   = the header, the catalog and `src/**` disagree.
#   * exit 0    = clean, or `--warn-only`.
#   * exit 1    = at least one finding.
#   * exit 2    = usage error (missing header/catalog/sources).
# =============================================================================

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass

# -----------------------------------------------------------------------------
# Layout
# -----------------------------------------------------------------------------
HEADER_REL = "src/common/include/cp3/diag/diagnostic_codes.hpp"
CATALOG_DIR_REL = "docs/cli-tooling"
CATALOG_SUFFIX = "-diagnostic-catalog.md"
SOURCE_EXTENSIONS = (".cpp", ".hpp", ".h")

RE_DEFINE = re.compile(r"\b(k[A-Za-z0-9_]+)\s*=\s*\"(CP3[0-9]{3})\"")
RE_CODE = re.compile(r"\bCP3[0-9]{3}\b")


# -----------------------------------------------------------------------------
# Findings
# -----------------------------------------------------------------------------
@dataclass
class Finding:
    code: str       # the CP3xxx the finding is about
    check: str      # undefined | undocumented | duplicate-code | duplicate-name
    message: str


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def iter_sources(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if name.endswith(SOURCE_EXTENSIONS):
                yield os.path.join(dirpath, name)


def mentions(path, pattern):
    """Yield (line, token) for every `pattern` match in `path`."""
    for idx, line in enumerate(read_text(path).splitlines(), start=1):
        for token in pattern.findall(line):
            yield idx, token


# -----------------------------------------------------------------------------
# Checks
# -----------------------------------------------------------------------------
def parse_header(text, findings):
    """Return {code: name} from the header; record aliases and duplicates."""
    defined: dict[str, str] = {}
    seen_names: dict[str, str] = {}
    for line_no, line in enumerate(text.splitlines(), start=1):
        match = RE_DEFINE.search(line)
        if match is None:
            continue
        name, code = match.group(1), match.group(2)
        if code in defined:
            findings.append(Finding(code, "duplicate-code",
                                    f"{code} is defined by both '{defined[code]}' and "
                                    f"'{name}' ({HEADER_REL}:{line_no})"))
        else:
            defined[code] = name
        if name in seen_names:
            findings.append(Finding(code, "duplicate-name",
                                    f"'{name}' is mapped to both {seen_names[name]} and "
                                    f"{code} ({HEADER_REL}:{line_no})"))
        else:
            seen_names[name] = code
    return defined


def check_sources(src_root, header_abs, defined, findings):
    """Every code token in src/** must exist in the header."""
    header_abs = os.path.normcase(os.path.abspath(header_abs))
    scanned = 0
    for path in iter_sources(src_root):
        if os.path.normcase(os.path.abspath(path)) == header_abs:
            continue  # the definitions live here; they are not usages
        scanned += 1
        for line_no, token in mentions(path, RE_CODE):
            if token not in defined:
                rel = rel_to_repo(path)
                findings.append(Finding(token, "undefined",
                                        f"{token} is used at {rel}:{line_no} but is not "
                                        f"defined in {HEADER_REL}"))
    return scanned


def check_catalog(catalog_path, defined, findings):
    """Every catalog code must exist in the header, and vice versa."""
    documented: set[str] = set()
    for line_no, token in mentions(catalog_path, RE_CODE):
        documented.add(token)
        if token not in defined:
            rel = rel_to_repo(catalog_path)
            findings.append(Finding(token, "undefined",
                                    f"{token} is documented at {rel}:{line_no} but is not "
                                    f"defined in {HEADER_REL}"))
    return documented


def rel_to_repo(path):
    return os.path.relpath(path, REPO_ROOT).replace("\\", "/")


# -----------------------------------------------------------------------------
# Driver
# -----------------------------------------------------------------------------
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def find_catalogs(repo_root):
    catalog_dir = os.path.join(repo_root, CATALOG_DIR_REL)
    if not os.path.isdir(catalog_dir):
        return None
    return sorted(os.path.join(catalog_dir, name)
                  for name in os.listdir(catalog_dir) if name.endswith(CATALOG_SUFFIX))


def main(argv=None):
    global REPO_ROOT  # noqa: PLW0603 - single-shot CLI, set once before any use

    parser = argparse.ArgumentParser(
        description="Check the diagnostic code table against the catalog and src/**.")
    parser.add_argument("--warn-only", action="store_true",
                        help="report only; never exit non-zero for findings")
    parser.add_argument("--root", default=None,
                        help="repository root (default: the parent of vendor/tools/)")
    args = parser.parse_args(argv)

    repo_root = os.path.abspath(args.root) if args.root else REPO_ROOT
    REPO_ROOT = repo_root

    header_abs = os.path.join(repo_root, HEADER_REL)
    src_root = os.path.join(repo_root, "src")
    catalogs = find_catalogs(repo_root)
    if catalogs is None or not catalogs:
        sys.stderr.write(f"check_diagnostics: no {CATALOG_DIR_REL}/*{CATALOG_SUFFIX} "
                         f"under {repo_root}\n")
        return 2
    if not os.path.isfile(header_abs):
        sys.stderr.write(f"check_diagnostics: no {HEADER_REL} under {repo_root}\n")
        return 2
    if not os.path.isdir(src_root):
        sys.stderr.write(f"check_diagnostics: no src/ directory under {repo_root}\n")
        return 2

    findings: list[Finding] = []
    defined = parse_header(read_text(header_abs), findings)

    documented: set[str] = set()
    for catalog in catalogs:
        documented |= check_catalog(catalog, defined, findings)

    scanned = check_sources(src_root, header_abs, defined, findings)

    for code in sorted(set(defined) - documented):
        rels = ", ".join(rel_to_repo(c) for c in catalogs)
        findings.append(Finding(code, "undocumented",
                                f"{code} is defined in {HEADER_REL} but documented in no "
                                f"catalog ({rels})"))

    findings.sort(key=lambda f: (f.code, f.check, f.message))

    print("cp3 diagnostics check - header <-> catalog <-> src/**")
    print()
    print(f"{len(defined)} code(s) defined in {HEADER_REL}")
    print(f"{len(documented)} code token(s) documented in "
          f"{', '.join(rel_to_repo(c) for c in catalogs)}")
    print(f"{scanned} source file(s) scanned under src/**")
    print()
    for f in findings:
        print(f"  {f.code}  error  {f.check:<15} {f.message}")
    if findings:
        print()
    print(f"{len(findings)} finding(s).")

    if findings:
        if args.warn_only:
            print("Findings present, but --warn-only was requested: exit 0.")
            return 0
        print("Diagnostics check FAILED: fix the entries above.")
        return 1
    print("Diagnostics check OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
