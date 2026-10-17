#!/usr/bin/env python3
# =============================================================================
# examples/competitions/bench/run_suite.py
#
# Dependency-free comparison-suite runner: builds and runs every competition
# entry available on THIS machine and prints comparison tables of real
# measurements. Nothing here is estimated; every number comes from a process
# this script started and stopped itself.
#
#   * health-server: build (if needed), boot->ready ms, /healthz latency
#     p50/p95 over N requests on persistent keep-alive sessions, req/s at fixed
#     concurrency for a few seconds. Processes are started, and killed, one at
#     a time; only processes this script started are ever stopped.
#   * compute-single / compute-multi: build (if needed), then run R times and
#     report the best wall time. Outputs are cross-checked against each other
#     where the workload is identical.
#   * cp3 entries that need unlanded features are attempted like everything
#     else; the compiler's own CP3xxx diagnostic is reported instead of a
#     timing. When a feature lands, the entry participates automatically.
#
# Python 3 stdlib only. Usage:  python run_suite.py [options]
# =============================================================================
from __future__ import annotations

import argparse
import http.client
import os
import platform
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPETITIONS = HERE.parent
ROOT = COMPETITIONS.parents[1]  # examples/competitions -> repo root
BUILD = HERE / ".build"

CP3C: Path | None = None  # set in main() from --cp3c
EXE = ".exe" if os.name == "nt" else ""
SERVER_PORTS = {
    "cpp": 18080,
    "rust": 18081,
    "go": 18082,
    "js": 18083,
    "python": 18084,
    # The cp3 server entry has no argv surface in the beta-one std spec, so its
    # port is the constant in its own source. Never bound unless std/network
    # lands and the entry builds.
    "cp3": 8080,
}
LANGUAGES = ["cpp", "rust", "go", "js", "python", "cp3"]


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------
def percentile(values: list[float], p: float) -> float:
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = round((p / 100.0) * (len(ordered) - 1))
    return ordered[max(0, min(len(ordered) - 1, index))]


def first_diagnostic(text: str) -> str:
    for line in text.replace("\r", "").split("\n"):
        if "error" in line or "CP3" in line:
            return line.strip()
    return text.strip().split("\n")[0][:160] if text.strip() else "failed"


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def up_to_date(output: Path, source: Path) -> bool:
    """True when output exists and is at least as new as its source."""
    return output.exists() and output.stat().st_mtime >= source.stat().st_mtime


