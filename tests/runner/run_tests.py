#!/usr/bin/env python3
# =============================================================================
# tests/runner/run_tests.py — the cp3c test harness.
#
# Discovers every tests/cases/**/<name>.expect.json, pairs it with the sibling
# <name>.cp3, runs cp3c, and compares the result against the frozen schema
# documented in tests/cases/README.md.
#
# Designed to be safe while cp3c is still a scaffold: a compile command that
# exits with code 3 and prints the "not implemented" marker is reported as
# SKIPPED, never a crash. When nothing is runnable the harness exits 0.
#
# Usage:
#   python tests/runner/run_tests.py [--cp3c PATH] [--cases DIR]
#                                    [--filter SUBSTR] [--json] [--verbose]
#
# Exit code: 0 when no case failed (all passed or all skipped), 1 otherwise.
# =============================================================================
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# --- Exit codes shared with src/cli/include/cp3/cli.hpp ----------------------
STUB_EXIT_CODE = 3
STUB_MARKER = "not implemented"

REPO_ROOT = Path(__file__).resolve().parents[2]

# --- Diagnostic-line parsing -------------------------------------------------
# Canonical diagnostic line shape the CLI is expected to emit:
#   <path>:<line>:<col>: <severity>: <CODE>: <message>
# The loose pattern is a tolerant fallback for any line that carries both a
# :line:col: location and a CP3xxxx code in some other order.
_CANONICAL = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+):\s*"
    r"(?P<sev>[A-Za-z]+):\s*(?P<code>CP3[0-9A-Za-z]+)\b(?P<rest>.*)$"
)
# Fallback pieces, applied to any line the canonical shape does not match.
_CODE_ANYWHERE = re.compile(r"CP3[0-9A-Za-z]+")
_LOCATION = re.compile(r"(?<![0-9A-Za-z])(?P<line>\d+):(?P<col>\d+)(?![0-9])")


