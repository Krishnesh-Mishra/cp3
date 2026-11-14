#!/usr/bin/env python3
# =============================================================================
# tests/v0.2.5/runner.py - the v0.2.5 dev-toolchain corpus.
#
# Each case is a fixture project (package.json + src/*.cp3) plus a `case.json`
# expectation. The fixture is copied to a unique temp directory - the checked-
# in fixture is never dirtied - and `cp3c` runs there with the case's argv.
# The runner asserts exit code, stdout/stderr content and artifact existence.
#
# Sequential by default (dev-run cases spawn watchers/children); `--jobs N`
# only schedules cases that set "parallelSafe": true. e2e cases (real watch
# loops, ports, timing) are skipped unless `--e2e` is passed.
#
# Usage:
#   python tests/v0.2.5/runner.py [--cp3c PATH] [--cases DIR] [--filter SUBSTR]
#       [--list] [--verbose] [--json PATH] [--e2e] [--jobs N] [--timeout S]
# Exit codes: 0 all pass | 1 any failure | 2 infra error
# =============================================================================
import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_CASES = REPO_ROOT / "tests" / "v0.2.5" / "cases"


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
    import time
    tmp_root = os.environ.get("TMPDIR_CP3") or tempfile.gettempdir() or "."
    suffix = f"-{os.getpid()}-{time.time_ns() % 1000000000}"
    tmp = Path(tmp_root) / f"cp3-v025-{name.replace('/', '__')}{suffix}"
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


def run_cp3c(cp3c: str, args: list[str], cwd: Path, env: dict, timeout: float):
    full_env = dict(os.environ)
    full_env.update(env)
    return subprocess.run(
        [cp3c] + args,
        cwd=str(cwd),
        env=full_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        text=True,
        errors="replace",
    )


def apply_edits(case_dir: Path, tmp: Path) -> None:
    edits = case_dir / "edits"
    if not edits.is_dir():
        return
    for edit in edits.rglob("*"):
        if not edit.is_file():
            continue
        rel = edit.relative_to(edits)
        target = tmp / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(edit, target)


def assert_spec(case: dict, proc, tmp: Path, prefix: str) -> list[str]:
    failures = []

    def fail(msg):
        failures.append(f"{prefix}{msg}")

    expect_exit = case.get("expectExit")
    if expect_exit is not None and proc.returncode != expect_exit:
        fail(f"exit code {proc.returncode}, expected {expect_exit}")
    for needle in case.get("stdoutContains", []):
        if needle not in proc.stdout:
            fail(f"stdout missing {needle!r}")
    for needle in case.get("stdoutNotContains", []):
        if needle in proc.stdout:
            fail(f"stdout unexpectedly contains {needle!r}")
    for needle in case.get("stderrContains", []):
        if needle not in proc.stderr:
            fail(f"stderr missing {needle!r}")
    for needle in case.get("stderrNotContains", []):
        if needle in proc.stderr:
            fail(f"stderr unexpectedly contains {needle!r}")
    for rel in case.get("filesExist", []):
        if not (tmp / rel).exists():
            fail(f"expected file {rel} does not exist")
    for rel in case.get("filesAbsent", []):
        if (tmp / rel).exists():
            fail(f"file {rel} should not exist")
    if case.get("exeExists"):
        # .cp3/out/ has exactly one .exe in each fixture shape.
        out_dir = tmp / ".cp3" / "out"
        if not out_dir.is_dir() or not list(out_dir.glob("*.exe")):
            fail("no executable was produced under .cp3/out")
    if case.get("moduleLibsAbsent"):
        out_dir = tmp / ".cp3" / "out"
        dlls = list(out_dir.glob("*.dll")) if out_dir.is_dir() else []
        if dlls:
            fail(f"module-libs DLLs present but not wanted: {[d.name for d in dlls]}")
    return failures


