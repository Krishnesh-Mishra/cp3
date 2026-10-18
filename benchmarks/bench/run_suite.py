#!/usr/bin/env python3
# =============================================================================
# benchmarks/bench/run_suite.py
#
# Dependency-free comparison-suite runner: builds and runs every benchmark
# entry available on THIS machine and prints comparison tables of real
# measurements. Nothing here is estimated; every number comes from a process
# this script started and stopped itself.
#
#   * health-server: build (if needed), boot->ready ms, /healthz latency
#     p50/p95 over N requests on persistent keep-alive sessions, req/s at fixed
#     concurrency for a few seconds. Processes are started, and killed, one at
#     a time; only processes this script started are ever stopped.
#   * compute-style tasks (compute-single, compute-single-int, compute-multi,
#     hello-world, string-churn, word-frequency, object-churn, array-growth,
#     closure-capture, interface-dispatch, generics-mono): build (if needed),
#     then run R times and report the best wall time; hello-world reports the
#     best and the median over at least 15 runs. Outputs are cross-checked
#     across languages and against the independent oracle values (see
#     bench/oracle.py) where one exists.
#   * cp3 entries that need unlanded features are attempted like everything
#     else; the compiler's own CP3xxx diagnostic is reported instead of a
#     timing. When a feature lands, the entry participates automatically.
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
# 1.10x cpp/rust but under the big tier. Compute references under 10 ms are
# noise-scale and not gated. D64 targets cp3 within about 5% of cpp/rust for
# the native-type tiers; --strict turns any tier line into exit code 1.
#
# Python 3 stdlib only. Usage:  python run_suite.py [options]
# =============================================================================
from __future__ import annotations

import argparse
import http.client
import math
import os
import platform
import re
import shutil
import subprocess
import sys
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

SERVER_TASKS = ["health-server"]
COMPUTE_TASKS = [
    "compute-single",
    "compute-single-int",
    "compute-multi",
    "hello-world",
    "string-churn",
    "word-frequency",
    "object-churn",
    "array-growth",
    "closure-capture",
    "interface-dispatch",
    "generics-mono",
]
ALL_TASKS = SERVER_TASKS + COMPUTE_TASKS

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
    "word-frequency": "37425343",
    "object-churn": "90641269",
    "array-growth": "200000010000000",
    "closure-capture": "16666688333331 998521500000",
    "interface-dispatch": "37999986 66000000",
    "generics-mono": "998986007 998986007",
}
CHURN_EXPECTED = {
    CHURN_DEFAULT_ITERATIONS: "852167",
    CHURN_STRESS_ITERATIONS: "254546",
}

GATE_BIG_NATIVE = 1.20   # BIG WARN when cp3 is over this multiple of cpp/rust time
GATE_WARN_NATIVE = 1.10  # WARN when cp3 is over this multiple, under the big tier
GATE_SCRIPT = 1.00       # BIG WARN when cp3 is slower than go/js/python at all
NOISE_FLOOR_MS = 10.0    # compute references faster than this are noise-scale
GATE_REFERENCES = ("cpp", "rust", "go", "js", "python")
NATIVE_REFERENCES = ("cpp", "rust")
GATE_TARGET = ("D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x "
               "cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust")

GATE_WARNINGS: list[str] = []   # tier lines (BIG WARN / WARN), printed at the end
GATE_NOTES: list[str] = []      # noise-scale notes, printed with the gate block
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
def server_build_spec(language: str) -> tuple[list[str], Path, Path] | None:
    """(build argv, output binary, source file), or None when there is no build step."""
    base = BENCHMARKS / "health-server"
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
    base = BENCHMARKS / "health-server"
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
    base = BENCHMARKS / "health-server"
    row: dict = {"language": language, "variant": "", "build_ms": None,
                 "boot_ms": None, "p50": None, "p95": None, "rps": None,
                 "requests": 0, "errors": 0, "peak_rss_mb": None,
                 "binary_kb": None, "note": ""}
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
        row["binary_kb"] = stripped_size_kb(Path(detail))
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
            row["binary_kb"] = stripped_size_kb(output)

    # --- run it -----------------------------------------------------------
    log_path = BUILD / f"health-server-{language}.log"
    with open(log_path, "wb") as log:
        process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT, cwd=base)
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
            sampler.stop()
            sampler.join(timeout=5)
            if sampler.peak is not None:
                row["peak_rss_mb"] = sampler.peak / 1048576.0
            process.kill()  # only ever the process this script started
            process.wait(timeout=15)
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
# reporting
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


