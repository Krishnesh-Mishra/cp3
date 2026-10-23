#!/usr/bin/env python3
# =============================================================================
# benchmarks/bench/run_suite.py
#
# Dependency-free comparison-suite runner: builds and runs every benchmark
# entry available on THIS machine and prints comparison tables of real
# measurements. Nothing here is estimated; every number comes from a process
# this script started and stopped itself.
#
#   * health-server and health-server-multi: build (if needed), boot->ready
#     ms, /healthz latency p50/p95 over N requests on persistent keep-alive
#     sessions, req/s at fixed concurrency for a few seconds. The multicore
#     row is driven by several client OS processes. Server processes are
#     started and stopped one at a time, together with the process tree each
#     one created (cluster workers, multiprocessing workers).
#   * compute-style tasks (compute-single, compute-single-int, compute-multi,
#     hello-world, string-churn, sha256-churn, buffer-copy, word-frequency,
#     object-churn, array-growth, closure-capture, interface-dispatch,
#     generics-mono, json-roundtrip, async-fanout): build (if needed),
#     then run R times and report the best wall time; hello-world reports the
#     best and the median over at least 15 runs. Outputs are cross-checked
#     across languages and against the independent oracle values (see
#     bench/oracle.py) where one exists. json-roundtrip and async-fanout omit
#     cpp/rust on purpose (no stdlib JSON surface / no stdlib timer-event loop),
#     and sha256-churn omits them too (no stdlib SHA-256); the runner prints
#     the reason per row and still gates go/js/python.
#   * cp3 entries that need unlanded features are attempted like everything
#     else; the compiler's own CP3xxx diagnostic is reported instead of a
#     timing. A CP3002 row is a hard gate failure, not a pending row: the
#     benchmark gate must not look green while a documented language feature
#     is missing. When a feature lands, the entry participates automatically.
#
# Every row also carries measured peak RSS (Windows: kernel PeakWorkingSetSize
# sampled from Python; "-" when the process exits before a sample lands) and
# the size (KiB) of the built executable after stripping a copy where the
# toolchain strip applies ("-" for Node/Python and for rows that never built).
# Compute tables and the final summary carry cp3/cpp and cp3/rust time ratios
# (lower is better) plus a geomean footer over the runnable rows.
#
# Performance gate (D67): every runnable row is gated against all five
# references as a time ratio (lower is better for cp3). BIG WARN: cp3 over
# 1.20x cpp/rust, or slower than any of go/js/python at all. WARN: cp3 over
# 1.05x cpp/rust (the D64 5% band is the trigger; 2026-10-19 directive).
# Multi-core rows (compute-multi and the health-server-multi row;
# MULTICORE_TASKS) must additionally be FASTER than cpp: any ratio above 1.00
# warns. Compute references under 10 ms are noise-scale and not gated. D64
# targets cp3 within about 5% of cpp/rust for the native-type tiers; --strict
# turns any tier line into exit code 1.
#
# Python 3 stdlib only. Usage:  python run_suite.py [options]
# =============================================================================
from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import math
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import textwrap
import threading
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
BENCHMARKS = HERE.parent
ROOT = BENCHMARKS.parent  # benchmarks -> repo root
BUILD = HERE / ".build"

CP3C: Path | None = None  # set in main() from --cp3c
EXE = ".exe" if os.name == "nt" else ""
SERVER_PORTS = {
    "health-server": {
        "cpp": 18080,
        "rust": 18081,
        "go": 18082,
        "js": 18083,
        "python": 18084,
        # std/process (argv) and std/network have landed, so the cp3 entry
        # takes its port from argv[1] like every other language.
        "cp3": 18085,
    },
    "health-server-multi": {
        "cpp": 18090,
        "rust": 18091,
        "go": 18092,
        "js": 18093,
        "python": 18094,
        "cp3": 18095,
    },
}
LANGUAGES = ["cpp", "rust", "go", "js", "python", "cp3"]

SERVER_TASKS = ["health-server", "health-server-multi"]
COMPUTE_TASKS = [
    "compute-single",
    "compute-single-int",
    "compute-multi",
    "hello-world",
    "string-churn",
    "sha256-churn",
    "buffer-copy",
    "word-frequency",
    "object-churn",
    "array-growth",
    "closure-capture",
    "interface-dispatch",
    "generics-mono",
    "json-roundtrip",
    "async-fanout",
]
ALL_TASKS = SERVER_TASKS + COMPUTE_TASKS

# Per-task language omissions. These rows compare standard-library surfaces
# that two languages do not have: no JSON parser/serializer in C++ std or Rust
# std without a crate, no timer/event-loop surface in either standard library,
# and no SHA-256 in either standard library. A single dependency-free file
# would have to vendor or hand-roll the missing stack, which compares
# dependencies, not the language. The reason is printed per row, and these
# languages are skipped only for these tasks; the gate still applies to
# go/js/python.
TASK_OMIT_LANGUAGES = {
    "json-roundtrip": {
        "cpp": "no JSON in the C++ standard library; a dependency-free file would vendor a parser",
        "rust": "no JSON in Rust std without a crate; adding serde_json compares dependencies",
    },
    "async-fanout": {
        "cpp": "no timers or event loop in the C++ standard library; a file would hand-roll both",
        "rust": "no timers or executor in Rust std; adding tokio/async-std compares dependencies",
    },
    "sha256-churn": {
        "cpp": "no SHA-256 in the C++ standard library; vendoring a hash implementation compares dependencies",
        "rust": "no SHA-256 in Rust std without a crate; adding sha2 compares dependencies",
    },
}

# Workload scale for string-churn. The cp3 entry has no argv surface yet, so
# its committed source carries CHURN_DEFAULT_ITERATIONS; the runner builds a
# copy with the constant substituted for any other scale.
CHURN_DEFAULT_ITERATIONS = 50000
CHURN_STRESS_ITERATIONS = 300000
CHURN_SCALE_PRESETS = {"default": CHURN_DEFAULT_ITERATIONS, "stress": CHURN_STRESS_ITERATIONS}

# Expected outputs. Fixed-value tasks are embedded here; string-churn values
# depend on the chosen scale. All values were computed independently by
# benchmarks/bench/oracle.py, which re-implements every workload in plain
# Python; see benchmarks/README.md ("measured, not estimated").
EXPECTED = {
    "compute-single": "131434424",
    "compute-single-int": "131434424",
    "compute-multi": "131434424",
    "hello-world": "Hello, world!",
    "sha256-churn": "218229",
    "buffer-copy": "688024",
    "word-frequency": "37425343",
    "object-churn": "90641269",
    "array-growth": "200000010000000",
    "closure-capture": "16666688333331 998521500000",
    "interface-dispatch": "37999986 66000000",
    "generics-mono": "998986007 998986007",
    "json-roundtrip": "664346",
    "async-fanout": "99898",
}
CHURN_EXPECTED = {
    CHURN_DEFAULT_ITERATIONS: "852167",
    CHURN_STRESS_ITERATIONS: "254546",
}

GATE_BIG_NATIVE = 1.20   # BIG WARN when cp3 is over this multiple of cpp/rust time
GATE_WARN_NATIVE = 1.05  # WARN when cp3 is over this multiple (the D64 5% band; user directive 2026-10-19)
GATE_SCRIPT = 1.00       # BIG WARN when cp3 is slower than go/js/python at all
GATE_MULTI = 1.00        # multicore rows must be FASTER than cpp (user directive 2026-10-19)
NOISE_FLOOR_MS = 10.0    # compute references faster than this are noise-scale
GATE_REFERENCES = ("cpp", "rust", "go", "js", "python")
NATIVE_REFERENCES = ("cpp", "rust")
# Rows whose cp3 entry is deliberately parallel (TBB). cp3 ships TBB, so these
# rows are expected to win against a cpp comparator, not merely tie it.
MULTICORE_TASKS = {"compute-multi", "health-server-multi"}
GATE_TARGET = ("D64 target: cp3 within about 5% of cpp/rust (WARN over 1.05x); "
               "BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; "
               "multi-core rows must be faster than cpp")

GATE_WARNINGS: list[str] = []   # tier lines (BIG WARN / WARN), printed at the end
GATE_NOTES: list[str] = []      # noise-scale notes, printed with the gate block
GATE_BLOCKERS: list[str] = []   # CP3002 rows that cannot be measured at all
SUMMARY_ROWS: list[dict] = []   # one {"task", "cpp", "rust"} ratio row per task


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


