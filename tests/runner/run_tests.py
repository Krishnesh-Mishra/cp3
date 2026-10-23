#!/usr/bin/env python3
# =============================================================================
# tests/runner/run_tests.py - the cp3c test harness.
#
# Discovers every tests/cases/**/<name>.expect.json, pairs it with the sibling
# <name>.cp3, runs cp3c, and compares the result against the frozen schema
# documented in tests/cases/README.md.
#
# Designed to be safe while cp3c is still a scaffold: a compile command that
# exits with code 3 and prints the "not implemented" marker is reported as
# SKIPPED, never a crash. When nothing is runnable the harness exits 0.
#
# Cases living under a `_ambiguous/` directory are *open-question markers*, not
# assertions (see docs/rough/DECISIONS.md, "Harness policy for `_ambiguous/` cases"). They
# are discovered and reported in their own `ambiguous` bucket with the question
# text pulled from `error.contains`, and are never counted as pass/fail and
# never affect the exit code. Pass --strict-ambiguous to run them as ordinary
# cases instead (used when a resolution lands and a marker is promoted into its
# parent category).
#
# Usage:
#   python tests/runner/run_tests.py [--cp3c PATH] [--cases DIR]
#                                    [--filter SUBSTR] [--json] [--verbose]
#                                    [--strict-ambiguous]
#                                    [--write-registry [PATH]]
#                                    [--no-bench] [--bench-args "ARGS"]
#                                    [--jobs N]
#
# --jobs N runs the corpus pass with up to N cp3c subprocesses in flight at
# once (stdlib only, ThreadPoolExecutor plus a small dispatcher). The default
# is one per logical CPU minus one; --jobs 1 is the unchanged sequential path
# and any N >= 1 is accepted. Each case still runs exactly the same command
# with the same cwd and timeout, and per-case outcomes
# are buffered and printed in the same order the sequential runner prints them,
# so the per-case lines, the `_ambiguous/` listing and the summary stay
# deterministic and each case's output is attributable to that case. Run-mode
# cases are parallelised across project roots only: cp3c writes its build
# intermediates (shared runtime objects, build manifest) into
# `<projectRoot>/.cp3/`, so two building cases from the same root are never in
# flight together (that would race on those files); check-mode cases are
# frontend-only and never occupy a root, and different roots are fully
# independent. The bench pass stays sequential
# (`benchmarks/run_benchmark.py` is a pinned measurement suite), so --jobs only
# affects the corpus pass.
#
# After the corpus pass the cross-language benchmark suite
# (benchmarks/run_benchmark.py) runs automatically (D67), streams its output,
# and re-prints the performance-gate WARN / BIG WARN lines at the end.
# --no-bench skips the bench pass; --bench is still accepted as an explicit
# (now redundant) request; --json skips the bench pass with a note.
#
# Exit code: 0 when no case failed (all passed or all skipped), 1 otherwise.
# =============================================================================
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# --- Exit codes shared with src/cli/include/cp3/cli.hpp ----------------------
STUB_EXIT_CODE = 3
STUB_MARKER = "not implemented"

# --- Open-question markers ---------------------------------------------------
# Any path with this directory segment is a marker, not an assertion.
AMBIGUOUS_DIR = "_ambiguous"
# The question text is carried verbatim in `error.contains`, prefixed with
# "AMBIGUOUS:". Strip that prefix for display/registry purposes.
_AMBIGUOUS_PREFIX = re.compile(r"^\s*AMBIGUOUS\s*[:\-]\s*", re.IGNORECASE)
DEFAULT_REGISTRY = "AMBIGUITIES.md"

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
    # more wildcard characters is treated as "any run" - this is what makes the
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


def is_ambiguous_parts(parts: tuple[str, ...]) -> bool:
    """True when a relative path contains an exact `_ambiguous` segment."""
    return AMBIGUOUS_DIR in parts


