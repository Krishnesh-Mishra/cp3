#!/usr/bin/env python3
# =============================================================================
# tools/run_examples.py -- compile & run the examples/ showcase programs.
#
# Discovers examples/<category>/<name>/expect.json and runs `cp3c run main.cp3`
# in each example's own directory:
#
#   * "active"  -- must compile, run, and match expect.stdout / expect.exitCode
#                  exactly (newline-normalized). Any mismatch is a FAILURE
#                  (exit 1); these are verified facts, not claims.
#   * "pending" -- spec-correct but blocked on a feature the compiler does not
#                  have yet; always informational. When one starts compiling
#                  and running, the runner prints a prominent
#                  "READY: promote to active" line -- the milestone signal.
#                  --write-observed refreshes each pending example's record.
#
# Examples run with stdin closed (like a CI box); interactive programs are
# written so their EOF path prints a reproducible transcript.
#
# Usage:  python tools/run_examples.py [--cp3c PATH] [--filter SUBSTR]
#                                     [--verbose] [--timeout SEC] [--write-observed]
# Snapshot: python tools/run_examples.py --cp3c build-semacomp/bin/cp3c.exe
# Exit: 0 when every active example passed, 1 on failure/drift/broken manifest.
# =============================================================================
from __future__ import annotations

import argparse
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

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXAMPLES_DIR = REPO_ROOT / "examples"

# Any line carrying a diagnostic code; used to surface "why is it pending".
_CP3_CODE = re.compile(r"CP3[0-9A-Za-z]+")

VALID_STATUSES = ("active", "pending")


def _normalize_newlines(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def _decode(data: Optional[bytes]) -> str:
    if not data:
        return ""
    return _normalize_newlines(data.decode("utf-8", errors="replace"))


def _first_diagnostic(output: str) -> Optional[str]:
    """The first line of compiler output that carries a CP3xxxx code."""
    for raw in output.splitlines():
        if _CP3_CODE.search(raw):
            return raw.strip()
    return None


# --- Example model -----------------------------------------------------------
@dataclass
class Example:
    category: str
    name: str
    directory: Path
    expect_path: Path
    spec: dict = field(default_factory=dict)

    @property
    def display(self) -> str:
        return f"{self.category}/{self.name}" if self.category else self.name

    @property
    def status(self) -> str:
        return str(self.spec.get("status", ""))

    @property
    def _expect(self) -> dict:
        expect = self.spec.get("expect")
        return expect if isinstance(expect, dict) else {}

    @property
    def expected_stdout(self) -> str:
        return _normalize_newlines(str(self._expect.get("stdout", "")))

    @property
    def expected_exit(self) -> int:
        return int(self._expect.get("exitCode", 0))

    @property
    def mode(self) -> str:
        return str(self.spec.get("mode", "run"))


@dataclass
class Outcome:
    example: Example
    status: str  # "passed" | "failed" | "pending" | "ready"
    detail: str = ""


def discover_examples(examples_dir: Path, filter_substr: Optional[str]) -> tuple[list[Example], list[str]]:
    """Returns (examples, discovery_warnings). Never raises."""
    examples: list[Example] = []
    warnings: list[str] = []
    if not examples_dir.exists():
        warnings.append(f"examples directory not found: {examples_dir}")
        return examples, warnings

    for expect_path in sorted(examples_dir.glob("*/*/expect.json")):
        category = expect_path.parent.parent.name
        display = f"{category}/{expect_path.parent.name}"
        if filter_substr and filter_substr not in display:
            continue

        spec: dict = {}
        try:
            spec = json.loads(expect_path.read_text(encoding="utf-8"))
            if not isinstance(spec, dict):
                warnings.append(f"{display}: expect.json is not a JSON object")
                spec = {}
        except (OSError, ValueError) as exc:
            warnings.append(f"{display}: could not read/parse expect.json ({exc})")

        examples.append(Example(category, expect_path.parent.name, expect_path.parent, expect_path, spec))
    return examples, warnings


def _resolve_compiler(path_arg: Optional[str]) -> Optional[Path]:
    """Mirror tests/runner/run_tests.py: explicit path wins, else build/bin/cp3c."""
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


def evaluate(example: Example, compiler: Path, timeout: float) -> Outcome:
    """Run one example and classify it. Never raises."""
    if example.mode != "run":
        return Outcome(example, "failed", f'unsupported mode "{example.mode}" (expected "run")')
    main_cp3 = example.directory / "main.cp3"
    if not main_cp3.is_file():
        return Outcome(example, "failed", "missing main.cp3")

    cmd = [str(compiler), "run", "main.cp3"]
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            timeout=timeout,
            cwd=str(example.directory),
            stdin=subprocess.DEVNULL,
        )
    except subprocess.TimeoutExpired:
        if example.status == "active":
            return Outcome(example, "failed", f"timed out after {timeout:g}s")
        return Outcome(example, "pending", f"did not finish within {timeout:g}s (long-running program?)")
    except FileNotFoundError:
        return Outcome(example, "failed", f"cp3c executable not found: {compiler}")
    except OSError as exc:  # pragma: no cover - defensive
        return Outcome(example, "failed", f"could not run cp3c ({exc})")

    stdout = _decode(proc.stdout)
    stderr = _decode(proc.stderr)
    exit_code = proc.returncode

    if example.status == "active":
        if exit_code != example.expected_exit:
            hint = _first_diagnostic(stderr + "\n" + stdout) or (stderr.strip() or stdout.strip())
            return Outcome(example, "failed", f"expected exit {example.expected_exit}, got {exit_code} ({hint or 'no output'})")
        if stdout != example.expected_stdout:
            return Outcome(
                example,
                "failed",
                f"stdout mismatch: expected {example.expected_stdout!r}, got {stdout!r}",
            )
        return Outcome(example, "passed")

    if example.status != "pending":
        return Outcome(example, "failed", f'unknown status "{example.status}" (expected one of {", ".join(VALID_STATUSES)})')

    # --- pending: informational only, never a failure ------------------------
    if exit_code == 0:
        matched = stdout == example.expected_stdout
        note = "stdout matches expect.json" if matched else "stdout differs from expect.json - review it before promoting"
        return Outcome(example, "ready", f"it compiles and runs now ({note})")

    diagnostic = _first_diagnostic(stderr + "\n" + stdout)
    if diagnostic:
        return Outcome(example, "pending", diagnostic)
    if exit_code == STUB_EXIT_CODE and STUB_MARKER in (stdout + stderr).lower():
        return Outcome(example, "pending", "compiler is still a scaffold build")
    tail = (stderr.strip() or stdout.strip()).splitlines()
    return Outcome(example, "pending", f"exited {exit_code} without a diagnostic" + (f" ({tail[-1]})" if tail else ""))


