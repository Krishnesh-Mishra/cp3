#!/usr/bin/env python3
# =============================================================================
# tools/lint_layout.py - STYLE.md layout/budget linter for `src/**`.
#
# Run directly, or through the advisory CMake target:
#
#     python tools/lint_layout.py                # report; exit 1 on HARD violations
#     python tools/lint_layout.py --warn-only    # report only; always exit 0
#     cmake --build <build> --target lint
#
# WHAT IT CHECKS (STYLE.md)
# ------------------------
#   * §2 hard budgets, per file:
#       - source file (`.cpp`, private header) .... soft 400 / hard 500 lines
#       - public header (`include/cp3/*.hpp`) ..... soft 500 / hard 600 lines
#       - function / method ....................... soft  60 / hard  80 lines
#       - type (`class`/`struct`/`union`/`enum`) .. soft 250 / hard 400 lines
#       - line length ............................. soft 100 / hard 100 chars
#       - parameter count ......................... soft   4 / hard   5
#       - nesting depth ........................... soft   3 / hard   4
#   * §4 naming spot-checks:
#       - file names are `snake_case.{cpp,hpp,h}`
#       - project macros are `CP3_UPPER_SNAKE`
#   * §2 suppression: a file may carry `// cp3-lint: allow-file-size - <reason>`
#     with a NON-EMPTY reason; that waives only the file-size check for that
#     file, and the marker is itself reported when the reason is missing.
#
# HEURISTICS (all deterministic, stdlib-only, no compiler required)
# ----------------------------------------------------------------
#  1. Comment/string masking. Every line is copied with the *contents* of
#     `//` comments, `/* */` comments, `"..."` and `'...'` literals replaced by
#     spaces (length preserved, so column/line mapping stays exact). Raw string
#     literals and multi-line string continuations are not modelled; a brace
#     inside one would be miscounted (documented, not observed in this tree).
#  2. Preprocessor blanking. A line whose first non-space character is `#`, plus
#     all of its backslash-continuation lines, is blanked entirely before
#     analysis. This is what keeps MACRO-GENERATED code (e.g. the
#     CP3_AST_NODE_LIST / CP3_DISPATCH_CASE tables) from being reported as
#     giant functions. Braces inside a macro body are therefore invisible to
#     depth tracking: do not write unbalanced braces in a macro.
#  3. A FUNCTION body is a `{` whose accumulated header - the text since the
#     previous `;`, `{` or `}` - matches
#         [<pre>] <name> ( <params> ) <tail>
#     where `<name>` is an identifier (optionally `A::B`-qualified or
#     `operator()`) and `<tail>` holds qualifiers / a ctor init list. The header
#     is rejected if `<pre>` contains `=` or `[`, ends with `.`/`->`, or if the
#     name is a control keyword (`if`, `for`, `while`, ...). Function *length*
#     is `close_brace_line - open_brace_line + 1` (braces included).
#  4. Function detection is additionally gated by SCOPE: a definition is only
#     looked for at namespace scope, or directly inside a `class`/`struct`/
#     `union`/`enum` body (`namespace` and `extern` blocks are transparent).
#     Blocks nested inside a function body are therefore never mistaken for
#     functions, which is what keeps `if/for/switch` blocks and lambdas out.
#     Cost of the gate: a function defined inside a function body (a nested
#     local class method) is not measured.
#  5. PARAMETERS are counted as top-level commas + 1, ignoring commas nested in
#     `(...)`, `[...]` or `<...>` (template argument lists). A lone `void` or an
#     empty list is 0. Headers whose parameters nest parens more than one level
#     deep (function-pointer parameters) do not match the header shape in (3)
#     and are skipped rather than mis-counted.
#  6. NESTING DEPTH is the maximum brace depth inside the function body, where
#     depth 1 is the body itself. Brace-less control flow (`if (x) return;`) is
#     not counted - only braces are, cheaply and deterministically.
#  7. TYPE size is measured the same way for `class`/`struct`/`union`/`enum`
#     bodies.
#  8. FILE size is the number of lines (newline-separated, trailing newline not
#     counted as a line). Line length is character count excluding trailing
#     whitespace. Tabs count as one character.
#
# ALLOW-LIST
# ----------
# Some files legitimately break a *size* budget by being a single-source data
# definition. They are listed in ALLOW_LIST below with a mandatory reason and
# the exact checks that are waived - editing the file instead of the list is not
# an option for a frozen header. Entries are the only sanctioned exceptions;
# §2's "logic files are never exempt" rule still holds (the linter does not
# waive function-size for anything).
#
# The only platform-macro exception lives in PLATFORM_MACRO_ALLOWLIST: OS/Win32
# feature macros that this project sets (`WIN32_LEAN_AND_MEAN`, `NOMINMAX`, ...)
# are not `CP3_`-prefixed project macros.
#
# SEVERITY / EXIT CODES
# ---------------------
#   * "error"   = a §2 HARD budget (or a §4 naming rule) is broken.
#   * "warning" = a §2 SOFT budget is broken (or an advisory note).
#   * exit 0    = no errors, or `--warn-only`.
#   * exit 1    = at least one error.
#   * exit 2    = usage error (bad path).
# =============================================================================