def _normalize_newlines(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def _decode(data: Optional[bytes]) -> str:
    if not data:
        return ""
    return _normalize_newlines(data.decode("utf-8", errors="replace"))


@dataclass
class Diagnostic:
    line: Optional[int]
    col: Optional[int]
    code: Optional[str]
    text: str


def parse_diagnostics(output: str) -> list[Diagnostic]:
    found: list[Diagnostic] = []
    for raw in output.splitlines():
        line = raw.rstrip("\n")
        if not line.strip():
            continue
        m = _CANONICAL.match(line)
        if m:
            found.append(
                Diagnostic(
                    int(m.group("line")),
                    int(m.group("col")),
                    m.group("code"),
                    line,
                )
            )
            continue
        code_m = _CODE_ANYWHERE.search(line)
        if code_m:
            loc_m = _LOCATION.search(line)
            found.append(
                Diagnostic(
                    int(loc_m.group("line")) if loc_m else None,
                    int(loc_m.group("col")) if loc_m else None,
                    code_m.group(0),
                    line,
                )
            )
    return found


def _code_pattern_to_regex(pattern: str) -> re.Pattern[str]:
    # 'x'/'X'/'?' match one character; '*' matches any run. A run of two or
    # more wildcard characters is treated as "any run" — this is what makes the
    # frozen placeholder code "CP3xxxx" match a real code such as "CP3408"
    # (which has only three digits). Everything else is literal.
    out = []
    i = 0
    while i < len(pattern):
        ch = pattern[i]
        if ch == "*":
            out.append(".*")
        elif ch in "xX?":
            run = 0
            while i < len(pattern) and pattern[i] in "xX?":
                run += 1
                i += 1
            out.append(".*" if run >= 2 else ".")
            continue
        else:
            out.append(re.escape(ch))
        i += 1
    return re.compile("^" + "".join(out) + "$")


def diagnostic_matches(expected: dict, diag: Diagnostic) -> bool:
    code = expected.get("code")
    if code:
        if not diag.code or not _code_pattern_to_regex(str(code)).match(diag.code):
            return False
    want_line = expected.get("line")
    if want_line is not None:
        if diag.line != int(want_line):
            return False
    want_col = expected.get("col")
    if want_col is not None:
        if diag.col != int(want_col):
            return False
    contains = expected.get("contains")
    if contains:
        if str(contains) not in diag.text:
            return False
    return True


# --- Case model --------------------------------------------------------------
@dataclass
class Case:
    category: str
    name: str
    cp3_path: Path
    expect_path: Path
    spec: dict = field(default_factory=dict)

    @property
    def display(self) -> str:
        return f"{self.category}/{self.name}" if self.category else self.name


@dataclass
class Outcome:
    case: Case
    status: str  # "passed" | "failed" | "skipped"
    detail: str = ""


def discover_cases(cases_dir: Path, filter_substr: Optional[str]) -> tuple[list[Case], list[str]]:
    """Returns (cases, discovery_warnings). Never raises."""
    cases: list[Case] = []
    warnings: list[str] = []
    if not cases_dir.exists():
        warnings.append(f"cases directory not found: {cases_dir}")
        return cases, warnings

    for expect_path in sorted(cases_dir.rglob("*.expect.json")):
        rel = expect_path.relative_to(cases_dir)
        rel_str = str(rel).replace("\\", "/")
        if filter_substr and filter_substr not in rel_str:
            continue

        stem = expect_path.name[: -len(".expect.json")]
        cp3_path = expect_path.parent / (stem + ".cp3")
        category = str(rel.parent).replace("\\", "/")
        if category == ".":
            category = ""

        spec: dict = {}
        try:
            spec = json.loads(expect_path.read_text(encoding="utf-8"))
            if not isinstance(spec, dict):
                warnings.append(f"{rel_str}: expected a JSON object")
                spec = {}
        except (OSError, ValueError) as exc:
            warnings.append(f"{rel_str}: could not read/parse expect JSON ({exc})")

        cases.append(Case(category, stem, cp3_path, expect_path, spec))
    return cases, warnings


def _resolve_compiler(path_arg: Optional[str]) -> Optional[Path]:
    candidates: list[Path] = []
    if path_arg:
        candidates.append(Path(path_arg))
    else:
        for name in ("cp3c.exe", "cp3c"):
            candidates.append(REPO_ROOT / "build" / "bin" / name)

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
        # Allow "--cp3c build/bin/cp3c" without the extension on Windows.
        if os.name == "nt":
            with_exe = (
                candidate.with_suffix(candidate.suffix + ".exe")
                if candidate.suffix
                else Path(str(candidate) + ".exe")
            )
            if with_exe.is_file():
                return with_exe.resolve()
    return candidates[0] if candidates else None


def evaluate(case: Case, compiler: Path, timeout: float) -> Outcome:
    mode = case.spec.get("mode", "check")
    if mode not in ("run", "check"):
        mode = "check"

    if not case.cp3_path.is_file():
        return Outcome(case, "failed", f"missing source file: {case.cp3_path}")

    cmd = [str(compiler), mode, str(case.cp3_path)]
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            timeout=timeout,
            cwd=str(case.cp3_path.parent),
        )
    except subprocess.TimeoutExpired:
        return Outcome(case, "failed", f"timed out after {timeout:g}s")
    except FileNotFoundError:
        return Outcome(case, "skipped", "cp3c executable not found")
    except OSError as exc:  # pragma: no cover - defensive
        return Outcome(case, "skipped", f"could not run cp3c ({exc})")

    stdout = _decode(proc.stdout)
    stderr = _decode(proc.stderr)
    combined = stdout + "\n" + stderr
    exit_code = proc.returncode

    # --- scaffold detection ---------------------------------------------------
    if exit_code == STUB_EXIT_CODE and STUB_MARKER in combined.lower():
        return Outcome(case, "skipped", "compiler is a scaffold build")

    expected_error = case.spec.get("error")
    expected_stdout = _normalize_newlines(str(case.spec.get("stdout", "")))
    expected_exit = case.spec.get("exitCode", 0)

    # --- error cases ----------------------------------------------------------
    if expected_error is not None:
        if expected_exit is not None and exit_code != int(expected_exit):
            return Outcome(
                case,
                "failed",
                f"expected exit {expected_exit}, got {exit_code}",
            )
        if exit_code == 0:
            return Outcome(case, "failed", "expected a compile error, got exit 0")

        diags = parse_diagnostics(combined)
        for diag in diags:
            if diagnostic_matches(expected_error, diag):
                return Outcome(case, "passed")
        if not diags:
            return Outcome(
                case,
                "failed",
                "expected an error diagnostic, but none was found on stdout/stderr",
            )
        wanted = json.dumps(expected_error, sort_keys=True)
        got = "; ".join(d.text.strip() for d in diags[:4])
        return Outcome(case, "failed", f"no diagnostic matched {wanted} (got: {got})")

    # --- success cases --------------------------------------------------------
    if exit_code != int(expected_exit):
        tail = (stderr.strip() or stdout.strip()).splitlines()
        hint = f" (last output: {tail[-1]})" if tail else ""
        return Outcome(case, "failed", f"expected exit {expected_exit}, got {exit_code}{hint}")

    if stdout != expected_stdout:
        return Outcome(
            case,
            "failed",
            f"stdout mismatch: expected {expected_stdout!r}, got {stdout!r}",
        )

    return Outcome(case, "passed")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="cp3c test harness (discovers tests/cases/**/*.expect.json)."
    )
    parser.add_argument("--cp3c", default=None, help="path to the cp3c executable")
    parser.add_argument(
        "--cases",
        default=str(REPO_ROOT / "tests" / "cases"),
        help="root directory containing case folders (default: tests/cases)",
    )
    parser.add_argument("--filter", dest="filter_substr", default=None,
                        help="only run cases whose relative path contains SUBSTR")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="print every case, not just failures")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="per-case timeout in seconds (default: 60)")
    args = parser.parse_args(argv)

    cases_dir = Path(args.cases)
    cases, warnings = discover_cases(cases_dir, args.filter_substr)
    compiler = _resolve_compiler(args.cp3c)

    outcomes: list[Outcome] = []
    compiler_missing = compiler is None or not compiler.is_file()
    if compiler_missing:
        reason = "cp3c executable not found"
        outcomes = [Outcome(c, "skipped", reason) for c in cases]
    else:
        for case in cases:
            outcomes.append(evaluate(case, compiler, args.timeout))

    passed = sum(1 for o in outcomes if o.status == "passed")
    failed = sum(1 for o in outcomes if o.status == "failed")
    skipped = sum(1 for o in outcomes if o.status == "skipped")

    if args.json:
        payload = {
            "compiler": str(compiler) if compiler else None,
            "casesDir": str(cases_dir),
            "found": len(cases),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "runnable": passed + failed > 0,
            "warnings": warnings,
            "results": [
                {"case": o.case.display, "status": o.status, "detail": o.detail}
                for o in outcomes
            ],
        }
        json.dump(payload, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0 if failed == 0 else 1

    print("cp3 test harness")
    print(f"  compiler : {compiler if compiler else '(not found)'}")
    print(f"  cases dir: {cases_dir}")
    print(f"  found    : {len(cases)} case(s)")
    for warning in warnings:
        print(f"  warning  : {warning}")
    print()

    for outcome in outcomes:
        c = outcome.case.display
        if outcome.status == "passed":
            if args.verbose:
                print(f"  ok   {c}")
        elif outcome.status == "failed":
            print(f"  FAIL {c}: {outcome.detail}")
        else:  # skipped
            if args.verbose or (passed + failed == 0):
                print(f"  skip {c} ({outcome.detail})")

    print()
    if passed + failed == 0:
        print(f"{len(cases)} found, nothing runnable yet "
              f"({skipped} skipped); treating as success while cp3c is a scaffold.")
    else:
        print(f"{len(cases)} found, {passed} passed, {failed} failed, {skipped} skipped.")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:  # pragma: no cover
        sys.exit(130)