def _write_observed(example: Example, observed: str) -> bool:
    """Refresh `observed` in expect.json, preserving key order. Returns True on success."""
    spec = dict(example.spec)
    spec["observed"] = observed
    try:
        # newline="\n" keeps refreshes diff-clean on Windows (LF, not CRLF).
        with open(example.expect_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(spec, indent=2) + "\n")
        return True
    except OSError as exc:
        print(f"error: could not write {example.expect_path} ({exc})", file=sys.stderr)
        return False


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Compile and run the examples/ showcase programs through cp3c."
    )
    parser.add_argument("--cp3c", default=None, help="path to the cp3c executable (default: build/bin/cp3c)")
    parser.add_argument("--examples", default=str(DEFAULT_EXAMPLES_DIR),
                        help="examples root directory (default: examples/)")
    parser.add_argument("--filter", dest="filter_substr", default=None,
                        help="only run examples whose category/name contains SUBSTR")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="also print a line for every passing/pending example")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="per-example timeout in seconds (default: 30)")
    parser.add_argument("--write-observed", dest="write_observed", action="store_true",
                        help="update each blocked pending example's `observed` diagnostic "
                             "in its expect.json, then exit")
    args = parser.parse_args(argv)

    examples_dir = Path(args.examples)
    examples, warnings = discover_examples(examples_dir, args.filter_substr)

    compiler = _resolve_compiler(args.cp3c)
    compiler_missing = compiler is None or not compiler.is_file()

    print("cp3c example runner")
    print(f"  compiler : {compiler if compiler else '(not found)'}")
    print(f"  examples : {examples_dir}")
    print(f"  found    : {len(examples)} example(s)")
    for warning in warnings:
        print(f"  warning  : {warning}")
    print()

    if compiler_missing:
        print("  note: no cp3c executable found - every example is reported as "
              "unverified; pass --cp3c <path> to point at one.")

    outcomes: list[Outcome] = []
    for example in examples:
        if compiler_missing:
            outcomes.append(Outcome(example, "failed", "cp3c executable not found"))
        else:
            outcomes.append(evaluate(example, compiler, args.timeout))

    passed = sum(1 for o in outcomes if o.status == "passed")
    failed = sum(1 for o in outcomes if o.status == "failed")
    ready = [o for o in outcomes if o.status == "ready"]
    pending = sum(1 for o in outcomes if o.status == "pending")
    active = sum(1 for o in outcomes if o.example.status == "active")

    for outcome in outcomes:
        label = outcome.example.display
        if outcome.status == "passed":
            if args.verbose:
                print(f"  ok    {label}")
        elif outcome.status == "failed":
            print(f"  FAIL  {label}: {outcome.detail}")
        elif outcome.status == "ready":
            print(f"  READY {label}: {outcome.detail}")
        else:  # pending
            requires = outcome.example.spec.get("requires")
            suffix = f" [requires: {requires}]" if requires else ""
            print(f"  pend  {label}: {outcome.detail}{suffix}")

    # --- refresh observed diagnostics (opt-in side effect) --------------------
    if args.write_observed:
        updated = 0
        for outcome in outcomes:
            if outcome.status == "pending":
                if _write_observed(outcome.example, outcome.detail):
                    updated += 1
            elif outcome.status == "ready":
                print(f"  note: {outcome.example.display} is ready to promote - "
                      f"flip its status to \"active\" instead of refreshing `observed`")
        print()
        print(f"refreshed `observed` for {updated} pending example(s).")
        return 0 if failed == 0 else 1

    print()
    summary = (f"{len(examples)} found, {active} active "
               f"({passed} passed, {failed} failed), {pending} pending")
    if ready:
        summary += f", {len(ready)} READY to promote"
    print(f"{summary}.")
    if ready:
        print(f"*** READY: {len(ready)} pending example(s) now compile and run - "
              f'promote to "active" (see examples/README.md) ***')
    else:
        print("(pending examples are informational; see examples/README.md for the workflow)")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:  # pragma: no cover
        sys.exit(130)