from __future__ import annotations

import argparse
import bisect
import os
import re
import sys
from dataclasses import dataclass

# -----------------------------------------------------------------------------
# Budgets (STYLE.md §2). A value is a violation when it is GREATER than `hard`;
# "at the cap" is allowed (the file is split by concern at the cap).
# -----------------------------------------------------------------------------
FILE_SOFT, FILE_HARD = 400, 500
PUBLIC_HEADER_SOFT, PUBLIC_HEADER_HARD = 500, 600
FUNC_SOFT, FUNC_HARD = 60, 80
TYPE_SOFT, TYPE_HARD = 250, 400
LINE_SOFT, LINE_HARD = 100, 100
PARAM_SOFT, PARAM_HARD = 4, 5
NEST_SOFT, NEST_HARD = 3, 4

SCAN_EXTENSIONS = (".cpp", ".hpp", ".h")

# -----------------------------------------------------------------------------
# Allow-list: repo-relative (forward-slash) path -> waived checks.
# -----------------------------------------------------------------------------
@dataclass(frozen=True)
class AllowEntry:
    reason: str
    checks: frozenset


ALLOW_LIST = {
    # The AST registry is a data-definition header: CP3_AST_NODE_LIST is the
    # single source of truth for NodeKind + forward declarations + visitor
    # dispatch, so the file is one intentional unit and is frozen (lead-owned).
    #
    # Waived checks and why:
    #   * file-size  - the registry is deliberately one table, not split units.
    #   * type-size  - a single-source data table, not one oversized type.
    #   * line-length - a handful of registry field declarations carry trailing
    #                   explanatory comments (one is 102 chars). The header is
    #                   frozen, so this cannot be reflowed here; revisit on the
    #                   next lead sign-off touch rather than exempting it forever.
    # Nothing else is waived: functions, parameters, nesting and naming are
    # still checked inside allow-listed files (STYLE.md "logic files are never
    # exempt").
    "src/common/include/cp3/ast.hpp": AllowEntry(
        reason="AST node registry, single-source macro table",
        checks=frozenset({"file-size", "type-size", "line-length"}),
    ),
}

# Macros that configure the platform/SDK, not cp3 (STYLE.md §4 covers project
# macros; these are the only unprefixed defines in the tree).
PLATFORM_MACRO_ALLOWLIST = frozenset(
    {
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "NDEBUG",
        "UNICODE",
        "_UNICODE",
        "CRT_SECURE_NO_WARNINGS",
    }
)

CONTROL_KEYWORDS = frozenset(
    {
        "if", "for", "while", "switch", "catch", "else", "do", "try", "return",
        "sizeof", "decltype", "alignof", "typeid", "new", "delete", "throw",
        "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast",
        "namespace", "class", "struct", "union", "enum", "template",
    }
)