def run_case(case: dict, cp3c: str) -> tuple[str, list[str], subprocess.CompletedProcess | None]:
    case_dir = Path(case["_dir"])
    name = case.get("name") or case["_path"]
    scenario = case.get("scenario", "fresh")
    timeout = float(case.get("timeoutSeconds", 60.0))
    args = case.get("args", [])
    env = dict(case.get("env", {}))
    prefix = f"{name}: "
    tmp = prepare_tmp(case_dir, case["_path"])
    try:
        pass
        if scenario in ("fresh", "dev-once"):
            proc = run_cp3c(cp3c, args, tmp, env, timeout)
            pass_n = 1
        elif scenario in ("rebuild", "incremental", "module-libs"):
            first = run_cp3c(cp3c, args, tmp, env, timeout)
            if scenario == "incremental":
                apply_edits(case_dir, tmp)
            rebuild_args = case.get("rebuildArgs", args)
            proc = run_cp3c(cp3c, rebuild_args, tmp, env, timeout)
            pass_n = 2
        elif scenario == "monolithic-compare":
            # Build twice: monolithic env unset vs CP3_DEV_MODULE_LIBS=1,
            # assert the program's stdout is identical both times.
            tmp_a = prepare_tmp(case_dir, name + "__monolithic")
            proc_a = run_cp3c(cp3c, args, tmp_a, {}, timeout)
            tmp_b = prepare_tmp(case_dir, name + "__modulelibs")
            proc_b = run_cp3c(cp3c, args, tmp_b,
                              {"CP3_DEV_MODULE_LIBS": "1"}, timeout)
            if proc_a.stdout != proc_b.stdout:
                shutil.rmtree(tmp_a, ignore_errors=True)
                shutil.rmtree(tmp_b, ignore_errors=True)
                return (
                    "FAIL",
                    [f"{prefix}monolithic stdout ({proc_a.stdout!r}) != "
                     f"module-libs stdout ({proc_b.stdout!r})"],
                    proc_b,
                )
            shutil.rmtree(tmp_a, ignore_errors=True)
            proc = proc_b
            tmp = tmp_b
            pass_n = 2
        else:
            return ("FAIL", [f"unknown scenario {scenario!r} (infra)"], None)

        failures = assert_spec(case, proc, tmp, prefix)
        # Extra guard for the crash class this suite exists to catch.
        for banned in ("terminate called without an active exception",):
            if banned in proc.stderr:
                failures.append(f"{prefix}stderr contains {banned!r}")
        if failures:
            return ("FAIL", failures, proc)
        return ("PASS", [], proc)
    except subprocess.TimeoutExpired:
        return ("FAIL", [f"{prefix}timed out after {timeout}s"], None)
    except OSError as exc:
        return ("FAIL", [f"{prefix}could not run {cp3c}: {exc}"], None)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="v0.2.5 dev-toolchain corpus")
    parser.add_argument("--cp3c", default=None)
    parser.add_argument("--cases", default=str(DEFAULT_CASES))
    parser.add_argument("--filter", dest="filter_substr", default=None)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--json", default=None, metavar="PATH")
    parser.add_argument("--e2e", action="store_true",
                        help="run requiresE2E cases (real watch loop)")
    parser.add_argument("--jobs", type=int, default=1,
                        help="parallel workers for parallelSafe cases only")
    parser.add_argument("--timeout", type=float, default=None)
    args = parser.parse_args(argv)

    cases_root = Path(args.cases)
    if not cases_root.is_dir():
        print(f"v0.2.5: cases directory not found: {cases_root}", file=sys.stderr)
        return 2

    cases = discover_cases(cases_root, args.filter_substr)
    if args.list:
        for case in cases:
            print(case["_path"])
        return 0

    cp3c = args.cp3c or default_cp3c()
    print(f"v0.2.5 runner; compiler: {cp3c}")
    print(f"cases: {len(cases)} found" + (" (e2e required)" if args.e2e else ""))

    results = []
    for case in cases:
        if case.get("requiresE2E") and not args.e2e:
            results.append(("SKIP", case["_path"], ["requires --e2e"]))
            continue
        status, failures, proc = run_case(case, cp3c)
        results.append((status, case["_path"], failures))
        if args.verbose or status == "FAIL":
            print(f"  {status} {case['_path']}")
            for failure in failures:
                print(f"         {failure}")
            if proc is not None:
                tail = "\n".join(proc.stdout.splitlines()[-4:] + proc.stderr.splitlines()[-4:])
                if tail:
                    print(f"         --- output tail ---\n{tail}\n         -----------------")

    passed = sum(1 for r in results if r[0] == "PASS")
    skipped = sum(1 for r in results if r[0] == "SKIP")
    failed = [r for r in results if r[0] == "FAIL"]
    print(f"v0.2.5: {passed} passed, {len(failed)} failed, {skipped} skipped, "
          f"{len(results)} total")

    if args.json:
        payload = [
            {"name": r[1], "status": r[0], "failures": r[2]} for r in results
        ]
        Path(args.json).write_text(
            json.dumps(payload, indent=2), encoding="utf-8")

    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
