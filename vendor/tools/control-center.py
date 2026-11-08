#!/usr/bin/env python3
# =============================================================================
# vendor/tools/control-center.py - the cp3 control center.
#
# One entry point for the whole repository workflow. Run it on a fresh clone
# and it first checks the machine (vendor/tools/check-prerequisites.py),
# telling you - and, with consent, configuring - anything that is missing.
# Then it asks what you want to do:
#
#   > Build / package targets    build the compiler and package it
#                                (vendor/tools/build-matrix.py, same checklist)
#   > Run the test corpus        run the test suite once (tests/runner/run_tests.py),
#                                optionally limited to a folder and with a jobs count
#   > Run benchmarks             checkbox-select the benchmarks, set the repetitions
#                                (benchmarks/runner/run_suite.py)
#   > Doctor / initial setup     re-run the prerequisite check and repair flow
#   > Quit
#
# Everything is also scriptable: pass the matching flag instead of navigating
# the menu, e.g.
#
#   python vendor/tools/control-center.py --build --targets windows-x64,linux-x64 --yes
#   python vendor/tools/control-center.py --test --filter tls --jobs 8
#   python vendor/tools/control-center.py --bench --only hello-world,sha256-churn --reps 5
#
# Usage:
#   python vendor/tools/control-center.py            interactive (runs the check first)
#   python vendor/tools/control-center.py --help     all flags
# =============================================================================
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]            # repository root
VERSION = (ROOT / "version.txt").read_text(encoding="utf-8").strip()

IS_WINDOWS = os.name == "nt"

# Verified-against-run_suite.py-2026-11 name list, only used if the --help probe fails.
FALLBACK_BENCHMARKS = (
    "health-server,health-server-multi,compute-single,compute-single-int,compute-multi,"
    "hello-world,string-churn,sha256-churn,buffer-copy,typed-array-sum,math-kernel,"
    "word-frequency,object-churn,array-growth,closure-capture,interface-dispatch,"
    "generics-mono,json-roundtrip,async-fanout,env-injection,file-roundtrip,path-join,"
    "date-format,os-probe,binary-trees,map-churn,utf8-text,regex-scan,json-parse-large,"
    "deep-recursion,exception-cost,file-big,tree-walk,graph-bfs,strlist,lru,interp,blur,"
    "calc,particles,push,records,scene,sexp,respond,sum-array"
).split(",")

# --------------------------------------------------------------------------
# small helpers (kept in sync with build-matrix.py / check-prerequisites.py)
# --------------------------------------------------------------------------

def _python() -> str:
    return sys.executable or "python"


def _tool(name: str) -> Path:
    return ROOT / "vendor" / "tools" / name