# -----------------------------------------------------------------------------
# Patterns
# -----------------------------------------------------------------------------
# Function-definition header: <pre> <name> ( <params> ) <tail>. `pre` is
# NON-greedy so the *earliest* plausible name wins (the declared name is the
# first identifier immediately followed by `(`), and the lookbehind keeps the
# name from starting in the middle of a longer identifier (`foo` must not be
# read as `oo`). The name may be `A::B`-qualified and may be `operator()`, so
# `int operator()(int x)` counts one parameter rather than zero.
RE_FUNC = re.compile(
    r"^(?P<pre>.*?)"
    r"(?<![A-Za-z0-9_])"
    r"(?P<name>(?:[A-Za-z_~][A-Za-z0-9_]*::)*(?:operator\s*\(\s*\)|[A-Za-z_~][A-Za-z0-9_]*))"
    r"\s*\((?P<params>[^()]*(?:\([^()]*\)[^()]*)*)\)"
    r"(?P<tail>[^;{}]*)$",
    re.S,
)

RE_TYPE_KEYWORD = re.compile(r"\b(?:class|struct|union|enum)\b")
RE_TYPE_NAME = re.compile(r"\b(?:class|struct|union|enum)\s+(?:class\s+)?([A-Za-z_][A-Za-z0-9_]*)")
RE_EXTERN_ONLY = re.compile(r"^extern\s*$")

RE_SUPPRESS = re.compile(r"cp3-lint:\s*allow-file-size\b(.*)$")
RE_MACRO = re.compile(r"^\s*#\s*define\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)")
RE_GOOD_MACRO = re.compile(r"^CP3_[A-Z0-9]+(?:_[A-Z0-9]+)*$")
RE_GOOD_FILE = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*\.(?:cpp|hpp|h)$")

# `namespace` blocks and `extern "C"` blocks are transparent for function
# detection; class bodies are too (inline member definitions).
TRANSPARENT_BLOCKS = frozenset({"NAMESPACE", "CLASS"})


# -----------------------------------------------------------------------------
# Findings
# -----------------------------------------------------------------------------
@dataclass
class Violation:
    path: str          # repo-relative, forward slashes
    line: int
    check: str         # file-size | function-length | type-size | line-length |
                       # params | nesting | file-name | macro-name | suppression
    severity: str      # "error" | "warning"
    message: str


# -----------------------------------------------------------------------------
# Source masking (see header, heuristics 1 and 2)
# -----------------------------------------------------------------------------
def mask_comments_and_strings(lines):
    """Blank comment/string *contents*, preserving length and line/col."""
    out = []
    in_block_comment = False
    for line in lines:
        chars = []
        i, n = 0, len(line)
        while i < n:
            c = line[i]
            if in_block_comment:
                if c == "*" and i + 1 < n and line[i + 1] == "/":
                    chars.append("  ")
                    i += 2
                    in_block_comment = False
                else:
                    chars.append(" ")
                    i += 1
                continue
            if c == "/" and i + 1 < n and line[i + 1] == "/":
                chars.append(" " * (n - i))
                break
            if c == "/" and i + 1 < n and line[i + 1] == "*":
                chars.append("  ")
                i += 2
                in_block_comment = True
                continue
            if c == '"' or c == "'":
                quote = c
                chars.append(" ")
                i += 1
                while i < n:
                    if line[i] == "\\" and i + 1 < n:
                        chars.append("  ")
                        i += 2
                        continue
                    if line[i] == quote:
                        chars.append(" ")
                        i += 1
                        break
                    chars.append(" ")
                    i += 1
                continue
            chars.append(c)
            i += 1
        out.append("".join(chars))
    return out


def blank_preprocessor(masked_lines):
    """Blank a preprocessor directive and its continuation lines."""
    out = list(masked_lines)
    n = len(out)
    i = 0
    while i < n:
        if out[i].lstrip().startswith("#"):
            j = i
            while True:
                continues = out[j].rstrip().endswith("\\")
                out[j] = " " * len(out[j])
                if not continues or j + 1 >= n:
                    break
                j += 1
            i = j + 1
        else:
            i += 1
    return out


# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
def count_params(params: str) -> int:
    """Top-level commas + 1, ignoring ()/[]/<> nesting; `void`/empty -> 0."""
    text = params.strip()
    if not text or text == "void":
        return 0
    paren = bracket = angle = 0
    count = 1
    for ch in text:
        if ch == "(":
            paren += 1
        elif ch == ")":
            paren = max(0, paren - 1)
        elif ch == "[":
            bracket += 1
        elif ch == "]":
            bracket = max(0, bracket - 1)
        elif ch == "<":
            angle += 1
        elif ch == ">":
            angle = max(0, angle - 1)
        elif ch == "," and paren == 0 and bracket == 0 and angle == 0:
            count += 1
    return count


def match_function_header(header: str):
    """Return the RE_FUNC match when `header` looks like a definition, else None."""
    m = RE_FUNC.match(header)
    if m is None:
        return None
    pre = m.group("pre")
    tail = m.group("tail")
    name = m.group("name")
    last = name.split("::")[-1]
    if last in CONTROL_KEYWORDS:
        return None
    if "=" in pre or "[" in pre:
        return None
    if pre.rstrip().endswith(".") or pre.rstrip().endswith("->"):
        return None
    # A trailing `= ...` after the parameter list is an initializer, not a body.
    if "=" in tail:
        return None
    return m


def is_public_header(rel_path: str) -> bool:
    parts = rel_path.split("/")
    return "include" in parts and "cp3" in parts and rel_path.endswith(".hpp")


