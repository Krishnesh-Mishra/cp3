#!/usr/bin/env python3
# =============================================================================
# tests/v0.2.5/package-test/runner.py - the packaging regression corpus.
#
# The language corpus (tests/cases/**) runs one command per case. Packaging
# is not one command: a package is built, packed, installed and then consumed
# by another project. This runner therefore drives a *scenario*: an ordered
# list of steps, each a `cp3c` invocation in one directory of the fixture,
# each with its own expectations.
#
# A case is a folder:
#
#   case.json      the scenario (the steps and their expectations)
#   pkg/           the package project (package.json + src/*.cp3)
#   app/           the consumer project (package.json + src/*.cpp3)
#
# The fixture is copied to a unique temp directory (the checked-in fixture is
# never dirtied) and every step runs there.
#
# Usage:
#   python tests/v0.2.5/package-test/runner.py [--cp3c PATH] [--cases DIR]
#       [--filter SUBSTR] [--list] [--verbose] [--json PATH] [--timeout S]
#
# Exit codes: 0 all pass | 1 any failure | 2 infra error
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CASES = REPO_ROOT / "tests" / "v0.2.5" / "package-test" / "cases"


def default_cp3c() -> str:
    names = ["cp3c.exe"] if os.name == "nt" else ["cp3c"]
    for candidate in (
        REPO_ROOT / "build" / "windows-release" / "bin" / "cp3c.exe",
        REPO_ROOT / "build" / "windows-release" / "bin" / "cp3c",
        REPO_ROOT / "build" / "linux-release" / "bin" / "cp3c",
        REPO_ROOT / "build" / "bin" / "cp3c",
    ):
        if candidate.is_file():
            return str(candidate)
    return names[0] if shutil.which(names[0]) else str(names[0])


def load_case(case_dir: Path) -> dict | None:
    spec_path = case_dir / "case.json"
    if not spec_path.is_file():
        return None
    try:
        with spec_path.open(encoding="utf-8-sig") as fh:
            spec = json.load(fh)
    except (OSError, ValueError) as exc:
        spec = {"_loadError": str(exc)}
    spec["_dir"] = case_dir
    spec["_path"] = str(case_dir.relative_to(case_dir.parents[1])).replace("\\", "/")
    return spec


def discover_cases(cases_root: Path, filter_substr: str | None) -> list[dict]:
    found = []
    for spec_path in sorted(cases_root.rglob("case.json")):
        case = load_case(spec_path.parent)
        if case is None:
            continue
        if filter_substr and filter_substr not in case["_path"]:
            continue
        found.append(case)
    return found


def prepare_tmp(fixture: Path, name: str) -> Path:
    tmp_root = os.environ.get("TMPDIR_CP3") or tempfile.gettempdir() or "."
    suffix = f"-{os.getpid()}-{time.time_ns() % 1000000000}"
    tmp = Path(tmp_root) / f"cp3-pak-{name.replace('/', '__')}{suffix}"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    shutil.copytree(
        fixture,
        tmp,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns(".cp3", ".git", "node_modules", "case.json"),
    )
    return tmp


def run_cp3c(cp3c: str, args: list[str], cwd: Path, timeout: float):
    return subprocess.run(
        [cp3c] + args,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        text=True,
        errors="replace",
    )


def _file_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return ""


def assert_step(step: dict, proc, tmp: Path, cwd: Path, prefix: str,
                check_files: bool = True) -> list[str]:
    failures: list[str] = []

    def fail(msg: str) -> None:
        failures.append(f"{prefix}{msg}")

    expect_exit = step.get("expectExit")
    if expect_exit is not None and proc.returncode != int(expect_exit):
        tail = (proc.stderr.strip() or proc.stdout.strip()).splitlines()
        detail = tail[-1] if tail else "(no output)"
        crash_hex = ""
        unsigned = proc.returncode & 0xFFFFFFFF
        if proc.returncode < 0 or proc.returncode > 0x7FFFFFFF:
            crash_hex = f" (0x{unsigned:08X})"
        fail(f"exit code {proc.returncode}{crash_hex}, expected {expect_exit} "
             f"(last: {detail})")

    for needle in step.get("stdoutContains", []):
        if needle not in proc.stdout:
            fail(f"stdout missing {needle!r}")
    for needle in step.get("stdoutNotContains", []):
        if needle in proc.stdout:
            fail(f"stdout unexpectedly contains {needle!r}")
    for needle in step.get("stderrContains", []):
        if needle not in proc.stderr:
            fail(f"stderr missing {needle!r}")
    for needle in step.get("stderrNotContains", []):
        if needle in proc.stderr:
            fail(f"stderr unexpectedly contains {needle!r}")
    for rel in step.get("filesExist", []) if check_files else ():
        if not (cwd / rel).exists():
            fail(f"expected file {rel} does not exist")
    for rel in step.get("filesAbsent", []) if check_files else ():
        if (cwd / rel).exists():
            fail(f"file {rel} should not exist")
    for probe in step.get("fileContains", []) if check_files else ():
        text = _file_text(cwd / probe["path"])
        if probe["contains"] not in text:
            fail(f"{probe['path']} does not contain {probe['contains']!r}")
    for probe in step.get("fileNotContains", []) if check_files else ():
        text = _file_text(cwd / probe["path"])
        if probe["contains"] in text:
            fail(f"{probe['path']} unexpectedly contains {probe['contains']!r}")
    return failures