def _run(cmd: list[str]) -> int:
    print("  $ " + " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, cwd=str(ROOT)).returncode


def _run_tool(script: str, args: list[str]) -> int:
    return _run([_python(), str(_tool(script))] + args)


def _load_benchmark_names() -> list[str]:
    try:
        proc = subprocess.run([_python(), str(ROOT / "benchmarks" / "runner" / "run_suite.py"),
                               "--help"],
                              capture_output=True, text=True, cwd=str(ROOT), timeout=30)
        text = (proc.stdout or "") + (proc.stderr or "")
        match = re.search(r"--only ONLY\s+comma list of:\s*(.*?)\n\s*--", text, re.DOTALL)
        if match:
            # The runner wraps the list mid-token and indents continuation
            # lines; benchmarks never contain spaces, so remove all whitespace.
            chunk = re.sub(r"\s+", "", match.group(1))
            names = [part.strip() for part in chunk.split(",") if part.strip()]
            if names and len(names) > 2:
                return names
    except (OSError, subprocess.TimeoutExpired):
        pass
    return FALLBACK_BENCHMARKS


# --------------------------------------------------------------------------
# terminal UI (arrow keys + checkbox lists)
# --------------------------------------------------------------------------

def _read_key():
    if os.name == "nt":
        import msvcrt

        def read():
            ch = msvcrt.getwch()
            if ch == "\x03":
                raise KeyboardInterrupt
            if ch in ("\r", "\n"):
                return "enter"
            if ch == " ":
                return "space"
            if ch in ("\x00", "\xe0"):
                # Windows extended key: getwch() returns chr(0) or chr(224) first,
                # then one scan-code letter ('H'=72 up, 'P'=80 down, 'K'=75 left,
                # 'M'=77 right, 'G'=71 home, 'O'=79 end)
                scan = msvcrt.getwch()
                return {"H": "up", "P": "down", "K": "left", "M": "right",
                        "G": "home", "O": "end"}.get(scan, "unknown")
            if ch == "\x1b":
                second = msvcrt.getwch()
                if second == "[":
                    third = msvcrt.getwch()
                    return {"A": "up", "B": "down", "H": "home"}.get(third, "unknown")
                return "unknown"
            return ch
        return read

    import termios
    import tty

    def read():
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
            if ch == "\x03":
                raise KeyboardInterrupt
            if ch in ("\r", "\n"):
                return "enter"
            if ch == " ":
                return "space"
            if ch == "\x1b":
                seq = sys.stdin.read(2)
                return {"[A": "up", "[B": "down", "[H": "home"}.get(seq, "unknown")
            return ch
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
    return read


def _is_interactive() -> bool:
    return sys.stdin.isatty() and sys.stdout.isatty()


def choose_one(title: str, options: list[str]) -> int | None:
    """Bullet selector. Returns the chosen index, or None on quit/EOF."""
    if not _is_interactive():
        return 0
    cursor = 0
    read = _read_key()
    try:
        while True:
            lines = [title, "up/down = move   Enter = select   q = quit", ""]
            for i, option in enumerate(options):
                caret = ">" if i == cursor else " "
                lines.append(f"{caret} {option}")
            sys.stdout.write("\x1b[2J\x1b[H" + "\n".join(lines) + "\n")
            sys.stdout.flush()
            key = read()
            if key == "up":
                cursor = (cursor - 1) % len(options)
            elif key == "down":
                cursor = (cursor + 1) % len(options)
            elif key == "home":
                cursor = 0
            elif key == "q":
                return None
            elif key == "enter":
                return cursor
    except KeyboardInterrupt:
        return None


def choose_many(title: str, options: list[str], preselect: bool = True) -> list[int] | None:
    """Checkbox selector. Returns the chosen indices (sorted), None on quit/EOF."""
    if not _is_interactive():
        return list(range(len(options))) if preselect else []
    selected = set(range(len(options))) if preselect else set()
    cursor = 0
    read = _read_key()
    try:
        while True:
            lines = [title,
                     "space = toggle   a = all   n = none   Enter = confirm   q = quit",
                     ""]
            for i, option in enumerate(options):
                mark = "[x]" if i in selected else "[ ]"
                caret = ">" if i == cursor else " "
                lines.append(f"{caret} {mark} {option}")
            lines.append("")
            lines.append(f"{len(selected)} of {len(options)} selected")
            sys.stdout.write("\x1b[2J\x1b[H" + "\n".join(lines) + "\n")
            sys.stdout.flush()
            key = read()
            if key == "up":
                cursor = (cursor - 1) % len(options)
            elif key == "down":
                cursor = (cursor + 1) % len(options)
            elif key == "home":
                cursor = 0
            elif key == "space":
                if cursor in selected:
                    selected.remove(cursor)
                else:
                    selected.add(cursor)
            elif key == "a":
                selected = set(range(len(options)))
            elif key == "n":
                selected = set()
            elif key == "q":
                return None
            elif key == "enter":
                return sorted(selected)
    except KeyboardInterrupt:
        return None


def ask(prompt: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    try:
        answer = input(f"{prompt}{suffix}: ").strip()
    except EOFError:
        answer = ""
    return answer if answer else default


# --------------------------------------------------------------------------
# the phases
# --------------------------------------------------------------------------

def run_doctor(args) -> int:
    cmd = [_python(), str(_tool("check-prerequisites.py"))]
    if args.report:
        cmd.append("--report")
    elif args.yes:
        cmd.append("--yes")
    return _run(cmd)


def run_build(args) -> int:
    cmd = [_python(), str(_tool("build-matrix.py"))]
    if args.targets:
        cmd += ["--targets", args.targets]
    if args.yes:
        cmd.append("--yes")
    return _run(cmd)


def _wsl_prefix() -> str | None:
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        return None
    return wsl


def run_tests(args) -> int:
    cmd = [_python(), str(ROOT / "tests" / "runner" / "run_tests.py")]
    if args.filter:
        cmd += ["--filter", args.filter]
    if args.cases:
        cmd += ["--cases", args.cases]
    if args.jobs:
        cmd += ["--jobs", args.jobs]
    if args.timeout:
        cmd += ["--timeout", args.timeout]
    if args.json:
        cmd.append("--json")
    if args.no_bench:
        cmd.append("--no-bench")
    if args.platform == "linux":
        prefix = _wsl_prefix()
        if not prefix:
            print("no WSL on this host; use --platform windows (or run from Linux/WSL directly)")
            return 2
        # Lay the same arguments back onto python3 inside the repo mounted at /mnt/c/...
        repo = _wsl_path()
        cases_arg = _wsl_argv(args.cases) if args.cases else None
        linux_cmd = ["bash", "-lc", f"cd {repo} && python3 tests/runner/run_tests.py"
                     + (" --filter '" + _wsl_argv(args.filter) + "'" if args.filter else "")
                     + (" --cases '" + cases_arg + "'" if cases_arg else "")
                     + (" --jobs " + args.jobs if args.jobs else "")
                     + (" --timeout " + args.timeout if args.timeout else "")
                     + (" --json" if args.json else "")
                     + (" --no-bench" if args.no_bench else "")]
        print("  $ wsl bash -lc \"cd " + repo + " && python3 tests/runner/run_tests.py ...\"")
        return subprocess.run([prefix] + linux_cmd, cwd=str(ROOT)).returncode
    return _run(cmd)


def _wsl_argv(path: str) -> str:
    # convert C:\Users\... or C:/Users/... to /mnt/c/Users/... (cygpath-style)
    import re
    m = re.match(r"^([A-Za-z]):[\\/]", path)
    if m:
        rest = path[2:].replace("\\", "/")
        return f"/mnt/{m.group(1).lower()}{rest}"
    if path.startswith("/mnt/") or path.startswith("/"):
        return path
    return path.replace("\\", "/")


def _wsl_path() -> str:
    # convert C:\Users\... repo path to /mnt/<drive>/... for bash -lc.
    import re
    m = re.match(r"^([A-Za-z]):[\\/]", str(ROOT))
    if not m:
        return str(ROOT).replace("\\", "/")
    rest = str(ROOT)[2:].replace("\\", "/")
    return f"/mnt/{m.group(1).lower()}{rest}"


def run_benchmarks(args) -> int:
    if args.platform == "linux":
        prefix = _wsl_prefix()
        if not prefix:
            print("no WSL on this host; use --platform windows (or run from Linux/WSL directly)")
            return 2
        repo = _wsl_path()
        only_flag = f"--only '{args.only}'" if args.only else ""
        except_flag = f"--except '{args.except_}'" if args.except_ else ""
        reps_flag = f"--reps {args.reps}" if args.reps else ""
        seconds_flag = f"--seconds {args.seconds}" if args.seconds else ""
        concurrency_flag = f"--concurrency {args.concurrency}" if args.concurrency else ""
        clients_flag = f"--clients {args.clients}" if args.clients else ""
        results_flag = f"--results '{args.results}'" if args.results else ""
        strict_flag = "--strict" if args.strict else ""
        linux_cmd = ["bash", "-lc", f"cd {repo} && python3 benchmarks/runner/run_suite.py "
                     + " ".join(filter(None, [only_flag, except_flag, reps_flag, seconds_flag,
                                                concurrency_flag, clients_flag, results_flag, strict_flag]))]
        print("  $ wsl bash -lc \"cd " + repo + " && python3 benchmarks/runner/run_suite.py ...\"")
        return subprocess.run([prefix] + linux_cmd, cwd=str(ROOT)).returncode
    cmd = [_python(), str(ROOT / "benchmarks" / "runner" / "run_suite.py")]
    if args.only:
        cmd += ["--only", args.only]
    if args.except_:
        cmd += ["--except", args.except_]
    if args.reps:
        cmd += ["--reps", args.reps]
    if args.seconds:
        cmd += ["--seconds", args.seconds]
    if args.concurrency:
        cmd += ["--concurrency", args.concurrency]
    if args.clients:
        cmd += ["--clients", args.clients]
    if args.results:
        cmd += ["--results", args.results]
    if args.strict:
        cmd.append("--strict")
    return _run(cmd)


def interactive_tests(args) -> int:
    print("Run the test corpus once.")
    platform = _ask_platform()
    folder = ask("case folder (a directory path, or a substring to filter - empty = everything)")
    job_spec = ask("parallel jobs (empty = default, one per logical CPU)")
    eff = argparse.Namespace(filter=None, cases=None, jobs=None,
                             timeout=None, json=False, no_bench=True, platform=platform)
    if folder:
        candidate = Path(folder)
        if candidate.is_dir():
            eff.cases = str(candidate)
        else:
            eff.filter = folder
    if job_spec:
        eff.jobs = job_spec
    return run_tests(eff)


def _ask_platform() -> str:
    if not _is_interactive() or _wsl_prefix() is None:
        return "windows"
    options = ["Windows", "Linux (via WSL)"]
    choice = choose_one("where to run?", options)
    if choice is None:
        return "windows"
    return "windows" if choice == 0 else "linux"


def interactive_benchmarks(args) -> int:
    names = _load_benchmark_names()
    platform = _ask_platform()
    print(f"{len(names)} benchmarks available.")
    picked = choose_many("cp3 benchmarks - what should run?", names, preselect=False)
    if picked is None or not picked:
        print("aborted." if picked is None else "nothing selected.")
        return 1 if picked is None else 0
    eff = argparse.Namespace(only=",".join(names[i] for i in picked),
                             except_=None, reps=None, seconds=None,
                             concurrency=None, clients=None, results=None,
                             strict=False, platform=platform)
    eff.reps = ask("independent repetitions per test", "3")
    seconds = ask("throughput phase seconds per server (empty = default)")
    if seconds:
        eff.seconds = seconds
    strict = ask("exit non-zero when the gate WARNs? [y/N]").lower()
    eff.strict = strict in ("y", "yes")
    return run_benchmarks(eff)


MENU = [
    ("Build / package targets", "build the compiler and package installers (build-matrix)"),
    ("Run the test corpus", "tests once, optionally a folder / jobs count"),
    ("Run benchmarks", "checkbox-select benchmarks, set repetitions"),
    ("Doctor / initial setup", "verify clang, libs, everything - configure what is missing"),
    ("Quit", ""),
]


def run_menu(args) -> int:
    while True:
        choice = choose_one(f"cp3 control center v{VERSION}",
                            [f"{name}" for name, _ in MENU])
        if choice is None or MENU[choice][0] == "Quit":
            print("bye.")
            return 0
        if choice == 0:
            rc = run_build(args)
        elif choice == 1:
            rc = interactive_tests(args)
        elif choice == 2:
            rc = interactive_benchmarks(args)
        elif choice == 3:
            rc = run_doctor(args)
        input("\npress Enter to return to the menu ...")
        print()


# --------------------------------------------------------------------------
# entry point
# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="cp3 control center: check the host, build, test, benchmark")

    action = parser.add_argument_group("actions (default when absent: the interactive menu)")
    action.add_argument("--doctor", action="store_true",
                        help="run the prerequisite check (and repair with --yes)")
    action.add_argument("--build", action="store_true",
                        help="build and package the selected targets")
    action.add_argument("--test", action="store_true",
                        help="run the test corpus")
    action.add_argument("--bench", action="store_true",
                        help="run the benchmark suite")

    parser.add_argument("--build-targets", "--targets", dest="targets", default=None,
                        help="comma-separated matrix targets or 'all' (windows-x64, windows-arm64, linux-x64, linux-arm64)")
    parser.add_argument("--filter", default=None,
                        help="only test cases whose path contains this substring")
    parser.add_argument("--cases", default=None,
                        help="root directory of test cases (default: tests/cases)")
    parser.add_argument("--jobs", default=None,
                        help="parallel jobs for the test corpus (default: one per logical CPU)")
    parser.add_argument("--timeout", default=None,
                        help="per-case timeout in seconds (default: 60)")
    parser.add_argument("--json", action="store_true",
                        help="emit machine-readable test output")
    parser.add_argument("--no-bench", action="store_true",
                        help="skip the benchmark pass that follows the corpus pass")
    parser.add_argument("--only", default=None,
                        help="comma-separated benchmarks to run (see --bench --help for the list)")
    parser.add_argument("--except", dest="except_", default=None,
                        help="comma-separated benchmarks to skip")
    parser.add_argument("--reps", default=None,
                        help="independent repetitions per benchmark (default: 3)")
    parser.add_argument("--seconds", default=None,
                        help="throughput phase seconds per server")
    parser.add_argument("--concurrency", default=None,
                        help="throughput phase connections")
    parser.add_argument("--clients", default=None,
                        help="client processes for the multicore server row")
    parser.add_argument("--results", default=None,
                        help="directory to tee benchmark output into")
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero when the benchmark gate prints WARN/BIG WARN")
    parser.add_argument("--report", action="store_true",
                        help="doctor: print findings only, install nothing")
    parser.add_argument("--yes", action="store_true",
                        help="do not ask before configuring or building")
    parser.add_argument("--no-check", action="store_true",
                        help="skip the prerequisite check before the menu")
    parser.add_argument("--platform", default="windows", choices=["windows", "linux"],
                        help="where to run --test/--bench: windows (local) or linux (inside WSL against the WSL-built cp3c)")
    args = parser.parse_args(argv)

    scripted = args.doctor or args.build or args.test or args.bench
    if not scripted and not args.no_check and _is_interactive():
        rc = run_doctor(argparse.Namespace(report=False, yes=args.yes))
        if rc != 0:
            print("\nsome pieces are missing - the menu is still available;"
                  " use 'Doctor / initial setup' to configure them.\n")

    if args.doctor:
        return run_doctor(args)
    if args.build:
        return run_build(args)
    if args.test:
        return run_tests(args)
    if args.bench:
        return run_benchmarks(args)

    if not _is_interactive():
        parser.print_help()
        print("\nno action requested (non-interactive); pass --build/--test/--bench/--doctor")
        return 0
    return run_menu(args)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        print("\ninterrupted.")
        sys.exit(130)