# -----------------------------------------------------------------------------
# Per-file analysis
# -----------------------------------------------------------------------------
def analyze(rel_path, text, findings):
    lines = text.splitlines()
    add = findings.append

    # --- §4 file name --------------------------------------------------------
    base = rel_path.rsplit("/", 1)[-1]
    if not RE_GOOD_FILE.match(base):
        add(Violation(rel_path, 1, "file-name", "error",
                      f"file name '{base}' is not snake_case.{{cpp,hpp,h}} (STYLE.md sec.4)"))

    # --- suppression marker / allow-list -------------------------------------
    waived = set()
    entry = ALLOW_LIST.get(rel_path)
    if entry is not None:
        waived |= set(entry.checks)
    suppressed_file_size = False
    for idx, raw in enumerate(lines, start=1):
        if "cp3-lint" not in raw:
            continue
        m = RE_SUPPRESS.search(raw)
        if m is None:
            continue
        reason = m.group(1).strip().lstrip("\u2014\u2013-:").strip()
        if not reason:
            add(Violation(rel_path, idx, "suppression", "error",
                          "'allow-file-size' suppression needs a non-empty reason: "
                          "// cp3-lint: allow-file-size - <reason>"))
        else:
            suppressed_file_size = True
            waived.add("file-size")

    # --- §2 file size --------------------------------------------------------
    line_count = len(lines)
    if "file-size" not in waived:
        soft, hard = (PUBLIC_HEADER_SOFT, PUBLIC_HEADER_HARD) if is_public_header(rel_path) \
            else (FILE_SOFT, FILE_HARD)
        kind = "public header" if is_public_header(rel_path) else "source file"
        if line_count > hard:
            add(Violation(rel_path, 1, "file-size", "error",
                          f"{line_count} lines exceeds the {hard}-line hard budget for a "
                          f"{kind} (soft {soft}); split by concern (STYLE.md sec.2)"))
        elif line_count > soft:
            add(Violation(rel_path, 1, "file-size", "warning",
                          f"{line_count} lines exceeds the {soft}-line soft budget for a "
                          f"{kind} (hard {hard})"))
    elif suppressed_file_size and line_count <= FILE_HARD:
        add(Violation(rel_path, 1, "suppression", "warning",
                      f"'allow-file-size' suppression is stale: file is {line_count} lines "
                      f"(hard {FILE_HARD})"))

    # --- §2 line length ------------------------------------------------------
    if "line-length" not in waived:
        for idx, raw in enumerate(lines, start=1):
            width = len(raw.rstrip())
            if width > LINE_HARD:
                add(Violation(rel_path, idx, "line-length", "error",
                              f"line is {width} chars; hard budget {LINE_HARD}"))
            elif width > LINE_SOFT:
                add(Violation(rel_path, idx, "line-length", "warning",
                              f"line is {width} chars; soft budget {LINE_SOFT}"))

    # --- §4 macro naming -----------------------------------------------------
    for idx, raw in enumerate(lines, start=1):
        m = RE_MACRO.match(raw)
        if m is None:
            continue
        name = m.group("name")
        if name in PLATFORM_MACRO_ALLOWLIST or RE_GOOD_MACRO.match(name):
            continue
        add(Violation(rel_path, idx, "macro-name", "error",
                      f"macro '{name}' is not CP3_UPPER_SNAKE (STYLE.md sec.4)"))

    # --- §2 function / type / params / nesting -------------------------------
    masked = blank_preprocessor(mask_comments_and_strings(lines))
    body = "\n".join(masked)

    starts = [0]
    for pos, ch in enumerate(body):
        if ch == "\n":
            starts.append(pos + 1)

    def line_of(pos: int) -> int:
        return bisect.bisect_right(starts, pos)

    def find_close(open_pos: int) -> int:
        depth = 0
        for p in range(open_pos, len(body)):
            if body[p] == "{":
                depth += 1
            elif body[p] == "}":
                depth -= 1
                if depth == 0:
                    return p
        return len(body) - 1

    depth = 0
    stack = []
    stmt_start = 0
    pos = 0
    size = len(body)
    while pos < size:
        ch = body[pos]
        if ch == "{":
            header = body[stmt_start:pos]
            norm = header.strip()
            parent_ok = depth == 0 or (stack and stack[-1] in TRANSPARENT_BLOCKS)
            kind = "BLOCK"

            if norm and RE_TYPE_KEYWORD.search(norm) and "(" not in norm:
                kind = "CLASS"
            elif norm.startswith("namespace") or RE_EXTERN_ONLY.match(norm):
                kind = "NAMESPACE"
            elif parent_ok:
                func = match_function_header(header)
                if func is not None:
                    kind = "FUNCTION"
                    open_line = line_of(pos)
                    close = find_close(pos)
                    close_line = line_of(close)
                    length = close_line - open_line + 1
                    start_line = line_of(stmt_start + func.start("name"))
                    name = func.group("name")
                    if length > FUNC_HARD:
                        add(Violation(rel_path, start_line, "function-length", "error",
                                      f"'{name}' is {length} lines; hard budget {FUNC_HARD} "
                                      f"(soft {FUNC_SOFT})"))
                    elif length > FUNC_SOFT:
                        add(Violation(rel_path, start_line, "function-length", "warning",
                                      f"'{name}' is {length} lines; soft budget {FUNC_SOFT} "
                                      f"(hard {FUNC_HARD})"))

                    nparams = count_params(func.group("params"))
                    if nparams > PARAM_HARD:
                        add(Violation(rel_path, start_line, "params", "error",
                                      f"'{name}' takes {nparams} parameters; hard budget "
                                      f"{PARAM_HARD} (soft {PARAM_SOFT}) - use an options struct"))
                    elif nparams > PARAM_SOFT:
                        add(Violation(rel_path, start_line, "params", "warning",
                                      f"'{name}' takes {nparams} parameters; soft budget "
                                      f"{PARAM_SOFT} (hard {PARAM_HARD})"))

                    cur = 0
                    max_depth = 0
                    deep_line = open_line
                    for p in range(pos + 1, close):
                        c = body[p]
                        if c == "{":
                            cur += 1
                            if cur > max_depth:
                                max_depth = cur
                                deep_line = line_of(p)
                        elif c == "}":
                            cur -= 1
                    if max_depth > NEST_HARD:
                        add(Violation(rel_path, deep_line, "nesting", "error",
                                      f"'{name}' nests {max_depth} levels deep; hard budget "
                                      f"{NEST_HARD} (soft {NEST_SOFT})"))
                    elif max_depth > NEST_SOFT:
                        add(Violation(rel_path, deep_line, "nesting", "warning",
                                      f"'{name}' nests {max_depth} levels deep; soft budget "
                                      f"{NEST_SOFT} (hard {NEST_HARD})"))

            if kind == "CLASS" and "type-size" not in waived:
                open_line = line_of(pos)
                close_line = line_of(find_close(pos))
                length = close_line - open_line + 1
                m = RE_TYPE_NAME.search(norm)
                tname = m.group(1) if m else "<anonymous>"
                if length > TYPE_HARD:
                    add(Violation(rel_path, open_line, "type-size", "error",
                                  f"'type {tname}' spans {length} lines; hard budget {TYPE_HARD} "
                                  f"(soft {TYPE_SOFT})"))
                elif length > TYPE_SOFT:
                    add(Violation(rel_path, open_line, "type-size", "warning",
                                  f"'type {tname}' spans {length} lines; soft budget {TYPE_SOFT} "
                                  f"(hard {TYPE_HARD})"))

            stack.append(kind)
            depth += 1
            stmt_start = pos + 1
        elif ch == ";" or ch == "}":
            if ch == "}":
                if stack:
                    stack.pop()
                depth = max(0, depth - 1)
            stmt_start = pos + 1
        pos += 1