def run_case(case: dict, cp3c: str, timeout: float) -> tuple[str, list[str]]:
    case_dir = Path(case["_dir"])
    name = case.get("name") or case["_path"]
    steps = case.get("steps")
    if not isinstance(steps, list) or not steps:
        return "FAIL", [f"{name}: the case has no steps (infra)"]
    tmp = prepare_tmp(case_dir, case["_path"])
    failures: list[str] = []
    try:
        for index, step in enumerate(steps, start=1):
            cwd_rel = step.get("cwd", ".")
            cwd = (tmp / cwd_rel).resolve()
            if not cwd.is_dir():
                failures.append(f"{name}: step {index}: no directory {cwd_rel!r} (infra)")
                break
            args = step.get("args", [])
            repeat = step.get("repeat", 1)
            if not isinstance(repeat, int) or repeat < 1:
                failures.append(f"{name}: step {index}: 'repeat' must be an "
                                f"integer >= 1 (infra)")
                break
            joined_args = " ".join(args)
            for iteration in range(1, repeat + 1):
                try:
                    proc = run_cp3c(cp3c, args, cwd, timeout)
                except subprocess.TimeoutExpired:
                    failures.append(f"{name}: step {index} iteration "
                                    f"{iteration}/{repeat}: timed out after {timeout}s")
                    continue
                except OSError as exc:
                    failures.append(f"{name}: step {index} iteration "
                                    f"{iteration}/{repeat}: could not run "
                                    f"{cp3c}: {exc}")
                    continue
                prefix = f"{name}: step {index} ({joined_args})"
                if repeat > 1:
                    prefix += f" iteration {iteration}/{repeat}"
                failures += assert_step(
                    step, proc, tmp, cwd, prefix + ": ",
                    check_files=(iteration == repeat))
            if failures:
                break
        return ("FAIL", failures) if failures else ("PASS", [])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="v0.2.5 packaging corpus")
    parser.add_argument("--cp3c", default=None)
    parser.add_argument("--cases", default=str(DEFAULT_CASES))
    parser.add_argument("--filter", dest="filter_substr", default=None)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--json", default=None, metavar="PATH")
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args(argv)

    cases_root = Path(args.cases)
    if not cases_root.is_dir():
        print(f"package-test: cases directory not found: {cases_root}", file=sys.stderr)
        return 2

    cases = discover_cases(cases_root, args.filter_substr)
    if args.list:
        for case in cases:
            print(case["_path"])
        return 0

    cp3c = args.cp3c or default_cp3c()
    print(f"package-test runner; compiler: {cp3c}")
    print(f"cases: {len(cases)} found")

    results = []
    for case in cases:
        status, failures = run_case(case, cp3c, args.timeout)
        results.append((status, case["_path"], failures))
        if args.verbose or status == "FAIL":
            print(f"  {status} {case['_path']}")
            for failure in failures:
                print(f"         {failure}")

    passed = sum(1 for r in results if r[0] == "PASS")
    failed = [r for r in results if r[0] == "FAIL"]
    print(f"package-test: {passed} passed, {len(failed)} failed, {len(results)} total")

    if args.json:
        payload = [{"name": r[1], "status": r[0], "failures": r[2]} for r in results]
        Path(args.json).write_text(json.dumps(payload, indent=2), encoding="utf-8")

    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