def print_server_table(rows: list[dict]) -> None:
    print()
    print("== health-server ==")
    print(f"{'lang':<7} {'variant':<30} {'build_ms':>9} {'boot_ms':>8} "
          f"{'p50_ms':>8} {'p95_ms':>8} {'req/s':>9} {'cp3/cpp':>8} {'cp3/rust':>9} "
          f"{'peak_rss_mb':>11} {'binary_kb':>9}  note")
    print("-" * 160)
    cp3_row = next((r for r in rows if r["language"] == "cp3"), None)
    cp3_rps = cp3_row["rps"] if cp3_row and cp3_row["rps"] else None
    cpp_row = next((r for r in rows if r["language"] == "cpp"), None)
    rust_row = next((r for r in rows if r["language"] == "rust"), None)
    cpp_rps = cpp_row["rps"] if cpp_row and cpp_row["rps"] else None
    rust_rps = rust_row["rps"] if rust_row and rust_row["rps"] else None
    # throughput -> time ratio: cp3/cpp = cpp_rps / cp3_rps
    cpp_ratio = cpp_rps / cp3_rps if (cpp_rps and cp3_rps) else None
    rust_ratio = rust_rps / cp3_rps if (rust_rps and cp3_rps) else None
    SUMMARY_ROWS.append({"task": "health-server", "cpp": cpp_ratio, "rust": rust_ratio})
    go_rps = next((r["rps"] for r in rows if r["language"] == "go" and r["rps"]), None)
    js_rps = next((r["rps"] for r in rows if r["language"] == "js" and r["rps"]), None)
    python_rps = next((r["rps"] for r in rows
                       if r["language"] == "python" and r["rps"]), None)
    gate_row("health-server", cp3_rps,
             {"cpp": cpp_rps, "rust": rust_rps, "go": go_rps, "js": js_rps,
              "python": python_rps}, unit="rps")
    for row in rows:
        build_cell = f"{row['build_ms']:>9.0f}" if row["build_ms"] is not None else f"{'-':>9}"
        ratio_cpp = ratio_cell(cpp_ratio) if row["language"] == "cp3" else "-"
        ratio_rust = ratio_cell(rust_ratio) if row["language"] == "cp3" else "-"
        if row["note"].startswith("not runnable"):
            print(f"{row['language']:<7} {'-':<30} {build_cell} {'-':>8} "
                  f"{'-':>8} {'-':>8} {'-':>9} {ratio_cpp:>8} {ratio_rust:>9} "
                  f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
            continue
        if row["boot_ms"] is None:
            print(f"{row['language']:<7} {row['variant']:<30} {build_cell} {'-':>8} "
                  f"{'-':>8} {'-':>8} {'-':>9} {ratio_cpp:>8} {ratio_rust:>9} "
                  f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
            continue
        print(f"{row['language']:<7} {row['variant']:<30} {build_cell} "
              f"{row['boot_ms']:>8.1f} {row['p50']:>8.2f} {row['p95']:>8.2f} "
              f"{row['rps']:>9.1f} {ratio_cpp:>8} {ratio_rust:>9} "
              f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
        if row["errors"]:
            print(f"{'':<7} ^ {row['errors']} request(s) failed during measurement")


def print_compute_table(task: str, rows: list[dict], header_text: str,
                        expected: str | None = None) -> None:
    print()
    print(f"== {task} == {header_text}")
    print(f"{'lang':<7} {'build_ms':>9} {'best_ms':>9} {'all_runs_ms':<20} "
          f"{'output':<26} {'match':<7} {'cp3/cpp':>8} {'cp3/rust':>9} "
          f"{'peak_rss_mb':>11} {'binary_kb':>9}  note")
    print("-" * 165)
    cp3_best = best_ms_of(rows, "cp3")
    cpp_ratio, rust_ratio = register_ratios(
        task, cp3_best, best_ms_of(rows, "cpp"), best_ms_of(rows, "rust"),
        best_ms_of(rows, "go"), best_ms_of(rows, "js"), best_ms_of(rows, "python"))
    reference = None
    for row in rows:
        build_cell = f"{row['build_ms']:>9.0f}" if row["build_ms"] is not None else f"{'-':>9}"
        if not row["runs_ms"]:
            print(f"{row['language']:<7} {build_cell} {'-':>9} {'-':<20} "
                  f"{'-':<26} {'-':<7} {'-':>8} {'-':>9} "
                  f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
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
        ratio_cpp_cell = ratio_cell(cpp_ratio) if row["language"] == "cp3" else "-"
        ratio_rust_cell = ratio_cell(rust_ratio) if row["language"] == "cp3" else "-"
        print(f"{row['language']:<7} {build_cell} {best:>9.0f} {runs:<20} "
              f"{(row['output'] or ''):<26} {match:<7} {ratio_cpp_cell:>8} {ratio_rust_cell:>9} "
              f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
    outputs = {row["output"] for row in rows if row["output"] is not None}
    if len(outputs) > 1:
        print(f"!! outputs differ across languages: {sorted(outputs)}")
    if expected is not None:
        for row in rows:
            if row["runs_ms"] and row["output"] != expected:
                print(f"!! {task}: {row['language']} output {row['output']!r} "
                      f"does not match the oracle {expected!r}")


def print_hello_table(task: str, rows: list[dict], header_text: str,
                      expected: str | None = None) -> None:
    print()
    print(f"== {task} == {header_text}")
    print(f"{'lang':<7} {'build_ms':>9} {'best_ms':>9} {'median_ms':>9} {'all_runs_ms':<20} "
          f"{'output':<16} {'match':<7} {'cp3/cpp':>8} {'cp3/rust':>9} "
          f"{'peak_rss_mb':>11} {'binary_kb':>9}  note")
    print("-" * 170)
    cp3_best = best_ms_of(rows, "cp3")
    cpp_ratio, rust_ratio = register_ratios(
        task, cp3_best, best_ms_of(rows, "cpp"), best_ms_of(rows, "rust"),
        best_ms_of(rows, "go"), best_ms_of(rows, "js"), best_ms_of(rows, "python"))
    reference = None
    for row in rows:
        build_cell = f"{row['build_ms']:>9.0f}" if row["build_ms"] is not None else f"{'-':>9}"
        if not row["runs_ms"]:
            print(f"{row['language']:<7} {build_cell} {'-':>9} {'-':>9} {'-':<20} "
                  f"{'-':<16} {'-':<7} {'-':>8} {'-':>9} "
                  f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
            continue
        best = min(row["runs_ms"])
        median = percentile(row["runs_ms"], 50)
        runs = "/".join(f"{ms:.0f}" for ms in row["runs_ms"])
        if reference is None:
            reference = row["output"]
            match = "ref"
        elif row["output"] == reference:
            match = "ok"
        else:
            match = "MISMATCH"
        ratio_cpp_cell = ratio_cell(cpp_ratio) if row["language"] == "cp3" else "-"
        ratio_rust_cell = ratio_cell(rust_ratio) if row["language"] == "cp3" else "-"
        print(f"{row['language']:<7} {build_cell} {best:>9.1f} {median:>9.1f} {runs:<20} "
              f"{(row['output'] or ''):<16} {match:<7} {ratio_cpp_cell:>8} {ratio_rust_cell:>9} "
              f"{rss_cell(row):>11} {binary_cell(row):>9}  {row['note']}")
    outputs = {row["output"] for row in rows if row["output"] is not None}
    if len(outputs) > 1:
        print(f"!! outputs differ across languages: {sorted(outputs)}")
    if expected is not None:
        for row in rows:
            if row["runs_ms"] and row["output"] != expected:
                print(f"!! {task}: {row['language']} output {row['output']!r} "
                      f"does not match the oracle {expected!r}")


def print_gate_and_summary() -> None:
    print()
    print("== summary: cp3 vs native ==")
    print(f"{'task':<20} {'cp3/cpp':>9} {'cp3/rust':>10}")
    print("-" * 42)
    for row in SUMMARY_ROWS:
        print(f"{row['task']:<20} {ratio_cell(row['cpp']):>9} {ratio_cell(row['rust']):>10}")
    cpp_values = [row["cpp"] for row in SUMMARY_ROWS if row["cpp"] is not None]
    rust_values = [row["rust"] for row in SUMMARY_ROWS if row["rust"] is not None]
    cpp_geo = geomean(cpp_values)
    rust_geo = geomean(rust_values)
    print("-" * 42)
    cpp_text = f"{cpp_geo:.2f}x (n={len(cpp_values)})" if cpp_geo is not None else "n/a"
    rust_text = f"{rust_geo:.2f}x (n={len(rust_values)})" if rust_geo is not None else "n/a"
    print(f"{'geomean over runnable rows':<20} {cpp_text:>15} {rust_text:>15}")
    print("ratios are cp3 time divided by the reference time; lower is better")

    print()
    print(f"== performance gate == {GATE_TARGET}")
    for warning in GATE_WARNINGS:
        print(warning)
    for note in GATE_NOTES:
        print(note)
    if GATE_WARNINGS:
        big = sum(1 for warning in GATE_WARNINGS if warning.startswith("BIG WARN "))
        plain = len(GATE_WARNINGS) - big
        print(f"gate: {big} BIG WARN(s), {plain} WARN(s); "
              f"rerun with --strict to fail on warnings")
    else:
        print("gate: no warnings")


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------
class Tee:
    """Writes everywhere the runner prints: console plus the results file."""

    def __init__(self, primary, secondary):
        self.primary = primary
        self.secondary = secondary

    def write(self, text: str):
        self.primary.write(text)
        self.secondary.write(text)

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
        ]
        (results_dir / "meta.txt").write_text("\n".join(meta) + "\n", encoding="utf-8")
        results_log = open(results_dir / "run.txt", "w", encoding="utf-8", newline="")
        sys.stdout = Tee(original_stdout, results_log)

    try:
        print(f"benchmarks bench suite -- {datetime.now().isoformat(timespec='seconds')}")
        print(f"machine: {platform.processor() or platform.machine()} | "
              f"{os.cpu_count()} logical CPUs | {platform.platform()}")
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

        for task in COMPUTE_TASKS:
            if task not in wanted:
                continue
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
            else:
                header = f"expected = {EXPECTED[task]}"
            rows = []
            for language in LANGUAGES:
                if not present(language):
                    print(f"[skip] {language}: toolchain not found on PATH")
                    continue
                print(f"[run ] {task}: {language} ...", flush=True)
                rows.append(run_compute_trial(language, task, reps, cp3c,
                                              extra=extra, cp3_entry=cp3_entry))
            if task == "hello-world":
                print_hello_table(task, rows, header, expected=expected)
            else:
                print_compute_table(task, rows, header, expected=expected)

        print_gate_and_summary()
    finally:
        if results_log is not None:
            sys.stdout = original_stdout
            results_log.close()

    if args.strict and GATE_WARNINGS:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