# -----------------------------------------------------------------------------
# Driver
# -----------------------------------------------------------------------------
def iter_sources(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if name.endswith(SCAN_EXTENSIONS):
                yield os.path.join(dirpath, name)


def rel_to_repo(path, repo_root):
    return os.path.relpath(path, repo_root).replace("\\", "/")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Enforce STYLE.md layout budgets and naming over src/**.")
    parser.add_argument("--warn-only", action="store_true",
                        help="report only; never exit non-zero for hard violations")
    parser.add_argument("--root", default=None,
                        help="repository root (default: the parent of tools/)")
    parser.add_argument("paths", nargs="*",
                        help="optional files/directories to lint instead of src/**")
    args = parser.parse_args(argv)

    repo_root = os.path.abspath(args.root) if args.root else \
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    targets = []
    if args.paths:
        for p in args.paths:
            ap = os.path.abspath(p)
            if os.path.isdir(ap):
                targets.extend(iter_sources(ap))
            elif os.path.isfile(ap):
                targets.append(ap)
            else:
                sys.stderr.write(f"lint_layout: no such path: {p}\n")
                return 2
    else:
        src = os.path.join(repo_root, "src")
        if not os.path.isdir(src):
            sys.stderr.write(f"lint_layout: no src/ directory under {repo_root}\n")
            return 2
        targets = list(iter_sources(src))

    targets = sorted(set(os.path.abspath(t) for t in targets))

    findings = []
    scanned = 0
    for path in targets:
        rel = rel_to_repo(path, repo_root)
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError as exc:
            sys.stderr.write(f"lint_layout: cannot read {rel}: {exc}\n")
            continue
        scanned += 1
        rel = rel_to_repo(path, repo_root)
        analyze(rel, text, findings)

    findings.sort(key=lambda v: (v.path, v.line, v.check, v.severity))
    by_file = {}
    for v in findings:
        by_file.setdefault(v.path, []).append(v)

    errors = sum(1 for v in findings if v.severity == "error")
    warnings = len(findings) - errors

    print("cp3 layout lint - STYLE.md sec.2 budgets, sec.4 naming")
    print()
    for path in sorted(by_file):
        items = by_file[path]
        hard = sum(1 for v in items if v.severity == "error")
        soft = len(items) - hard
        print(f"{path}  ({hard} error{'s' if hard != 1 else ''}, "
              f"{soft} warning{'s' if soft != 1 else ''})")
        for v in items:
            flag = "error" if v.severity == "error" else "warning"
            print(f"  {v.line:>5}: {flag:<7} {v.check:<15} {v.message}")
        print()

    print(f"{scanned} files scanned: {errors} error(s), {warnings} warning(s).")

    if errors:
        if args.warn_only:
            print("Hard violations present, but --warn-only was requested: exit 0.")
            return 0
        print("Layout lint FAILED: hard budget/naming violations above must be fixed.")
        return 1
    print("Layout lint OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