def extract_question(spec: dict) -> str:
    """Pull the open question out of a marker's `error.contains`.

    The spec-test agents wrote `{"error": {"contains": "AMBIGUOUS: <question>"}}`.
    Returns the question text with the `AMBIGUOUS:` prefix removed, or a
    descriptive placeholder when the marker is malformed.
    """
    err = spec.get("error")
    raw = ""
    if isinstance(err, dict) and err.get("contains") is not None:
        raw = str(err.get("contains"))
    elif isinstance(err, str):
        raw = err
    elif err is not None:
        raw = str(err)
    text = _AMBIGUOUS_PREFIX.sub("", raw).strip()
    return text or "(no question text)"


def _md_cell(text: str) -> str:
    """Make text safe for a single Markdown table cell."""
    return " ".join(text.split()).replace("|", "\\|")


def render_registry(markers: list["Case"], cases_dir: Path) -> str:
    """Render the AMBIGUITIES.md registry, grouped by category."""
    groups: dict[str, list[tuple[str, str]]] = {}
    for case in markers:
        try:
            rel = case.expect_path.relative_to(cases_dir).as_posix()
        except ValueError:  # pragma: no cover - defensive
            rel = case.expect_path.as_posix()
        # Category = the marker's parent folder chain minus the `_ambiguous` part.
        parent = [p for p in Path(rel).parts[:-1] if p != AMBIGUOUS_DIR]
        group = "/".join(parent) or "(root)"
        groups.setdefault(group, []).append((rel, case.question))

    out: list[str] = []
    out.append("# cp3c open spec questions (`_ambiguous/` registry)")
    out.append("")
    out.append(
        "Every file below is an **open-question marker**, not a conformance "
        "assertion: it records a point the spec does not pin down. The harness "
        "(`tests/runner/run_tests.py`) discovers them, reports them in a separate "
        "`ambiguous` bucket, and never counts them as pass or fail."
    )
    out.append("")
    out.append(
        "This file is generated. Regenerate it with:"
    )
    out.append("")
    out.append("```bash")
    out.append("python tests/runner/run_tests.py --write-registry")
    out.append("```")
    out.append("")
    out.append(f"**Total markers: {len(markers)}** across {len(groups)} categor"
               f"{'y' if len(groups) == 1 else 'ies'}.")
    out.append("")
    out.append("| Category | Markers |")
    out.append("| --- | ---: |")
    for group in sorted(groups):
        out.append(f"| `{group}` | {len(groups[group])} |")
    out.append(f"| **Total** | **{len(markers)}** |")
    out.append("")

    for group in sorted(groups):
        out.append(f"## `{group}`")
        out.append("")
        out.append("| case file | question |")
        out.append("| --- | --- |")
        for rel, question in sorted(groups[group]):
            out.append(f"| `{rel}` | {_md_cell(question)} |")
        out.append("")

    return "\n".join(out).rstrip() + "\n"


# --- Case model --------------------------------------------------------------
@dataclass
class Case:
    category: str
    name: str
    cp3_path: Path
    expect_path: Path
    spec: dict = field(default_factory=dict)
    ambiguous: bool = False
    question: str = ""

    @property
    def display(self) -> str:
        return f"{self.category}/{self.name}" if self.category else self.name


@dataclass
class Outcome:
    case: Case
    status: str  # "passed" | "failed" | "skipped" | "ambiguous"
    detail: str = ""