def run_command(argv: list[str], cwd: Path | None = None, timeout: float = 300.0):
    """subprocess.run with a guaranteed-bounded wait: on timeout the child is
    killed and the wait for it is itself bounded, so the suite can never hang."""
    process = subprocess.Popen(
        argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    try:
        out, err = process.communicate(timeout=timeout)
        return subprocess.CompletedProcess(argv, process.returncode, out, err)
    except subprocess.TimeoutExpired:
        process.kill()
        try:
            out, err = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            out, err = "", ""
        err = (err or "") + f"\n[runner] command timed out after {timeout:g}s and was killed"
        return subprocess.CompletedProcess(argv, -1, out or "", err)


def time_wait_count() -> int | None:
    """Windows only: how many sockets are in TIME_WAIT right now.

    A very high count (thousands) means some earlier tool hammered fresh TCP
    connections and the ephemeral port range is still congested; new conntions
    can then fail with WSAEADDRINUSE through no fault of the servers. The
    suite uses persistent keep-alive connections precisely to avoid creating
    that pressure itself, but it cannot undo someone else's.
    """
    if os.name != "nt":
        return None
    try:
        result = run_command(["netstat", "-an"], timeout=60)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return sum(1 for line in result.stdout.splitlines() if "TIME_WAIT" in line)


def probe_version(argv: list[str]) -> str:
    try:
        result = run_command(argv, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        return "missing"
    if result.returncode != 0:
        return "error"
    text = (result.stdout or result.stderr).strip().splitlines()
    return text[0] if text else "?"


# --------------------------------------------------------------------------
# HTTP client bits: one-shot probe (fresh connection) + keep-alive load sessions
# --------------------------------------------------------------------------
def http_get(port: int, path: str = "/healthz", timeout: float = 5.0):
    """One-shot request on a fresh connection; used only for boot probing."""
    start = time.perf_counter()
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        conn.request("GET", path)
        response = conn.getresponse()
        body = response.read().decode("utf-8", "replace")
        status = response.status
    finally:
        conn.close()
    return status, body, (time.perf_counter() - start) * 1000.0


class Session:
    """One persistent HTTP/1.1 (keep-alive) connection to a server.

    The load phases use one Session per client: this is how real load
    generators (wrk, ab, hey) drive servers by default, and it avoids the
    Windows client-side pathology where a rapid stream of fresh TCP
    connections exhausts ephemeral ports (WSAEADDRINUSE / WinError 10048)
    while the server is perfectly healthy. A server that closed the
    connection between requests is reconnected to once, transparently.
    """

    def __init__(self, port: int, timeout: float = 5.0):
        self.port = port
        self.timeout = timeout
        self.conn = None

    def close(self):
        if self.conn is not None:
            self.conn.close()
            self.conn = None

    def _connect(self):
        self.conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=self.timeout)

    def get(self, path: str = "/healthz"):
        """Returns (status, body, round-trip milliseconds)."""
        if self.conn is None:
            self._connect()
        for attempt in (0, 1):
            start = time.perf_counter()
            try:
                self.conn.request("GET", path)
                response = self.conn.getresponse()
                body = response.read().decode("utf-8", "replace")
                return response.status, body, (time.perf_counter() - start) * 1000.0
            except (OSError, http.client.HTTPException):
                self.close()
                if attempt == 1:
                    raise
                self._connect()


def wait_until_ready(port: int, process: subprocess.Popen, deadline_s: float = 20.0) -> float:
    """Returns boot->ready milliseconds, or -1 if the server never answered."""
    start = time.perf_counter()
    while time.perf_counter() - start < deadline_s:
        if process.poll() is not None:
            return -1.0
        try:
            status, _, _ = http_get(port, "/healthz", timeout=0.5)
            if status == 200:
                return (time.perf_counter() - start) * 1000.0
        except (OSError, http.client.HTTPException):
            pass
        time.sleep(0.005)
    return -1.0


# --------------------------------------------------------------------------
# server trials
# --------------------------------------------------------------------------
def server_build_spec(language: str) -> tuple[list[str], Path, Path] | None:
    """(build argv, output binary, source file), or None when there is no build step."""
    base = COMPETITIONS / "health-server"
    if language == "cpp":
        output = BUILD / f"health-server-cpp{EXE}"
        argv = ["g++", "-std=c++20", "-O2", "-o", str(output), str(base / "cpp" / "main.cpp")]
        if os.name == "nt":
            argv.append("-lws2_32")
        return argv, output, base / "cpp" / "main.cpp"
    if language == "rust":
        output = BUILD / f"health-server-rust{EXE}"
        return (["rustc", "-O", "-o", str(output), str(base / "rust" / "main.rs")],
                output, base / "rust" / "main.rs")
    if language == "go":
        output = BUILD / f"health-server-go{EXE}"
        return (["go", "build", "-o", str(output), str(base / "go" / "main.go")],
                output, base / "go" / "main.go")
    return None


def server_run_argv(language: str, port: int) -> tuple[list[str], str]:
    base = COMPETITIONS / "health-server"
    if language == "cpp":
        return [str(BUILD / f"health-server-cpp{EXE}"), str(port)], "hand-rolled sockets (Winsock2)"
    if language == "rust":
        return [str(BUILD / f"health-server-rust{EXE}"), str(port)], "std::net, no crates"
    if language == "go":
        return [str(BUILD / f"health-server-go{EXE}"), str(port)], "net/http"
    if language == "js":
        express_modules = base / "js" / "express" / "node_modules"
        if express_modules.is_dir():
            return ["node", str(base / "js" / "express" / "server.mjs"), str(port)], "express (installed)"
        return ["node", str(base / "js" / "main.mjs"), str(port)], "node:http (zero-dep)"
    if language == "python":
        return [sys.executable, str(base / "python" / "main.py"), str(port)], "http.server (threaded)"
    if language == "cp3":
        return [str(BUILD / f"health-server-cp3{EXE}"), str(port)], "std/network (spec)"
    raise ValueError(language)


def try_cp3_build(entry: Path) -> tuple[bool, str]:
    """Build a cp3 entry. Returns (ok, diagnostic-or-exe-path)."""
    result = run_command([str(CP3C), "build", "main.cp3"], cwd=entry)
    if result.returncode != 0:
        return False, first_diagnostic(result.stderr or result.stdout)
    candidates = sorted(
        (entry / ".cp3").rglob(f"*{EXE}") if EXE else (entry / ".cp3").rglob("*"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for candidate in candidates:
        if candidate.is_file() and candidate.suffix == EXE and candidate.stat().st_size > 0:
            return True, str(candidate)
    return False, "build succeeded but no executable was found under .cp3/"


def run_server_trial(language: str, latency_count: int, warmup: int,
                     seconds: float, concurrency: int, cp3c: Path) -> dict:
    base = COMPETITIONS / "health-server"
    row: dict = {"language": language, "variant": "", "build_ms": None,
                 "boot_ms": None, "p50": None, "p95": None, "rps": None,
                 "requests": 0, "errors": 0, "note": ""}
    port = SERVER_PORTS[language]

    # --- build step -------------------------------------------------------
    if language == "cp3":
        entry = base / "cp3"
        start = time.perf_counter()
        ok, detail = try_cp3_build(entry)
        row["build_ms"] = (time.perf_counter() - start) * 1000.0
        if not ok:
            row["note"] = f"not runnable yet: {detail}"
            return row
        argv, variant = [detail, str(port)], "std/network (spec)"
        row["variant"] = variant
    else:
        argv, variant = server_run_argv(language, port)
        row["variant"] = variant
        build = server_build_spec(language)
        if build is not None:
            build_argv, output, source = build
            if up_to_date(output, source):
                row["note"] = "build: cached"
            else:
                start = time.perf_counter()
                result = run_command(build_argv)
                row["build_ms"] = (time.perf_counter() - start) * 1000.0
                if result.returncode != 0:
                    row["note"] = f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
                    return row

    # --- run it -----------------------------------------------------------
    log_path = BUILD / f"health-server-{language}.log"
    with open(log_path, "wb") as log:
        process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT, cwd=base)
        try:
            boot_ms = wait_until_ready(port, process)
            if boot_ms < 0:
                row["note"] = f"never became ready (exit={process.poll()})"
                return row
            row["boot_ms"] = boot_ms

            session = Session(port)
            for _ in range(warmup):
                try:
                    session.get("/healthz")
                except (OSError, http.client.HTTPException):
                    pass

            latencies = []
            errors = 0
            for _ in range(latency_count):
                try:
                    status, _, ms = session.get("/healthz")
                    if status != 200:
                        errors += 1
                    latencies.append(ms)
                except (OSError, http.client.HTTPException):
                    errors += 1
            session.close()
            row["p50"] = percentile(latencies, 50)
            row["p95"] = percentile(latencies, 95)

            deadline = time.perf_counter() + seconds
            counter = {"ok": 0, "err": 0}
            lock = threading.Lock()

            def worker():
                local_ok = local_err = 0
                session = Session(port)
                while time.perf_counter() < deadline:
                    try:
                        status, _, _ = session.get("/healthz")
                        if status == 200:
                            local_ok += 1
                        else:
                            local_err += 1
                    except (OSError, http.client.HTTPException):
                        local_err += 1
                session.close()
                with lock:
                    counter["ok"] += local_ok
                    counter["err"] += local_err

            threads = [threading.Thread(target=worker) for _ in range(concurrency)]
            load_start = time.perf_counter()
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
            elapsed = time.perf_counter() - load_start
            row["requests"] = counter["ok"]
            row["errors"] = errors + counter["err"]
            row["rps"] = counter["ok"] / elapsed if elapsed > 0 else float("nan")
            details = f"warmup {warmup}, {len(latencies)} latency reqs, {concurrency} conns x {seconds:g}s"
            row["note"] = f"{row['note']}, {details}" if row["note"] else details
        finally:
            process.kill()  # only ever the process this script started
            process.wait(timeout=15)
    return row


# --------------------------------------------------------------------------
# compute trials
# --------------------------------------------------------------------------
def compute_build_spec(language: str, task: str) -> tuple[list[str], Path, Path] | None:
    """(build argv, output binary, source file), or None when there is no build step."""
    base = COMPETITIONS / task
    if language == "cpp":
        output = BUILD / f"{task}-cpp{EXE}"
        return (["g++", "-std=c++20", "-O2", "-o", str(output), str(base / "cpp" / "main.cpp")],
                output, base / "cpp" / "main.cpp")
    if language == "rust":
        output = BUILD / f"{task}-rust{EXE}"
        return (["rustc", "-O", "-o", str(output), str(base / "rust" / "main.rs")],
                output, base / "rust" / "main.rs")
    if language == "go":
        output = BUILD / f"{task}-go{EXE}"
        return (["go", "build", "-o", str(output), str(base / "go" / "main.go")],
                output, base / "go" / "main.go")
    return None


def compute_run_argv(language: str, task: str) -> list[str]:
    base = COMPETITIONS / task
    if language == "cpp":
        return [str(BUILD / f"{task}-cpp{EXE}")]
    if language == "rust":
        return [str(BUILD / f"{task}-rust{EXE}")]
    if language == "go":
        return [str(BUILD / f"{task}-go{EXE}")]
    if language == "js":
        return ["node", str(base / "js" / "main.mjs")]
    if language == "python":
        return [sys.executable, str(base / "python" / "main.py")]
    raise ValueError(language)


def run_compute_trial(language: str, task: str, reps: int, cp3c: Path) -> dict:
    base = COMPETITIONS / task
    row: dict = {"language": language, "build_ms": None, "runs_ms": [],
                 "output": None, "note": ""}

    if language == "cp3":
        entry = base / "cp3"
        start = time.perf_counter()
        ok, detail = try_cp3_build(entry)
        row["build_ms"] = (time.perf_counter() - start) * 1000.0
        if not ok:
            row["note"] = f"not runnable yet: {detail}"
            return row
        argv = [detail]
    else:
        build = compute_build_spec(language, task)
        if build is not None:
            build_argv, output, source = build
            if up_to_date(output, source):
                row["note"] = "build: cached"
            else:
                start = time.perf_counter()
                result = run_command(build_argv)
                row["build_ms"] = (time.perf_counter() - start) * 1000.0
                if result.returncode != 0:
                    row["note"] = f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
                    return row
        argv = compute_run_argv(language, task)

    for _ in range(reps):
        start = time.perf_counter()
        result = run_command(argv, timeout=900)
        wall_ms = (time.perf_counter() - start) * 1000.0
        if result.returncode != 0:
            row["note"] = f"run failed: {first_diagnostic(result.stderr or result.stdout)}"
            return row
        row["runs_ms"].append(wall_ms)
        row["output"] = (result.stdout or "").strip()
    return row


# --------------------------------------------------------------------------
# reporting
# --------------------------------------------------------------------------
def print_server_table(rows: list[dict]) -> None:
    print()
    print("== health-server ==")
    print(f"{'lang':<7} {'variant':<22} {'build_ms':>9} {'boot_ms':>8} "
          f"{'p50_ms':>8} {'p95_ms':>8} {'req/s':>9}  note")
    print("-" * 100)
    for row in rows:
        build_cell = f"{row['build_ms']:>9.0f}" if row["build_ms"] is not None else f"{'-':>9}"
        if row["note"].startswith("not runnable"):
            print(f"{row['language']:<7} {'-':<22} {build_cell} {'-':>8} "
                  f"{'-':>8} {'-':>8} {'-':>9}  {row['note']}")
            continue
        if row["boot_ms"] is None:
            print(f"{row['language']:<7} {row['variant']:<22} {build_cell} {'-':>8} "
                  f"{'-':>8} {'-':>8} {'-':>9}  {row['note']}")
            continue
        print(f"{row['language']:<7} {row['variant']:<22} {build_cell} "
              f"{row['boot_ms']:>8.1f} {row['p50']:>8.2f} {row['p95']:>8.2f} "
              f"{row['rps']:>9.1f}  {row['note']}")
        if row["errors"]:
            print(f"{'':<7} ^ {row['errors']} request(s) failed during measurement")


def print_compute_table(task: str, rows: list[dict], expected_note: str) -> None:
    print()
    print(f"== {task} == {expected_note}")
    print(f"{'lang':<7} {'build_ms':>9} {'best_ms':>9} {'all_runs_ms':<20} {'output':<12} {'match':<6}  note")
    print("-" * 100)
    reference = None
    for row in rows:
        build_cell = f"{row['build_ms']:>9.0f}" if row["build_ms"] is not None else f"{'-':>9}"
        if not row["runs_ms"]:
            print(f"{row['language']:<7} {build_cell} "
                  f"{'-':>9} {'-':<20} {'-':<12} {'-':<6}  {row['note']}")
            continue
        best = min(row["runs_ms"])
        runs = "/".join(f"{ms:.0f}" for ms in row["runs_ms"])
        if reference is None:
            reference = row["output"]
            match = "ref"
        elif row["output"] == reference:
            match = "ok"
        else:
            match = "MISMATCH"
        print(f"{row['language']:<7} {build_cell} {best:>9.0f} {runs:<20} "
              f"{row['output']:<12} {match:<6}  {row['note']}")
    outputs = {row["output"] for row in rows if row["output"] is not None}
    if len(outputs) > 1:
        print(f"!! outputs differ across languages: {sorted(outputs)}")


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description="examples/competitions comparison suite")
    parser.add_argument("--only", default="",
                        help="comma list of: health-server,compute-single,compute-multi")
    parser.add_argument("--seconds", type=float, default=3.0, help="throughput phase seconds per server")
    parser.add_argument("--concurrency", type=int, default=8, help="throughput phase connections")
    parser.add_argument("--latency", type=int, default=300, help="latency phase request count")
    parser.add_argument("--warmup", type=int, default=100, help="warmup requests before measuring")
    parser.add_argument("--reps", type=int, default=2, help="compute runs; best is reported")
    parser.add_argument("--cp3c", default=str(ROOT / "build" / "bin" / f"cp3c{EXE}"),
                        help="path to cp3c (default: build/bin/cp3c.exe)")
    args = parser.parse_args()

    global CP3C
    CP3C = Path(args.cp3c)
    cp3c = CP3C
    BUILD.mkdir(exist_ok=True)

    wanted = {part.strip() for part in args.only.split(",") if part.strip()} or {
        "health-server", "compute-single", "compute-multi"}

    print(f"examples/competitions bench suite -- {datetime.now().isoformat(timespec='seconds')}")
    print(f"machine: {platform.processor() or platform.machine()} | {os.cpu_count()} logical CPUs | {platform.platform()}")
    print(f"runner:  python {platform.python_version()} ({sys.executable})")
    probes = [
        ("g++", ["g++", "--version"]), ("rustc", ["rustc", "--version"]),
        ("go", ["go", "version"]), ("node", ["node", "--version"]),
        ("python", [sys.executable, "--version"]), ("cp3c", [str(cp3c), "version"]),
    ]
    for name, argv in probes:
        print(f"  {name:<7} {probe_version(argv)}")
    print("every number below is a wall-clock measurement from this run; "
          "load phases use persistent keep-alive connections (one per client)")
    waiting = time_wait_count()
    if waiting is not None and waiting > 4000:
        print(f"[warn] {waiting} sockets are in TIME_WAIT; a previous fresh-connection "
              "tool may still be congesting ephemeral ports -- failures below may be "
              "that, not the servers. Wait a couple of minutes and rerun for clean numbers.")

    def present(language: str) -> bool:
        if language == "cp3":
            return cp3c.exists()
        if language == "js":
            return have("node")
        if language == "python":
            return True
        return have({"cpp": "g++", "rust": "rustc", "go": "go"}[language])

    if "health-server" in wanted:
        rows = []
        for language in LANGUAGES:
            if not present(language):
                print(f"[skip] {language}: toolchain not found on PATH")
                continue
            print(f"[run ] health-server: {language} ...", flush=True)
            rows.append(run_server_trial(language, args.latency, args.warmup,
                                         args.seconds, args.concurrency, cp3c))
        print_server_table(rows)

    if "compute-single" in wanted or "compute-multi" in wanted:
        # One known-good reference value for the shared workload, so a wrong
        # output anywhere is visible even if every language were wrong the
        # same way. Computed independently: see competitions/README.md.
        known = 131434424
        for task in ("compute-single", "compute-multi"):
            if task not in wanted:
                continue
            rows = []
            for language in LANGUAGES:
                if not present(language):
                    print(f"[skip] {language}: toolchain not found on PATH")
                    continue
                print(f"[run ] {task}: {language} ...", flush=True)
                rows.append(run_compute_trial(language, task, args.reps, cp3c))
            print_compute_table(task, rows, f"expected total Collatz steps for 1..1,000,000 = {known}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