def run_measured(argv: list[str], cwd: Path | None = None, timeout: float = 900.0):
    """run_command plus wall time plus peak-RSS sampling of the child process.

    Returns (CompletedProcess, wall_ms, peak_bytes_or_None). The peak is the
    kernel-maintained PeakWorkingSetSize sampled from Python; on other
    platforms, or when the process exits before a sample lands, it is None and
    every report prints '-' honestly."""
    start = time.perf_counter()
    process = subprocess.Popen(
        argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    sampler = RssSampler(process.pid)
    sampler.start()
    try:
        out, err = process.communicate(timeout=timeout)
        wall_ms = (time.perf_counter() - start) * 1000.0
        result = subprocess.CompletedProcess(argv, process.returncode, out, err)
    except subprocess.TimeoutExpired:
        process.kill()
        wall_ms = (time.perf_counter() - start) * 1000.0
        try:
            out, err = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            out, err = "", ""
        err = (err or "") + f"\n[runner] command timed out after {timeout:g}s and was killed"
        result = subprocess.CompletedProcess(argv, -1, out or "", err or "")
    finally:
        sampler.stop()
        sampler.join(timeout=5)
    return result, wall_ms, sampler.peak


def time_wait_count() -> int | None:
    """Windows only: how many sockets are in TIME_WAIT right now.

    A very high count (thousands) means some earlier tool hammered fresh TCP
    connections and the ephemeral port range is still congested; new
    connections can then fail with WSAEADDRINUSE through no fault of the
    servers. The suite uses persistent keep-alive connections precisely to
    avoid creating that pressure itself, but it cannot undo someone else's.
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


def geomean(values: list[float]) -> float | None:
    if not values:
        return None
    return math.exp(sum(math.log(value) for value in values) / len(values))


# --------------------------------------------------------------------------
# peak RSS sampling (Windows kernel counters; honest None elsewhere)
# --------------------------------------------------------------------------
try:
    import ctypes
    from ctypes import wintypes

    class _ProcessMemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]
except Exception:  # pragma: no cover - non-Windows without wintypes
    _ProcessMemoryCounters = None


def peak_working_set(pid: int) -> int | None:
    """Kernel-maintained peak working set of a live process, or None."""
    if os.name != "nt" or _ProcessMemoryCounters is None:
        return None
    try:
        kernel32 = ctypes.windll.kernel32
        psapi = ctypes.windll.psapi
        handle = 0
        for access in (0x1000, 0x0400 | 0x0010):  # QUERY_LIMITED, QUERY|VM_READ
            handle = kernel32.OpenProcess(access, False, pid)
            if handle:
                break
        if not handle:
            return None
        try:
            counters = _ProcessMemoryCounters()
            counters.cb = ctypes.sizeof(_ProcessMemoryCounters)
            if psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
                return int(counters.PeakWorkingSetSize)
            return None
        finally:
            kernel32.CloseHandle(handle)
    except Exception:
        return None


class RssSampler(threading.Thread):
    """Samples a live process's peak working set until stopped.

    PeakWorkingSetSize is maintained by the kernel, so the last successful
    sample before exit is the process's true peak; if the process exits before
    the first sample lands, the report says '-'."""

    def __init__(self, pid: int):
        super().__init__(daemon=True)
        self.pid = pid
        self.peak: int | None = None
        self._stop = threading.Event()

    def run(self):
        interval = 0.002
        while not self._stop.is_set():
            value = peak_working_set(self.pid)
            if value is not None and (self.peak is None or value > self.peak):
                self.peak = value
            if self._stop.wait(interval):
                break
            interval = min(interval * 1.5, 0.02)

    def stop(self):
        self._stop.set()


def stripped_size_kb(binary: Path | None) -> float | None:
    """KiB of the built executable after stripping a COPY where strip applies.

    The binary that runs is never modified. When the toolchain 'strip' is
    missing or rejects the file (some Go builds), the as-built size is
    reported; both cases are honest and the README says which is which."""
    if binary is None or not binary.exists():
        return None
    size = binary.stat().st_size
    strip = shutil.which("strip")
    if strip:
        copy = BUILD / f"stripped-{binary.name}"
        try:
            shutil.copyfile(binary, copy)
            result = run_command([strip, str(copy)], timeout=60)
            if result.returncode == 0 and copy.exists():
                size = copy.stat().st_size
        except OSError:
            pass
        finally:
            try:
                copy.unlink()
            except OSError:
                pass
    return size / 1024.0


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
def server_build_spec(language: str, task: str) -> tuple[list[str], Path, Path] | None:
    """(build argv, output binary, source file), or None when there is no build step."""
    base = BENCHMARKS / task
    stem = task
    if language == "cpp":
        output = BUILD / f"{stem}-cpp{EXE}"
        argv = ["g++", "-std=c++20", "-O2", "-o", str(output), str(base / "cpp" / "main.cpp")]
        if os.name == "nt":
            argv.append("-lws2_32")
        return argv, output, base / "cpp" / "main.cpp"
    if language == "rust":
        output = BUILD / f"{stem}-rust{EXE}"
        return (["rustc", "-O", "-o", str(output), str(base / "rust" / "main.rs")],
                output, base / "rust" / "main.rs")
    if language == "go":
        output = BUILD / f"{stem}-go{EXE}"
        return (["go", "build", "-o", str(output), str(base / "go" / "main.go")],
                output, base / "go" / "main.go")
    return None


def server_run_argv(language: str, task: str, port: int) -> tuple[list[str], str]:
    base = BENCHMARKS / task
    stem = task
    if language == "cpp":
        return [str(BUILD / f"{stem}-cpp{EXE}"), str(port)], (
            "hand-rolled sockets, accept workers")
    if language == "rust":
        return [str(BUILD / f"{stem}-rust{EXE}"), str(port)], (
            "std::net, accept workers")
    if language == "go":
        return [str(BUILD / f"{stem}-go{EXE}"), str(port)], "net/http (all cores)"
    if language == "js":
        express_modules = base / "js" / "express" / "node_modules"
        if task == "health-server" and express_modules.is_dir():
            return ["node", str(base / "js" / "express" / "server.mjs"), str(port)], (
                "express (installed)")
        return ["node", str(base / "js" / "main.mjs"), str(port)], (
            "node:cluster" if task.endswith("-multi") else "node:http (zero-dep)")
    if language == "python":
        return [sys.executable, str(base / "python" / "main.py"), str(port)], (
            "multiprocessing (shared socket)" if task.endswith("-multi")
            else "http.server (threaded)")
    if language == "cp3":
        return [str(BUILD / f"{stem}-cp3{EXE}"), str(port)], (
            "std/network, os.cpus() loops" if task.endswith("-multi")
            else "std/network (spec)")
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


def kill_process_tree(process: subprocess.Popen) -> None:
    """Stop a server process and any children it started (cluster/multiprocessing)."""
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                       capture_output=True)
    else:
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
        except OSError:
            process.kill()
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.kill()


def run_multi_client_load(port: int, seconds: float, concurrency: int,
                          clients: int, path: str = "/healthz",
                          client: str = "raw") -> tuple[int, int, list[str]]:
    """Drive the server from several separate client OS processes.

    Each process owns `concurrency` keep-alive connections and runs for the
    same fixed window, starting at a shared epoch time. `client` is "raw" (the
    multicore row) or "http" (the single-core row's established http.client
    probe). Returns (ok, err, failure_notes); the caller divides ok by
    `seconds`.
    """
    script = HERE / "server_load.py"
    start_at = time.time() + 0.8
    procs: list[subprocess.Popen] = []
    for _ in range(max(1, clients)):
        procs.append(subprocess.Popen(
            [sys.executable, str(script), "--port", str(port),
             "--seconds", f"{seconds:g}", "--concurrency", str(concurrency),
             "--start-at", f"{start_at:.6f}", "--path", path,
             "--client", client],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    ok = err = 0
    failures: list[str] = []
    deadline = start_at + seconds + 60.0
    for process in procs:
        try:
            out, error_text = process.communicate(
                timeout=max(1.0, deadline - time.time()))
        except subprocess.TimeoutExpired:
            kill_process_tree(process)
            out, error_text = "", "[runner] load process timed out and was killed"
        payload = None
        for line in reversed((out or "").strip().splitlines()):
            try:
                payload = json.loads(line)
                break
            except json.JSONDecodeError:
                continue
        if payload is None:
            failures.append(first_diagnostic(error_text or out or "load process failed"))
            continue
        ok += int(payload.get("ok", 0))
        err += int(payload.get("err", 0))
    return ok, err, failures


def run_server_trial(language: str, task: str, latency_count: int, warmup: int,
                     seconds: float, concurrency: int, cp3c: Path,
                     clients: int = 4, single_clients: int = 2) -> dict:
    base = BENCHMARKS / task
    row: dict = {"language": language, "variant": "", "build_ms": None,
                 "boot_ms": None, "p50": None, "p95": None, "rps": None,
                 "requests": 0, "errors": 0, "peak_rss_mb": None,
                 "binary_kb": None, "note": ""}
    port = SERVER_PORTS[task][language]

    # --- build step -------------------------------------------------------
    if language == "cp3":
        entry = base / "cp3"
        start = time.perf_counter()
        ok, detail = try_cp3_build(entry)
        row["build_ms"] = (time.perf_counter() - start) * 1000.0
        if not ok:
            row["note"] = f"not runnable yet: {detail}"
            if "CP3002" in detail:
                GATE_BLOCKERS.append(f"CP3002 BLOCKER {task}: {detail}")
            return row
        argv, variant = [detail, str(port)], f"std/network, os.cpus() loops" \
            if task.endswith("-multi") else "std/network (spec)"
        row["variant"] = variant
        row["binary_kb"] = stripped_size_kb(Path(detail))
    else:
        argv, variant = server_run_argv(language, task, port)
        row["variant"] = variant
        build = server_build_spec(language, task)
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
            row["binary_kb"] = stripped_size_kb(output)

    # --- run it -----------------------------------------------------------
    log_path = BUILD / f"{task}-{language}.log"
    with open(log_path, "wb") as log:
        process = subprocess.Popen(
            argv, stdout=log, stderr=subprocess.STDOUT, cwd=base,
            start_new_session=(os.name != "nt"))
        sampler = RssSampler(process.pid)
        sampler.start()
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

            if task.endswith("-multi"):
                load_clients = clients
                load_client = "raw"
            else:
                load_clients = single_clients
                load_client = "http"
            load_ok, load_err, failures = run_multi_client_load(
                port, seconds, concurrency, load_clients, client=load_client)
            row["requests"] = load_ok
            row["errors"] = errors + load_err
            row["rps"] = load_ok / seconds if seconds > 0 else float("nan")
            details = (f"warmup {warmup}, {len(latencies)} latency reqs, "
                       f"{load_clients} client procs x {concurrency} conns x "
                       f"{seconds:g}s ({load_client})")
            if failures:
                details += f", load failures: {'; '.join(failures[:2])}"
            row["note"] = f"{row['note']}, {details}" if row["note"] else details
        finally:
            sampler.stop()
            sampler.join(timeout=5)
            if sampler.peak is not None:
                row["peak_rss_mb"] = sampler.peak / 1048576.0
            # Only ever the process tree this script started.
            kill_process_tree(process)
    return row


# --------------------------------------------------------------------------
# compute trials
# --------------------------------------------------------------------------
def compute_build_spec(language: str, task: str) -> tuple[list[str], Path, Path] | None:
    """(build argv, output binary, source file), or None when there is no build step."""
    base = BENCHMARKS / task
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


def compute_run_argv(language: str, task: str, extra: tuple[str, ...] = ()) -> list[str]:
    base = BENCHMARKS / task
    if language == "cpp":
        return [str(BUILD / f"{task}-cpp{EXE}"), *extra]
    if language == "rust":
        return [str(BUILD / f"{task}-rust{EXE}"), *extra]
    if language == "go":
        return [str(BUILD / f"{task}-go{EXE}"), *extra]
    if language == "js":
        return ["node", str(base / "js" / "main.mjs"), *extra]
    if language == "python":
        return [sys.executable, str(base / "python" / "main.py"), *extra]
    raise ValueError(language)


def prepare_cp3_churn_variant(entry: Path, iterations: int) -> Path:
    """Build cp3's string-churn entry at a non-default scale.

    The beta-one compiler has no argv surface, so the workload size is a source
    constant. This writes a copy of the entry under bench/.build with ONLY that
    constant substituted (the same workload, one pinned number changed) and
    returns the generated entry directory. The committed source is untouched
    and is used directly at the default scale."""
    source_path = entry / "main.cp3"
    text = source_path.read_text(encoding="utf-8")
    pattern = re.compile(r"const ITERATIONS = \d+;")
    if not pattern.search(text):
        raise SystemExit(f"[runner] cannot find the ITERATIONS constant in {source_path}")
    replaced = pattern.sub(f"const ITERATIONS = {iterations};", text, count=1)
    target = BUILD / "string-churn-cp3-variant"
    target.mkdir(parents=True, exist_ok=True)
    (target / "main.cp3").write_text(replaced, encoding="utf-8")
    return target


def run_compute_trial(language: str, task: str, reps: int, cp3c: Path,
                      extra: tuple[str, ...] = (), cp3_entry: Path | None = None) -> dict:
    base = BENCHMARKS / task
    row: dict = {"language": language, "build_ms": None, "runs_ms": [],
                 "output": None, "note": "", "peak_rss_mb": None, "binary_kb": None}

    if language == "cp3":
        entry = cp3_entry if cp3_entry is not None else (base / "cp3")
        start = time.perf_counter()
        ok, detail = try_cp3_build(entry)
        row["build_ms"] = (time.perf_counter() - start) * 1000.0
        if not ok:
            row["note"] = f"not runnable yet: {detail}"
            if "CP3002" in detail:
                GATE_BLOCKERS.append(f"CP3002 BLOCKER {task}: {detail}")
            return row
        argv = [detail]  # cp3 entries have no argv surface yet; scales are baked in
        row["binary_kb"] = stripped_size_kb(Path(detail))
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
            row["binary_kb"] = stripped_size_kb(output)
        argv = compute_run_argv(language, task, extra)

    for _ in range(reps):
        result, wall_ms, peak = run_measured(argv, timeout=900)
        if result.returncode != 0:
            row["note"] = f"run failed: {first_diagnostic(result.stderr or result.stdout)}"
            return row
        row["runs_ms"].append(wall_ms)
        row["output"] = (result.stdout or "").strip()
        if peak is not None:
            row["peak_rss_mb"] = max(row["peak_rss_mb"] or 0.0, peak / 1048576.0)
    return row


# --------------------------------------------------------------------------
# gate + ratio bookkeeping (no printing here)
# --------------------------------------------------------------------------
def best_ms_of(rows: list[dict], language: str) -> float | None:
    row = next((r for r in rows if r["language"] == language), None)
    if row is None or not row["runs_ms"]:
        return None
    return min(row["runs_ms"])


def gate_row(task: str, cp3_value: float | None,
             references: dict[str, float | None], unit: str) -> None:
    """Fill the tiered performance gate for one runnable task row (D67).

    Values are milliseconds (unit "ms") or requests per second (unit "rps").
    Ratios are time ratios, lower is better for cp3. BIG WARN when cp3 is over
    1.20x cpp/rust or slower than any of go/js/python at all; WARN when cp3 is
    over 1.10x cpp/rust but under the big tier. Compute rows whose reference
    best is under NOISE_FLOOR_MS are noted "noise-scale, ratio not gated"
    instead of tiered, so spawn jitter cannot flap the gate."""
    if cp3_value is None:
        return
    for language in GATE_REFERENCES:
        reference = references.get(language)
        if not reference:
            continue
        ratio = reference / cp3_value if unit == "rps" else cp3_value / reference
        if unit == "ms" and reference < NOISE_FLOOR_MS:
            if ratio > GATE_SCRIPT:
                GATE_NOTES.append(
                    f"note: {task}: cp3 {cp3_value:.1f} ms vs {language} "
                    f"{reference:.1f} ms is noise-scale, ratio not gated")
            continue
        if unit == "ms":
            subject = (f"cp3 {cp3_value:.0f} ms is {ratio:.2f}x {language} "
                       f"({reference:.0f} ms)")
        else:
            subject = (f"cp3 {cp3_value:.0f} req/s is {ratio:.2f}x slower than "
                       f"{language} ({reference:.0f} req/s)")
        if task in MULTICORE_TASKS and language == "cpp":
            # User directive 2026-10-19: cp3 ships TBB, so parallel rows must
            # beat the cpp comparator outright. Ties and losses both warn.
            if ratio > GATE_MULTI:
                tier = ("BIG WARN (fix this)" if ratio > GATE_BIG_NATIVE
                        else "WARN")
                GATE_WARNINGS.append(
                    f"{tier} multi-core {task}: {subject}; multi-core rows must "
                    f"be faster than cpp (target < {GATE_MULTI:.2f}x)")
            continue
        if language in NATIVE_REFERENCES:
            if ratio > GATE_BIG_NATIVE:
                GATE_WARNINGS.append(
                    f"BIG WARN (fix this): {task}: {subject}, tripwire {GATE_BIG_NATIVE:.2f}x")
            elif ratio > GATE_WARN_NATIVE:
                GATE_WARNINGS.append(
                    f"WARN {task}: {subject}, watch {GATE_WARN_NATIVE:.2f}x")
        elif ratio > GATE_SCRIPT:
            GATE_WARNINGS.append(
                f"BIG WARN (fix this): {task}: {subject}, tripwire {GATE_SCRIPT:.2f}x")


def register_ratios(task: str, cp3_value: float | None, cpp_value: float | None,
                    rust_value: float | None, go_value: float | None = None,
                    js_value: float | None = None,
                    python_value: float | None = None):
    """Fill the summary table and the tiered gate for one compute row.

    Visible summary columns stay cp3/cpp and cp3/rust; the gate additionally
    checks go/js/python (D67). Values are times (ms); the ratio is cp3 /
    reference, lower is better."""
    cpp_ratio = cp3_value / cpp_value if (cp3_value and cpp_value) else None
    rust_ratio = cp3_value / rust_value if (cp3_value and rust_value) else None
    SUMMARY_ROWS.append({"task": task, "cpp": cpp_ratio, "rust": rust_ratio})
    gate_row(task, cp3_value, {"cpp": cpp_value, "rust": rust_value,
                               "go": go_value, "js": js_value,
                               "python": python_value}, unit="ms")
    return cpp_ratio, rust_ratio


def ratio_cell(ratio: float | None) -> str:
    return f"{ratio:.2f}x" if ratio is not None else "-"


def rss_cell(row: dict) -> str:
    return f"{row['peak_rss_mb']:.1f}" if row["peak_rss_mb"] is not None else "-"


def binary_cell(row: dict) -> str:
    return f"{row['binary_kb']:.0f}" if row["binary_kb"] is not None else "-"


# --------------------------------------------------------------------------
# terminal UI -- presentation only. Nothing in this section starts a process,
# takes a measurement, or changes a number; it only decides how rows look.
#
#   * colour + unicode are auto-detected (tty, encoding); NO_COLOR disables
#     colour, FORCE_COLOR forces it; piped output degrades to plain text
#   * the --results file always receives plain text (escape codes stripped)
#   * tables shrink to the terminal width: the speed bar is dropped first,
#     then long cells are ellipsised and their full text is printed below
# --------------------------------------------------------------------------
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


class _Ui:
    """Terminal capabilities, decided once by ui_setup()."""
    color = False        # emit ANSI styling
    unicode = False      # box-drawing / block glyphs (else ASCII)
    live = False         # in-place progress line (needs a colour tty)
    width = 120          # terminal columns
    wrap = 120           # wrap width for prose lines
    out = None           # the real terminal stream (never the results tee)
    live_dirty = False   # a live line is currently on screen


UI = _Ui()

SGR = {
    "bold": "1",
    "dim": "38;5;245",
    "red": "38;5;203",
    "green": "38;5;114",
    "yellow": "38;5;221",
    "blue": "38;5;75",
    "magenta": "38;5;213",
    "cyan": "38;5;80",
    "orange": "38;5;208",
    "white": "38;5;255",
}
LANG_STYLE = {
    "cpp": "blue", "rust": "orange", "go": "cyan",
    "js": "yellow", "python": "green", "cp3": "magenta",
}

GLYPH_UNI = {
    "h": "─", "thin": "┈", "tl": "╭", "tr": "╮", "bl": "╰", "br": "╯", "v": "│",
    "title": "◆", "run": "▶", "ok": "✓", "bad": "✗", "warn": "▲", "info": "·",
    "ell": "…", "cur": "▸", "pend": "·", "fill": "█", "track": "·", "mark": "│",
    "over": "»", "arrow": "↳", "partial": "▏▎▍▌▋▊▉", "sep": " · ",
}
GLYPH_ASCII = {
    "h": "-", "thin": ".", "tl": "+", "tr": "+", "bl": "+", "br": "+", "v": "|",
    "title": "*", "run": ">", "ok": "+", "bad": "x", "warn": "!", "info": "-",
    "ell": "..", "cur": ">", "pend": ".", "fill": "#", "track": ".", "mark": "|",
    "over": ">", "arrow": "->", "partial": "", "sep": " | ",
}


def g(name: str) -> str:
    return (GLYPH_UNI if UI.unicode else GLYPH_ASCII)[name]


def paint(text: str, *styles: str) -> str:
    if not UI.color or not styles or not text:
        return text
    codes = ";".join(SGR[style] for style in styles)
    return f"\x1b[{codes}m{text}\x1b[0m"


def _enable_windows_vt() -> bool:
    """Turn on ANSI escape processing for the Windows console."""
    try:
        import ctypes as _ctypes
        kernel32 = _ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        mode = _ctypes.c_uint32()
        if not kernel32.GetConsoleMode(handle, _ctypes.byref(mode)):
            return False
        if mode.value & 0x0004:
            return True
        return bool(kernel32.SetConsoleMode(handle, mode.value | 0x0004))
    except Exception:
        return False


def ui_setup(stream) -> None:
    UI.out = stream
    try:
        is_tty = bool(stream.isatty())
    except Exception:
        is_tty = False
    encoding = (getattr(stream, "encoding", None) or "").lower()
    UI.unicode = "utf" in encoding
    color = is_tty
    if os.environ.get("NO_COLOR") or os.environ.get("TERM") == "dumb":
        color = False
    elif os.environ.get("FORCE_COLOR"):
        color = True
    if color and is_tty and os.name == "nt":
        color = _enable_windows_vt()
    UI.color = color
    UI.live = bool(color and is_tty)
    UI.width = shutil.get_terminal_size(fallback=(120 if is_tty else 140, 30)).columns
    UI.wrap = min(UI.width, 130)


def live_clear() -> None:
    if UI.live and UI.live_dirty and UI.out is not None:
        UI.out.write("\r\x1b[2K")
        UI.out.flush()
        UI.live_dirty = False


def live_status(text: str) -> None:
    UI.out.write("\r\x1b[2K" + text)
    UI.out.flush()
    UI.live_dirty = True


def shorten(text: str, width: int) -> str:
    if len(text) <= width:
        return text
    ell = g("ell")
    if width <= len(ell):
        return text[:width]
    return text[: width - len(ell)] + ell


def cell(text: str, *styles: str, rendered: str | None = None):
    """A table cell: (plain text, styles, optional pre-rendered string)."""
    return (text, styles, rendered)


class Col:
    def __init__(self, key: str, header: str, align: str = "r", flex: bool = False,
                 min_width: int = 0, optional: bool = False):
        self.key = key
        self.header = header
        self.align = align
        self.flex = flex
        self.min_width = min_width
        self.optional = optional


def render_table(columns: list[Col], rows: list, indent: int = 2, gap: int = 2) -> None:
    """Print a rule-style table sized to the terminal.

    `rows` holds dicts {"label", "cells", "styles"}; a None entry draws a thin
    separator. Optional columns are dropped first when the table is too wide,
    then flexible columns are ellipsised; ellipsised text is listed in full
    underneath so nothing is lost."""
    cols = list(columns)
    widths: dict[str, int] = {}
    for col in cols:
        width = len(col.header)
        for row in rows:
            if row is not None and col.key in row["cells"]:
                width = max(width, len(row["cells"][col.key][0]))
        widths[col.key] = width

    def total() -> int:
        return indent + sum(widths[c.key] for c in cols) + gap * (len(cols) - 1)

    available = max(40, UI.width - 2)
    while total() > available:
        optional = [c for c in cols if c.optional]
        if not optional:
            break
        cols.remove(optional[-1])
    # shrink flexible columns toward their minimum first; only on a very narrow
    # terminal go below it (hard floor of 6) rather than letting rows wrap
    for floor in (None, 6):
        while total() > available:
            flexible = [c for c in cols if c.flex and
                        widths[c.key] > (c.min_width if floor is None else floor)]
            if not flexible:
                break
            widest = max(flexible, key=lambda c: widths[c.key])
            widths[widest.key] -= 1

    span = total() - indent
    lead = " " * indent
    joiner = " " * gap

    def join(parts: list[str]) -> str:
        return lead + joiner.join(parts)

    print(lead + paint(g("h") * span, "dim"))
    header_parts = []
    for col in cols:
        text = shorten(col.header, widths[col.key])
        pad = " " * (widths[col.key] - len(text))
        header_parts.append(pad + paint(text, "bold", "dim") if col.align == "r"
                            else paint(text, "bold", "dim") + pad)
    print(join(header_parts).rstrip())
    print(lead + paint(g("h") * span, "dim"))

    truncated: list[tuple[str, str, str]] = []
    for row in rows:
        if row is None:
            print(lead + paint(g("thin") * span, "dim"))
            continue
        parts = []
        for index, col in enumerate(cols):
            text, styles, rendered = row["cells"].get(col.key, ("", (), None))
            width = widths[col.key]
            if len(text) > width:
                shown = shorten(text, width)
                truncated.append((row["label"], col.header, text))
                body = paint(shown, *(tuple(row.get("styles", ())) + tuple(styles)))
            elif rendered is not None:
                shown, body = text, rendered
            else:
                shown = text
                body = paint(shown, *(tuple(row.get("styles", ())) + tuple(styles)))
            pad = " " * (width - len(shown))
            if col.align == "r":
                parts.append(pad + body)
            else:
                parts.append(body if index == len(cols) - 1 else body + pad)
        print(join(parts).rstrip())
    print(lead + paint(g("h") * span, "dim"))
    for label, header, full in truncated:
        print(f"{lead}  {paint(g('arrow'), 'dim')} {paint(label, 'dim')} "
              f"{paint(header + ':', 'dim')} {paint(full, 'dim')}")


def banner(title: str, subtitle: str = "") -> None:
    print()
    print(f"  {paint(g('title'), 'cyan')} {paint(title, 'bold', 'white')}")
    if subtitle:
        for line in textwrap.wrap(subtitle, width=max(40, UI.wrap - 4)):
            print("    " + paint(line, "dim"))


def note_line(tag: str, tag_styles: tuple, text: str, text_styles: tuple = ("dim",),
              indent: int = 4) -> None:
    """A tagged, hanging-indent prose line (wraps to the terminal)."""
    prefix_len = indent + len(tag) + 2
    width = max(30, UI.wrap - prefix_len)
    lines = textwrap.wrap(text, width=width, break_on_hyphens=False) or [""]
    for number, line in enumerate(lines):
        if number == 0:
            print(" " * indent + paint(tag, *tag_styles) + "  " + paint(line, *text_styles))
        else:
            print(" " * prefix_len + paint(line, *text_styles))


def box(lines: list[tuple[str, tuple]], border: str = "cyan") -> None:
    inner = max(len(text) for text, _ in lines)
    edge = g("h") * (inner + 4)
    print("  " + paint(g("tl") + edge + g("tr"), border))
    for text, styles in lines:
        print("  " + paint(g("v"), border) + "  " + paint(text, *styles)
              + " " * (inner - len(text)) + "  " + paint(g("v"), border))
    print("  " + paint(g("bl") + edge + g("br"), border))


def bar(fraction: float | None, width: int, style: str) -> tuple[str, str]:
    """Horizontal bar -> (plain, rendered) of identical visible width."""
    fraction = 0.0 if fraction is None else max(0.0, min(1.0, fraction))
    if UI.unicode:
        eighths = int(round(fraction * width * 8))
        if fraction > 0:
            eighths = max(1, eighths)
        full, part = divmod(eighths, 8)
        filled = g("fill") * full + (g("partial")[part - 1] if part else "")
    else:
        count = int(round(fraction * width))
        if fraction > 0:
            count = max(1, count)
        filled = g("fill") * count
    empty = g("track") * (width - len(filled))
    return filled + empty, paint(filled, style) + paint(empty, "dim")


def ratio_bar(ratio: float | None, style: tuple, half: int = 10) -> tuple[str, str]:
    """Ratio gauge: 0x .. 1.00x (centre tick) .. 2x+; (plain, rendered)."""
    width = half * 2 + 2
    if ratio is None:
        return " " * width, " " * width
    eighths = int(round(min(ratio, 2.0) * half * 8))
    if ratio > 0:
        eighths = max(1, eighths)
    plain: list[str] = []
    rendered: list[str] = []
    for index in range(half * 2):
        if index == half:
            plain.append(g("mark"))
            rendered.append(paint(g("mark"), "dim"))
        level = max(0, min(8, eighths - index * 8))
        if UI.unicode and 0 < level < 8:
            char = g("partial")[level - 1]
            rendered.append(paint(char, *style))
        elif level >= (8 if UI.unicode else 4):
            char = g("fill")
            rendered.append(paint(char, *style))
        else:
            char = g("track")
            rendered.append(paint(char, "dim"))
        plain.append(char)
    if ratio > 2.0:
        plain.append(g("over"))
        rendered.append(paint(g("over"), *style))
    else:
        plain.append(" ")
        rendered.append(" ")
    return "".join(plain) + " ", "".join(rendered) + " "


def ratio_style(ratio: float | None) -> tuple:
    if ratio is None:
        return ("dim",)
    if ratio <= GATE_WARN_NATIVE:
        return ("green",)
    if ratio <= GATE_BIG_NATIVE:
        return ("yellow",)
    return ("red", "bold")


def best_langs(values: dict, higher: bool = False) -> set:
    """Languages holding the best value of a column (needs 2+ values)."""
    present = {key: value for key, value in values.items() if value is not None}
    if len(present) < 2:
        return set()
    target = max(present.values()) if higher else min(present.values())
    return {key for key, value in present.items() if value == target}


def value_cell(text: str, is_best: bool = False):
    if text == "-":
        return cell(text, "dim")
    return cell(text, "green") if is_best else cell(text)


def build_cell(row: dict, cached: bool):
    ms = row["build_ms"]
    if ms is not None:
        return cell(f"{ms / 1000:.1f} s" if ms >= 1000 else f"{ms:.0f} ms")
    if cached:
        return cell("cached", "dim")
    return cell("-", "dim")


def lang_cell(language: str):
    return cell(language, LANG_STYLE[language], "bold")


PROBLEM_PREFIXES = ("not runnable", "build failed", "run failed", "never became ready")


def split_note(note: str) -> tuple[bool, str]:
    """('build: cached' flag, remaining note text)."""
    text = note or ""
    cached = False
    if text.startswith("build: cached"):
        cached = True
        text = text[len("build: cached"):].lstrip(", ")
    return cached, text


def print_row_notes(rows: list[dict]) -> None:
    """Failures first (red/yellow), then shared run details once (grey)."""
    problems: list[tuple[str, str]] = []
    details: dict[str, list[str]] = {}
    for row in rows:
        _, rest = split_note(row["note"])
        if not rest:
            continue
        if rest.startswith(PROBLEM_PREFIXES):
            problems.append((row["language"], rest))
        else:
            details.setdefault(rest, []).append(row["language"])
    for language, text in problems:
        hard = "CP3002" in text or not text.startswith("not runnable")
        style = "red" if hard else "yellow"
        glyph = g("bad") if hard else g("warn")
        note_line(f"{glyph} {language}", (style, "bold"), text, (style,))
    for text, languages in details.items():
        scope = "" if len(languages) == len(rows) else ", ".join(languages) + ": "
        note_line(g("info"), ("dim",), scope + text.replace(", ", g("sep")))


def announce_run(task: str, language: str, index: int, total: int) -> None:
    """Progress before each trial: an in-place live line on a terminal."""
    if not UI.live:
        print(f"  {g('run')} {task}: {language} ...", flush=True)
        return
    current = LANGUAGES.index(language)
    parts = []
    for position, name in enumerate(LANGUAGES):
        if position < current:
            parts.append(paint(g("ok"), "green") + " " + paint(name, "dim"))
        elif position == current:
            parts.append(paint(g("cur"), LANG_STYLE[name]) + " "
                         + paint(name, LANG_STYLE[name], "bold"))
        else:
            parts.append(paint(g("pend"), "dim") + " " + paint(name, "dim"))
    live_status(f"  {paint(g('run'), 'cyan')} {paint(f'{index}/{total}', 'dim')}  "
                f"{paint(task, 'bold')}   " + "  ".join(parts))


def announce_skip(language: str) -> None:
    live_clear()
    note_line(f"{g('warn')} skip", ("yellow",), f"{language}: toolchain not found on PATH",
              ("yellow",), indent=2)


def announce_omit(task: str, language: str, reason: str) -> None:
    live_clear()
    note_line(f"{g('info')} omit", ("dim",), f"{task}: {language}: {reason}", ("dim",), indent=2)


# --------------------------------------------------------------------------
# reporting
# --------------------------------------------------------------------------
def print_header(cpu_text: str, probes: list) -> None:
    stamp = datetime.now().isoformat(timespec="seconds")
    print()
    box([("benchmarks" + g("sep") + "comparison suite", ("bold", "white")),
         (stamp, ("dim",))])
    print()
    for key, value in (("machine", f"{cpu_text}{g('sep')}{os.cpu_count()} logical CPUs"),
                       ("os", platform.platform()),
                       ("runner", f"python {platform.python_version()} ({sys.executable})")):
        print(f"  {paint(key.ljust(9), 'dim')} {value}")
    print()
    print("  " + paint("toolchains", "bold"))
    for name, argv in probes:
        version = probe_version(argv)
        good = version not in ("missing", "error")
        mark = paint(g("ok"), "green") if good else paint(g("bad"), "red", "bold")
        text = paint(version, "dim") if good else paint(version, "red")
        print(f"   {mark} {paint(name.ljust(7), LANG_STYLE.get(name, 'white'))} {text}")
    print()
    for line in textwrap.wrap("every number below is a wall-clock measurement from this run; "
                              "load phases use persistent keep-alive connections "
                              "(one per client)", width=max(40, UI.wrap - 4)):
        print("  " + paint(line, "dim"))


def print_time_wait_warning(waiting: int) -> None:
    print()
    note_line(f"{g('warn')} warn", ("yellow", "bold"),
              f"{waiting} sockets are in TIME_WAIT; a previous fresh-connection "
              "tool may still be congesting ephemeral ports -- failures below may "
              "be that, not the servers. Wait a couple of minutes and rerun for "
              "clean numbers.", ("yellow",), indent=2)


def print_server_table(task: str, rows: list[dict]) -> None:
    live_clear()
    banner(task)
    cp3_row = next((r for r in rows if r["language"] == "cp3"), None)
    cp3_rps = cp3_row["rps"] if cp3_row and cp3_row["rps"] else None
    cpp_row = next((r for r in rows if r["language"] == "cpp"), None)
    rust_row = next((r for r in rows if r["language"] == "rust"), None)
    cpp_rps = cpp_row["rps"] if cpp_row and cpp_row["rps"] else None
    rust_rps = rust_row["rps"] if rust_row and rust_row["rps"] else None
    # throughput -> time ratio: cp3/cpp = cpp_rps / cp3_rps
    cpp_ratio = cpp_rps / cp3_rps if (cpp_rps and cp3_rps) else None
    rust_ratio = rust_rps / cp3_rps if (rust_rps and cp3_rps) else None
    SUMMARY_ROWS.append({"task": task, "cpp": cpp_ratio, "rust": rust_ratio})
    go_rps = next((r["rps"] for r in rows if r["language"] == "go" and r["rps"]), None)
    js_rps = next((r["rps"] for r in rows if r["language"] == "js" and r["rps"]), None)
    python_rps = next((r["rps"] for r in rows
                       if r["language"] == "python" and r["rps"]), None)
    gate_row(task, cp3_rps,
             {"cpp": cpp_rps, "rust": rust_rps, "go": go_rps, "js": js_rps,
              "python": python_rps}, unit="rps")

    ran = {r["language"]: r["boot_ms"] is not None for r in rows}
    top_rps = max((r["rps"] for r in rows if ran[r["language"]] and r["rps"]), default=None)
    best_boot = best_langs({r["language"]: r["boot_ms"] for r in rows})
    best_p50 = best_langs({r["language"]: r["p50"] for r in rows})
    best_p95 = best_langs({r["language"]: r["p95"] for r in rows})
    best_rps = best_langs({r["language"]: r["rps"] for r in rows if ran[r["language"]]},
                          higher=True)
    best_rss = best_langs({r["language"]: r["peak_rss_mb"] for r in rows})
    best_bin = best_langs({r["language"]: r["binary_kb"] for r in rows})

    columns = [
        Col("lang", "lang", "l"),
        Col("variant", "variant", "l", flex=True, min_width=14),
        Col("build", "build"),
        Col("boot", "boot ms"),
        Col("p50", "p50 ms"),
        Col("p95", "p95 ms"),
        Col("rps", "req/s"),
        Col("cpp", "cp3/cpp"),
        Col("rust", "cp3/rust"),
        Col("rss", "rss MiB"),
        Col("bin", "bin KiB"),
        Col("bar", "speed", "l", optional=True),
    ]
    table_rows: list = []
    for row in rows:
        language = row["language"]
        cached, _ = split_note(row["note"])
        is_cp3 = language == "cp3"
        cells = {
            "lang": lang_cell(language),
            "variant": cell(row["variant"] or "-", "dim"),
            "build": build_cell(row, cached),
            "rss": value_cell(rss_cell(row), language in best_rss),
            "bin": value_cell(binary_cell(row), language in best_bin),
        }
        if ran[language]:
            cells["boot"] = value_cell(f"{row['boot_ms']:.1f}", language in best_boot)
            cells["p50"] = value_cell(f"{row['p50']:.2f}", language in best_p50)
            cells["p95"] = value_cell(f"{row['p95']:.2f}", language in best_p95)
            cells["rps"] = value_cell(f"{row['rps']:.1f}", language in best_rps)
            if top_rps:
                plain, rendered = bar(row["rps"] / top_rps, 10, LANG_STYLE[language])
                cells["bar"] = cell(plain, rendered=rendered)
        else:
            for key in ("boot", "p50", "p95"):
                cells[key] = cell("-", "dim")
            cells["rps"] = cell("no run", "red", "bold")
        if is_cp3:
            cells["cpp"] = cell(ratio_cell(cpp_ratio), *ratio_style(cpp_ratio))
            cells["rust"] = cell(ratio_cell(rust_ratio), *ratio_style(rust_ratio))
        else:
            cells["cpp"] = cell("-", "dim")
            cells["rust"] = cell("-", "dim")
        if is_cp3 and table_rows:
            table_rows.append(None)
        table_rows.append({"label": language, "cells": cells,
                           "styles": ("bold",) if is_cp3 else ()})
    render_table(columns, table_rows)
    print_row_notes(rows)
    for row in rows:
        if ran[row["language"]] and row["errors"]:
            note_line(f"{g('warn')} {row['language']}", ("yellow", "bold"),
                      f"{row['errors']} request(s) failed during measurement", ("yellow",))


def _print_compute_like(task: str, rows: list[dict], header_text: str,
                        expected: str | None, with_median: bool) -> None:
    live_clear()
    banner(task, header_text)
    cp3_best = best_ms_of(rows, "cp3")
    cpp_ratio, rust_ratio = register_ratios(
        task, cp3_best, best_ms_of(rows, "cpp"), best_ms_of(rows, "rust"),
        best_ms_of(rows, "go"), best_ms_of(rows, "js"), best_ms_of(rows, "python"))

    bests = {r["language"]: (min(r["runs_ms"]) if r["runs_ms"] else None) for r in rows}
    medians = {r["language"]: (percentile(r["runs_ms"], 50) if r["runs_ms"] else None)
               for r in rows}
    fastest = min((v for v in bests.values() if v), default=None)
    best_best = best_langs(bests)
    best_median = best_langs(medians)
    best_rss = best_langs({r["language"]: r["peak_rss_mb"] for r in rows})
    best_bin = best_langs({r["language"]: r["binary_kb"] for r in rows})

    columns = [Col("lang", "lang", "l"), Col("build", "build"), Col("best", "best ms")]
    if with_median:
        columns.append(Col("median", "median ms"))
    columns += [
        Col("runs", "runs ms", "l", flex=True, min_width=10),
        Col("output", "output", "l", flex=True, min_width=16),
        Col("match", "match", "l"),
        Col("cpp", "cp3/cpp"),
        Col("rust", "cp3/rust"),
        Col("rss", "rss MiB"),
        Col("bin", "bin KiB"),
        Col("bar", "speed", "l", optional=True),
    ]

    reference = None
    table_rows: list = []
    for row in rows:
        language = row["language"]
        cached, _ = split_note(row["note"])
        is_cp3 = language == "cp3"
        cells = {
            "lang": lang_cell(language),
            "build": build_cell(row, cached),
            "rss": value_cell(rss_cell(row), language in best_rss),
            "bin": value_cell(binary_cell(row), language in best_bin),
        }
        if row["runs_ms"]:
            best = bests[language]
            digits = ".1f" if with_median else ".0f"
            cells["best"] = value_cell(format(best, digits), language in best_best)
            if with_median:
                cells["median"] = value_cell(format(medians[language], ".1f"),
                                             language in best_median)
            cells["runs"] = cell("/".join(f"{ms:.0f}" for ms in row["runs_ms"]), "dim")
            if reference is None:
                reference = row["output"]
                match = "ref"
            elif row["output"] == reference:
                match = "ok"
            else:
                match = "MISMATCH"
            if match == "ref":
                cells["match"] = cell("ref", "dim")
            elif match == "ok":
                cells["match"] = cell(f"{g('ok')} ok", "green")
            else:
                cells["match"] = cell(f"{g('bad')} MISMATCH", "red", "bold")
            cells["output"] = cell(row["output"] or "", "dim" if match != "MISMATCH" else "red")
            if fastest:
                plain, rendered = bar(fastest / best, 10, LANG_STYLE[language])
                cells["bar"] = cell(plain, rendered=rendered)
        else:
            cells["best"] = cell("-", "dim")
            if with_median:
                cells["median"] = cell("-", "dim")
            cells["runs"] = cell("-", "dim")
            cells["output"] = cell("-", "dim")
            cells["match"] = cell(f"{g('bad')} no run", "red", "bold")
        if is_cp3:
            cells["cpp"] = cell(ratio_cell(cpp_ratio), *ratio_style(cpp_ratio))
            cells["rust"] = cell(ratio_cell(rust_ratio), *ratio_style(rust_ratio))
        else:
            cells["cpp"] = cell("-", "dim")
            cells["rust"] = cell("-", "dim")
        if is_cp3 and table_rows:
            table_rows.append(None)
        table_rows.append({"label": language, "cells": cells,
                           "styles": ("bold",) if is_cp3 else ()})
    render_table(columns, table_rows)
    print_row_notes(rows)

    outputs = {row["output"] for row in rows if row["output"] is not None}
    if len(outputs) > 1:
        note_line(g("bad"), ("red", "bold"),
                  f"outputs differ across languages: {sorted(outputs)}", ("red",))
    if expected is not None:
        for row in rows:
            if row["runs_ms"] and row["output"] != expected:
                note_line(g("bad"), ("red", "bold"),
                          f"{task}: {row['language']} output {row['output']!r} "
                          f"does not match the oracle {expected!r}", ("red",))


def print_compute_table(task: str, rows: list[dict], header_text: str,
                        expected: str | None = None) -> None:
    _print_compute_like(task, rows, header_text, expected, with_median=False)


def print_hello_table(task: str, rows: list[dict], header_text: str,
                      expected: str | None = None) -> None:
    _print_compute_like(task, rows, header_text, expected, with_median=True)


def print_gate_and_summary() -> None:
    live_clear()
    banner("summary: cp3 vs native",
           "ratios are cp3 time divided by the reference time; lower is better "
           "(the tick marks 1.00x = parity)")
    axis = ("0" + " " * 9 + "1x").ljust(19) + "2x+"
    columns = [
        Col("task", "task", "l"),
        Col("cpp", "cp3/cpp"),
        Col("cppbar", axis, "l", optional=True),
        Col("rust", "cp3/rust"),
        Col("rustbar", axis, "l", optional=True),
    ]

    def ratio_cells(prefix: str, ratio: float | None, text: str | None = None) -> dict:
        style = ratio_style(ratio)
        plain, rendered = ratio_bar(ratio, style)
        return {
            prefix: cell(text if text is not None else ratio_cell(ratio), *style),
            prefix + "bar": cell(plain, rendered=rendered),
        }

    table_rows: list = []
    for row in SUMMARY_ROWS:
        cells = {"task": cell(row["task"])}
        cells.update(ratio_cells("cpp", row["cpp"]))
        cells.update(ratio_cells("rust", row["rust"]))
        table_rows.append({"label": row["task"], "cells": cells, "styles": ()})
    cpp_values = [row["cpp"] for row in SUMMARY_ROWS if row["cpp"] is not None]
    rust_values = [row["rust"] for row in SUMMARY_ROWS if row["rust"] is not None]
    cpp_geo = geomean(cpp_values)
    rust_geo = geomean(rust_values)
    cells = {"task": cell("geomean")}
    cells.update(ratio_cells("cpp", cpp_geo, f"{cpp_geo:.2f}x" if cpp_geo is not None else "n/a"))
    cells.update(ratio_cells("rust", rust_geo, f"{rust_geo:.2f}x" if rust_geo is not None else "n/a"))
    if table_rows:
        table_rows.append(None)
    table_rows.append({"label": "geomean", "cells": cells, "styles": ("bold",)})
    render_table(columns, table_rows)
    cpp_text = f"{cpp_geo:.2f}x (n={len(cpp_values)})" if cpp_geo is not None else "n/a"
    rust_text = f"{rust_geo:.2f}x (n={len(rust_values)})" if rust_geo is not None else "n/a"
    note_line(g("info"), ("dim",),
              f"geomean over runnable rows: cp3/cpp {cpp_text}{g('sep')}cp3/rust {rust_text}")

    banner("performance gate", GATE_TARGET)
    print("  " + paint(g("h") * max(10, min(UI.wrap, 110) - 2), "dim"))
    for blocker in GATE_BLOCKERS:
        note_line(f"{g('bad')} BLOCKER".ljust(10), ("red", "bold"),
                  re.sub(r"^CP3002 BLOCKER\s*", "", blocker), ("red",), indent=2)
    tier_re = re.compile(r"^(BIG WARN(?: \(fix this\))?|WARN):?\s*")
    for warning in GATE_WARNINGS:
        big = warning.startswith("BIG WARN")
        tag = f"{g('warn')} {'BIG WARN' if big else 'WARN'}".ljust(10)
        note_line(tag, ("red", "bold") if big else ("yellow", "bold"),
                  tier_re.sub("", warning, count=1),
                  ("red",) if big else ("yellow",), indent=2)
    for note in GATE_NOTES:
        note_line(g("info").ljust(10), ("dim",), re.sub(r"^note:\s*", "", note),
                  ("dim",), indent=2)
    print("  " + paint(g("h") * max(10, min(UI.wrap, 110) - 2), "dim"))
    if GATE_BLOCKERS:
        verdict = (f"gate: {len(GATE_BLOCKERS)} CP3002 blocker(s); "
                   f"a row that cannot compile is a hard failure")
        print(f"  {paint(g('bad'), 'red', 'bold')} {paint(verdict, 'red', 'bold')}")
    elif GATE_WARNINGS:
        big = sum(1 for warning in GATE_WARNINGS if warning.startswith("BIG WARN "))
        plain = len(GATE_WARNINGS) - big
        verdict = (f"gate: {big} BIG WARN(s), {plain} WARN(s); "
                   f"rerun with --strict to fail on warnings")
        print(f"  {paint(g('warn'), 'yellow', 'bold')} {paint(verdict, 'yellow', 'bold')}")
    else:
        print(f"  {paint(g('ok'), 'green', 'bold')} {paint('gate: no warnings', 'green', 'bold')}")
    print()


def cp3c_provenance() -> list[str]:
    """Provenance lines for the compiler binary, recorded in meta.txt.

    The SHA-256 pins the exact compiler that produced a labeled run (uppercase
    to match the convention in the README and the report); the mtime uses the
    same local timestamp shape as the `invoked` line."""
    if CP3C is None or not Path(CP3C).is_file():
        return [f"cp3c: {CP3C}", "cp3c sha256: (not found)", "cp3c mtime: (not found)"]
    digest = hashlib.sha256()
    with open(CP3C, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    mtime = datetime.fromtimestamp(Path(CP3C).stat().st_mtime).isoformat(timespec="seconds")
    return [f"cp3c: {CP3C}", f"cp3c sha256: {digest.hexdigest().upper()}",
            f"cp3c mtime: {mtime}"]


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------
class Tee:
    """Writes everywhere the runner prints: console plus the results file.

    The console keeps colour; the results file is always plain text."""

    def __init__(self, primary, secondary):
        self.primary = primary
        self.secondary = secondary

    def write(self, text: str):
        self.primary.write(text)
        self.secondary.write(ANSI_RE.sub("", text))

    def flush(self):
        self.primary.flush()
        self.secondary.flush()


def resolve_churn_scale(text: str) -> tuple[int, str]:
    if text in CHURN_SCALE_PRESETS:
        return CHURN_SCALE_PRESETS[text], text
    try:
        iterations = int(text)
    except ValueError:
        iterations = 0
    if iterations > 0:
        return iterations, f"custom ({iterations})"
    raise SystemExit(
        f"[runner] --churn-scale expects default, stress or a positive integer, got {text!r}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="benchmarks comparison suite (single entry point: benchmarks/run_benchmark.py)")
    parser.add_argument("--only", default="", help="comma list of: " + ",".join(ALL_TASKS))
    parser.add_argument("--seconds", type=float, default=3.0, help="throughput phase seconds per server")
    parser.add_argument("--concurrency", type=int, default=8, help="throughput phase connections")
    parser.add_argument("--clients", type=int, default=4,
                        help="client OS processes for the multicore server row (default 4)")
    parser.add_argument("--single-clients", type=int, default=2,
                        help="client OS processes for the single-core server row (default 2)")
    parser.add_argument("--latency", type=int, default=300, help="latency phase request count")
    parser.add_argument("--warmup", type=int, default=100, help="warmup requests before measuring")
    parser.add_argument("--reps", type=int, default=2, help="compute runs; best is reported")
    parser.add_argument("--hello-reps", type=int, default=15,
                        help="hello-world runs; best and median are reported (default 15)")
    parser.add_argument("--churn-scale", default="default",
                        help="string-churn scale: default, stress or a positive integer")
    parser.add_argument("--cp3c", default=str(ROOT / "build" / "bin" / f"cp3c{EXE}"),
                        help="path to cp3c (default: build/bin/cp3c.exe)")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 when the performance gate prints any WARN or BIG WARN")
    parser.add_argument("--results", default="",
                        help="directory to tee the raw output into (run.txt + meta.txt)")
    args = parser.parse_args()

    global CP3C
    CP3C = Path(args.cp3c)
    cp3c = CP3C
    BUILD.mkdir(exist_ok=True)

    if args.only.strip():
        wanted = [part.strip() for part in args.only.split(",") if part.strip()]
        unknown = [name for name in wanted if name not in ALL_TASKS]
        if unknown:
            print(f"[runner] unknown task(s): {', '.join(unknown)}")
            print(f"[runner] known tasks: {', '.join(ALL_TASKS)}")
            return 2
    else:
        wanted = list(ALL_TASKS)

    churn_iterations, churn_label = resolve_churn_scale(args.churn_scale)

    original_stdout = sys.stdout
    ui_setup(original_stdout)
    results_log = None
    if args.results:
        results_dir = Path(args.results)
        results_dir.mkdir(parents=True, exist_ok=True)
        meta = [
            f"invoked: {datetime.now().isoformat(timespec='seconds')}",
            f"argv: {sys.argv}",
            f"cwd: {os.getcwd()}",
            f"runner: python {platform.python_version()} ({sys.executable})",
            f"machine: {platform.platform()}",
            f"string-churn scale: {churn_label} ({churn_iterations} iterations)",
            *cp3c_provenance(),
        ]
        (results_dir / "meta.txt").write_text("\n".join(meta) + "\n", encoding="utf-8")
        results_log = open(results_dir / "run.txt", "w", encoding="utf-8", newline="")
        sys.stdout = Tee(original_stdout, results_log)

    try:
        probes = [
            ("g++", ["g++", "--version"]), ("rustc", ["rustc", "--version"]),
            ("go", ["go", "version"]), ("node", ["node", "--version"]),
            ("python", [sys.executable, "--version"]), ("cp3c", [str(cp3c), "version"]),
        ]
        print_header(platform.processor() or platform.machine(), probes)
        waiting = time_wait_count()
        if waiting is not None and waiting > 4000:
            print_time_wait_warning(waiting)

        def present(language: str) -> bool:
            if language == "cp3":
                return cp3c.exists()
            if language == "js":
                return have("node")
            if language == "python":
                return True
            return have({"cpp": "g++", "rust": "rustc", "go": "go"}[language])

        task_total = sum(1 for name in ALL_TASKS if name in wanted)
        task_index = 0

        for task in SERVER_TASKS:
            if task not in wanted:
                continue
            task_index += 1
            rows = []
            for language in LANGUAGES:
                if not present(language):
                    announce_skip(language)
                    continue
                announce_run(task, language, task_index, task_total)
                rows.append(run_server_trial(language, task, args.latency, args.warmup,
                                             args.seconds, args.concurrency, cp3c,
                                             clients=args.clients,
                                             single_clients=args.single_clients))
            print_server_table(task, rows)

        for task in COMPUTE_TASKS:
            if task not in wanted:
                continue
            task_index += 1
            extra: tuple[str, ...] = ()
            cp3_entry = None
            expected = EXPECTED.get(task)
            reps = max(1, args.reps)
            if task == "hello-world":
                reps = max(1, args.hello_reps)
                header = (f"expected = {EXPECTED[task]} | spawn -> exit wall time, "
                          f"best and median of {reps} runs")
            elif task == "string-churn":
                extra = (str(churn_iterations),)
                if churn_iterations != CHURN_DEFAULT_ITERATIONS:
                    cp3_entry = prepare_cp3_churn_variant(BENCHMARKS / task / "cp3", churn_iterations)
                expected = CHURN_EXPECTED.get(churn_iterations)
                oracle_text = expected if expected is not None else "cross-check only (custom scale)"
                header = (f"expected = {oracle_text} | scale: {churn_label} "
                          f"({churn_iterations} iterations)")
            elif task in ("compute-single", "compute-single-int"):
                header = f"expected = {EXPECTED[task]} (total Collatz steps for 1..1,000,000)"
            elif task == "compute-multi":
                header = f"expected = {EXPECTED[task]} (same total split across all cores)"
            elif task == "json-roundtrip":
                header = (f"expected = {EXPECTED[task]} | 20,000 parse+stringify "
                          f"round trips of one 310-byte record")
            elif task == "async-fanout":
                header = (f"expected = {EXPECTED[task]} | 10,000-timer fan-out joined "
                          f"at once, then 10,000 chained awaits")
            else:
                header = f"expected = {EXPECTED[task]}"
            rows = []
            omitted = TASK_OMIT_LANGUAGES.get(task, {})
            for language in LANGUAGES:
                if language in omitted:
                    announce_omit(task, language, omitted[language])
                    continue
                if not present(language):
                    announce_skip(language)
                    continue
                announce_run(task, language, task_index, task_total)
                rows.append(run_compute_trial(language, task, reps, cp3c,
                                              extra=extra, cp3_entry=cp3_entry))
            if task == "hello-world":
                print_hello_table(task, rows, header, expected=expected)
            else:
                print_compute_table(task, rows, header, expected=expected)

        print_gate_and_summary()
    finally:
        live_clear()
        if results_log is not None:
            sys.stdout = original_stdout
            results_log.close()

    if GATE_BLOCKERS:
        return 1
    if args.strict and GATE_WARNINGS:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())