def discover_cases(cases_dir: Path, filter_substr: Optional[str]) -> tuple[list[Case], list[str]]:
    """Returns (cases, discovery_warnings). Never raises.

    A case is a folder that holds `main.cp3` (the program) and `output.json`
    (its expectation). The folder's name is the case's name; the folder chain
    above it is the case's category.
    """
    cases: list[Case] = []
    warnings: list[str] = []
    if not cases_dir.exists():
        warnings.append(f"cases directory not found: {cases_dir}")
        return cases, warnings

    # Two layouts are understood while the corpus migrates:
    #   <case>/main.cp3 + <case>/output.json            (the fixed-name layout)
    #   <name>.cp3 + <name>.expect.json                 (the flat layout)
    discovered: list[tuple[Path, str, Path]] = []
    for expect_path in sorted(cases_dir.rglob("output.json")):
        discovered.append((expect_path, expect_path.parent.name,
                           expect_path.parent / "main.cp3"))
    for expect_path in sorted(cases_dir.rglob("*.expect.json")):
        stem = expect_path.name[: -len(".expect.json")]
        discovered.append((expect_path, stem, expect_path.parent / (stem + ".cp3")))
    discovered.sort(key=lambda item: item[0].as_posix())

    for expect_path, stem, cp3_path in discovered:
        rel = expect_path.relative_to(cases_dir)
        rel_str = str(rel).replace("\\", "/")
        if filter_substr and filter_substr not in rel_str:
            continue

        category = str(rel.parent).replace("\\", "/")
        if category == ".":
            category = ""

        spec: dict = {}
        try:
            spec = json.loads(expect_path.read_text(encoding="utf-8-sig"))
            if not isinstance(spec, dict):
                warnings.append(f"{rel_str}: expected a JSON object")
                spec = {}
        except (OSError, ValueError) as exc:
            warnings.append(f"{rel_str}: could not read/parse expect JSON ({exc})")

        ambiguous = is_ambiguous_parts(rel.parts)
        question = extract_question(spec) if ambiguous else ""
        cases.append(
            Case(category, stem, cp3_path, expect_path, spec, ambiguous, question)
        )
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


def project_root_of(case: Case) -> Path:
    """The directory cp3c treats as the case's project root.

    Mirrors cp3::cli::findProjectRoot(): the nearest ancestor of the case file
    (starting with its own directory) that holds a package.json, or the case's
    own directory when there is none. cp3c writes its build intermediates to
    `<projectRoot>/.cp3/` (shared runtime objects, build manifest), so the
    parallel runner schedules at most one case per returned directory at a
    time.
    """
    first = case.cp3_path.parent
    probe = first
    while True:
        if (probe / "package.json").is_file():
            return probe
        parent = probe.parent
        if parent == probe:
            return first
        probe = parent


def run_benchmarks(extra_args: str) -> int:
    """Run benchmarks/run_benchmark.py, stream its output, re-print gate lines.

    The benchmark suite is a separate, long-running measurement pass: it builds
    and runs every language entry, so it runs by default after the corpus pass
    (D67) and --no-bench skips it. Its output is streamed live; WARN and
    BIG WARN lines from the performance gate are collected and re-printed in a
    compact block at the end so gate regressions cannot scroll away.
    extra_args is split shell-style; it is empty unless --bench-args was given.
    Returns the bench exit code (0 for a clean run, 1 for a gate/error failure).
    """
    script = REPO_ROOT / "benchmarks" / "run_benchmark.py"
    argv = [sys.executable, str(script)] + shlex.split(extra_args)
    print()
    print("== benchmarks ==")
    print(f"  command: {' '.join(argv)}")
    print()
    warnings: list[str] = []
    if not script.is_file():
        print(f"  error: {script} not found", file=sys.stderr)
        return 1
    try:
        process = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
    except OSError as exc:
        print(f"  error: could not start benchmarks ({exc})", file=sys.stderr)
        return 1
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="")
        if line.startswith(("WARN ", "BIG WARN ")):
            warnings.append(line.rstrip("\n"))
    exit_code = process.wait()
    print()
    if warnings:
        big = sum(1 for w in warnings if w.startswith("BIG WARN "))
        plain = len(warnings) - big
        print(f"  bench gate: {big} BIG WARN(s), {plain} WARN(s) "
              f"(performance gate, see benchmarks/README.md)")
        for warning in warnings:
            print(f"    {warning}")
    else:
        print("  bench gate: no warnings")
    if exit_code != 0:
        print(f"  bench suite exited with code {exit_code}")
    return exit_code


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
    parser.add_argument("--strict-ambiguous", dest="strict_ambiguous", action="store_true",
                        help="run `_ambiguous/` markers as ordinary cases (opt-in; they "
                             "then count as pass/fail and can affect the exit code)")
    parser.add_argument("--write-registry", dest="write_registry", nargs="?", const="",
                        default=None, metavar="PATH",
                        help="write the `_ambiguous/` marker registry and exit "
                             "(default: tests/cases/AMBIGUITIES.md)")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="per-case timeout in seconds (default: 60)")
    default_jobs = max(1, (os.cpu_count() or 1) - 1)
    parser.add_argument("--jobs", type=int, default=default_jobs, metavar="N",
                        help="run up to N cases concurrently during the corpus "
                             "pass (default: %(default)s, one per logical CPU "
                             "minus one; pass 1 for the strictly sequential "
                             "runner, or any larger N). Run-mode cases that "
                             "share a project root run one at a time (cp3c "
                             "builds into a shared .cp3/ there); check cases "
                             "and different roots run in parallel. Output "
                             "order and content do not depend on N; the bench "
                             "pass always runs sequentially.")
    parser.add_argument("--bench", action="store_true",
                        help="run the benchmark suite after the corpus pass (now the "
                             "default; kept as an explicit, redundant request)")
    parser.add_argument("--no-bench", dest="no_bench", action="store_true",
                        help="skip the benchmark suite pass (it runs by default "
                             "after the corpus pass)")
    parser.add_argument("--bench-args", dest="bench_args", default="", metavar="ARGS",
                        help="extra arguments forwarded to benchmarks/run_benchmark.py "
                             "when the bench pass runs (e.g. --only hello-world)")
    args = parser.parse_args(argv)

    if args.jobs < 1:
        parser.error("--jobs must be at least 1")

    cases_dir = Path(args.cases)
    cases, warnings = discover_cases(cases_dir, args.filter_substr)

    # --- registry generation (opt-in side effect, then exit) ------------------
    if args.write_registry is not None:
        markers = [c for c in cases if c.ambiguous]
        dest = Path(args.write_registry) if args.write_registry else cases_dir / DEFAULT_REGISTRY
        try:
            dest.write_text(render_registry(markers, cases_dir), encoding="utf-8")
        except OSError as exc:
            print(f"error: could not write registry to {dest} ({exc})", file=sys.stderr)
            return 1
        print(f"wrote {len(markers)} ambiguity marker(s) to {dest}")
        return 0

    compiler = _resolve_compiler(args.cp3c)
    compiler_missing = compiler is None or not compiler.is_file()
    missing_reason = "cp3c executable not found"

    def run_as_ordinary_serial(selected: list[Case]) -> list[Outcome]:
        sink: list[Outcome] = []
        for case in selected:
            if compiler_missing:
                sink.append(Outcome(case, "skipped", missing_reason))
            else:
                sink.append(evaluate(case, compiler, args.timeout))
        return sink

    def run_as_ordinary_parallel(selected: list[Case]) -> list[Outcome]:
        # Bounded pool, one cp3c subprocess per case - the same command, cwd
        # and timeout the sequential path uses. Cases are parallelised across
        # build roots: cp3c writes shared intermediates (runtime objects,
        # build manifest) into `<projectRoot>/.cp3/`, so two run-mode cases
        # from the same root must not overlap (ld would read an object file
        # while another cp3c writes it); `check` cases write nothing and never
        # occupy a root. Within a root run cases therefore run one after
        # another, exactly as in the sequential runner; different roots run
        # fully in parallel. Workers only return Outcome records (they never
        # print), and outcomes are collected per submission index, so the
        # emitted lines are byte-identical to a sequential run no matter which
        # case finishes first.
        count = len(selected)
        if compiler_missing:
            return [Outcome(case, "skipped", missing_reason) for case in selected]
        if args.jobs <= 1 or count <= 1:
            return run_as_ordinary_serial(selected)
        timeout = args.timeout
        # Only `run` cases involve the toolchain and the shared `<root>/.cp3/`
        # build directory; `check` cases are frontend-only (pure reads) and
        # never occupy a root. A `None` root therefore means "always free".
        root_cache: dict[Path, Path] = {}

        def root_of(case: Case) -> Path:
            parent = case.cp3_path.parent
            root = root_cache.get(parent)
            if root is None:
                root = project_root_of(case)
                root_cache[parent] = root
            return root

        roots = [
            root_of(case) if case.spec.get("mode", "check") == "run" else None
            for case in selected
        ]
        pending = list(range(count))
        busy_roots: set[Path] = set()
        inflight: dict = {}
        collected: dict[int, Outcome] = {}

        def start_more(pool: ThreadPoolExecutor) -> None:
            scan = 0
            while len(inflight) < args.jobs and scan < len(pending):
                index = pending[scan]
                root = roots[index]
                if root is not None and root in busy_roots:
                    scan += 1
                    continue
                pending.pop(scan)
                if root is not None:
                    busy_roots.add(root)
                future = pool.submit(evaluate, selected[index], compiler, timeout)
                inflight[future] = index

        with ThreadPoolExecutor(max_workers=min(args.jobs, count)) as pool:
            start_more(pool)
            while inflight:
                done, _ = wait(list(inflight), return_when=FIRST_COMPLETED)
                for future in done:
                    index = inflight.pop(future)
                    root = roots[index]
                    if root is not None:
                        busy_roots.discard(root)
                    collected[index] = future.result()
                start_more(pool)
        return [collected[i] for i in range(count)]

    def run_as_ordinary(selected: list[Case]) -> list[Outcome]:
        if args.jobs > 1:
            return run_as_ordinary_parallel(selected)
        return run_as_ordinary_serial(selected)

    if args.strict_ambiguous:
        # Opt-in: markers run like any other case (they may pass, fail, or skip).
        outcomes = run_as_ordinary(cases)
    else:
        # Default: markers are discovered and reported, never asserted against.
        outcomes = [Outcome(c, "ambiguous", c.question) for c in cases if c.ambiguous]
        outcomes += run_as_ordinary([c for c in cases if not c.ambiguous])

    passed = sum(1 for o in outcomes if o.status == "passed")
    failed = sum(1 for o in outcomes if o.status == "failed")
    skipped = sum(1 for o in outcomes if o.status == "skipped")
    ambiguous = sum(1 for o in outcomes if o.status == "ambiguous")

    if args.json:
        payload = {
            "compiler": str(compiler) if compiler else None,
            "casesDir": str(cases_dir),
            "strictAmbiguous": args.strict_ambiguous,
            "found": len(cases),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "ambiguous": ambiguous,
            "runnable": passed + failed > 0,
            "warnings": warnings,
            "results": [
                {
                    "case": o.case.display,
                    "status": o.status,
                    "detail": o.detail,
                    **({"question": o.case.question} if o.status == "ambiguous" else {}),
                }
                for o in outcomes
            ],
        }
        json.dump(payload, sys.stdout, indent=2)
        sys.stdout.write("\n")
        if not args.no_bench:
            print("note: bench pass skipped with --json (JSON output stays parseable)",
                  file=sys.stderr)
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
        elif outcome.status == "ambiguous":
            # Always reported: this is the lead's worklist of open questions.
            print(f"  amb  {c}: {outcome.detail}")
        else:  # skipped
            if args.verbose or (passed + failed == 0):
                print(f"  skip {c} ({outcome.detail})")

    print()
    summary = (f"{len(cases)} found, {passed} passed, {failed} failed, "
               f"{skipped} skipped, {ambiguous} ambiguous")
    if passed + failed == 0:
        print(f"{summary}; nothing runnable yet, treating as success while cp3c "
              f"is a scaffold.")
    else:
        print(f"{summary}.")

    exit_code = 0 if failed == 0 else 1
    if not args.no_bench:
        bench_exit = run_benchmarks(args.bench_args)
        exit_code = max(exit_code, bench_exit)
    return exit_code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:  # pragma: no cover
        sys.exit(130)
