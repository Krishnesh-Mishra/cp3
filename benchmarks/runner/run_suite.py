#!/usr/bin/env python3
# =============================================================================
# benchmarks/runner/run_suite.py
#
# Comparison-suite runner: builds and runs every benchmark entry available on
# THIS machine and prints comparison tables of real measurements. Nothing here
# is estimated; every number comes from a process this script started and
# stopped itself. Compute rows use each language's native standard library;
# the two HTTP rows use the best available framework per language (Gin, Axum,
# Drogon, Express + Fastify, FastAPI, Undertow) with a dependency-free fallback
# where the local toolchain is unavailable.
#
#   * health-server and health-server-multi: build (if needed), boot->ready
#     ms, /healthz latency p50/p95 over N requests on persistent keep-alive
#     sessions, req/s at fixed concurrency for a few seconds. The multicore
#     row is driven by several client OS processes. Server processes are
#     started and stopped one at a time, together with the process tree each
#     one created (cluster workers, multiprocessing workers). Each language is
#     measured over R independent server runs (default 5): boot and throughput
#     use trial medians, while latency percentiles use all pooled samples.
#   * compute-style tasks (compute-single, compute-single-int, compute-multi,
#     hello-world, string-churn, sha256-churn, buffer-copy, typed-array-sum,
#     math-kernel, word-frequency, object-churn, array-growth, closure-capture,
#     interface-dispatch, generics-mono, json-roundtrip, async-fanout,
#     env-injection, file-roundtrip, path-join, date-format, os-probe): build
#     (if needed), then run R times and report the median wall time, with best,
#     p95 and run-to-run variability alongside it. Outputs are
#     cross-checked across languages and against the independent oracle values
#     (see runner/oracle.py) where one exists. json-roundtrip, async-fanout and
#     sha256-churn omit cpp/rust on purpose (no stdlib JSON surface, no stdlib
#     timer-event loop, no stdlib SHA-256); date-format and os-probe omit rust
#     (no stdlib timezone formatter / hostname+platform probe). The runner
#     prints the reason per row and still gates the remaining languages.
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
# Every server and compute row carries a direct "vs cp3" comparison: how many
# times slower (or faster) that language is than cp3. Tables are sorted by the
# primary performance metric, fastest first; incomplete rows stay at the bottom.
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
import io
import json
import math
import os
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import textwrap
import threading
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent          # benchmarks/runner
BENCHMARKS = HERE.parent                        # benchmarks
ROOT = BENCHMARKS.parent                        # repo root
CASES = BENCHMARKS / "cases"
CACHE = BENCHMARKS / ".cache"
BUILD = CACHE / "build"
DEPS = CACHE / "deps"

# Split registry: tasks.py (task lists, categories, expected outputs) +
# langs.py (language lists, gate references). Adding a benchmark only
# touches tasks.py + a new cases/ folder; this file only dispatches.
import sys as _sys
_sys.path.insert(0, str(HERE))
try:
    from tasks import (  # noqa: E402
        SERVER_TASKS as _SERVER_TASKS,
        COMPUTE_TASKS as _COMPUTE_TASKS,
        TASK_OMIT_LANGUAGES as _TASK_OMIT,
        EXPECTED as _EXPECTED,
        CHURN_DEFAULT_ITERATIONS as _CHURN_DEF,
        CHURN_STRESS_ITERATIONS as _CHURN_STRESS,
        CHURN_SCALE_PRESETS as _CHURN_PRESETS,
        ENV_INJECTION_VARIABLE as _ENV_VAR,
        ENV_INJECTION_VALUE as _ENV_VAL,
        find_task_dir as _find_task_dir,
        category_of as _category_of,
        task_exists as _task_exists,
    )
    from langs import (  # noqa: E402
        COMPUTE_LANGUAGES as _COMPUTE_LANGS,
        SERVER_LANGUAGES as _SERVER_LANGS,
        GATE_REFERENCES as _GATE_REFS,
        NATIVE_REFERENCES as _NATIVE_REFS,
        MULTICORE_TASKS as _MULTICORE,
        GATE_BIG_NATIVE as _GATE_BIG,
        GATE_WARN_NATIVE as _GATE_WARN,
        GATE_SCRIPT as _GATE_SCRIPT,
        GATE_MULTI as _GATE_MULTI,
        NOISE_FLOOR_MS as _NOISE_FLOOR,
        language_present as _lang_present,
    )
    _REGISTRY_OK = True
except Exception:  # pragma: no cover - fallback keeps suite running without registry
    _REGISTRY_OK = False
    _COMPUTE_LANGS = ["cpp", "rust", "go", "js", "python", "java", "cp3"]
    _SERVER_LANGS = ["cpp", "rust", "go", "js", "js-fastify", "python", "java", "cp3"]
    _GATE_REFS = ("cpp", "rust", "go", "js", "python", "java")
    _NATIVE_REFS = ("cpp", "rust")
    _MULTICORE = {"compute-multi", "health-server-multi"}
    _GATE_BIG, _GATE_WARN, _GATE_SCRIPT, _GATE_MULTI, _NOISE_FLOOR = 1.20, 1.05, 1.00, 1.00, 10.0

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
        "java": 18086,
        "js-fastify": 18087,
        "dotnet": 18088,
        "goose": 18089,
    },
    "health-server-multi": {
        "cpp": 18090,
        "rust": 18091,
        "go": 18092,
        "js": 18093,
        "python": 18094,
        "cp3": 18095,
        "java": 18096,
        "js-fastify": 18097,
        "dotnet": 18098,
        "goose": 18099,
    },
}
# Compute rows keep the native standard library for each language. Server rows
# add the framework rows: Java (Spring Boot preferred, Undertow fallback),
# .NET (Kestrel) and a second JS row for Fastify. Express is the `js` row.
COMPUTE_LANGUAGES = _COMPUTE_LANGS
SERVER_LANGUAGES = _SERVER_LANGS
LANGUAGES = COMPUTE_LANGUAGES  # kept for callers that need a default ordering
CURRENT_LANGUAGES = COMPUTE_LANGUAGES  # set per task loop for progress UI

# Framework dependencies live outside git under benchmarks/.cache/deps. The
# runner discovers them if the setup scripts have been run; if not, it falls
# back to the dependency-free entry where one exists.
JAVA_DEPS = DEPS / "java" / "undertow"


def default_vcpkg_root() -> Path:
    """VCPKG_ROOT when set, otherwise the usual local install locations."""
    env = os.environ.get("VCPKG_ROOT")
    if env:
        return Path(env)
    candidates = [
        Path(r"C:\vcpkg"),
        Path.home() / "dev" / "vcpkg",
        Path.home() / "vcpkg",
    ]
    for candidate in candidates:
        if (candidate / "vcpkg.exe").exists():
            return candidate
    return candidates[0]


VCPKG_ROOT = default_vcpkg_root()
DROGON_PREFIX = Path(os.environ.get(
    "DROGON_PREFIX", str(VCPKG_ROOT / "installed" / "x64-windows")))
SERVER_RUST_BIN = {
    "health-server": "health-server-rust",
    "health-server-multi": "health-server-multi-rust",
}

SERVER_TASKS = _SERVER_TASKS
COMPUTE_TASKS = _COMPUTE_TASKS
ALL_TASKS = SERVER_TASKS + COMPUTE_TASKS
TASK_OMIT_LANGUAGES = _TASK_OMIT if _REGISTRY_OK else {}
CHURN_DEFAULT_ITERATIONS = _CHURN_DEF if _REGISTRY_OK else 50000
CHURN_STRESS_ITERATIONS = _CHURN_STRESS if _REGISTRY_OK else 300000
CHURN_SCALE_PRESETS = _CHURN_PRESETS if _REGISTRY_OK else {"default": 50000, "stress": 300000}
ENV_INJECTION_VARIABLE = _ENV_VAR if _REGISTRY_OK else "CP3_BENCH_ENV"
ENV_INJECTION_VALUE = _ENV_VAL if _REGISTRY_OK else "cp3-benchmark-value"
EXPECTED = _EXPECTED if _REGISTRY_OK else {}
CHURN_EXPECTED = {CHURN_DEFAULT_ITERATIONS: "852167", CHURN_STRESS_ITERATIONS: "254546"}
GATE_BIG_NATIVE = _GATE_BIG
GATE_WARN_NATIVE = _GATE_WARN
GATE_SCRIPT = _GATE_SCRIPT
GATE_MULTI = _GATE_MULTI
NOISE_FLOOR_MS = _NOISE_FLOOR
GATE_REFERENCES = _GATE_REFS
NATIVE_REFERENCES = _NATIVE_REFS
MULTICORE_TASKS = _MULTICORE

# Per-task language omissions. These rows compare standard-library surfaces
# that the omitted language does not have: no JSON parser/serializer, no
# timer/event-loop surface, and no SHA-256 in C++ std or Rust std without a
# crate; no timezone-aware calendar formatter or hostname/platform probe in
# Rust std without libc or a crate. A single dependency-free file would have to
# vendor or hand-roll the missing stack, which compares dependencies, not the
# language. The reason is printed per row, and these languages are skipped
# only for these tasks; the gate still applies to the remaining languages.
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
    "date-format": {
        "rust": "no timezone-aware date formatting in Rust std without a crate; adding chrono compares dependencies",
    },
    "os-probe": {
        "rust": "no hostname/platform probe in Rust std without libc or a crate; hand-rolling it compares bindings",
    },
    "regex-scan": {
        "rust": "no regex engine in Rust std; adding the regex crate compares dependencies",
    },
    "json-parse-large": {
        "cpp": "no JSON in the C++ standard library; a dependency-free file would vendor a parser",
        "rust": "no JSON in Rust std without a crate; adding serde_json compares dependencies",
    },
    "exception-cost": {
        "rust": "no exceptions in Rust; Result is the returned-value path, not throw/catch",
        "go": "no exceptions in Go; error values are the returned-value path, not throw/catch",
    },
}

# Workload scale for string-churn. The cp3 entry carries the scale as a
# compile-time constant; the runner builds a copy with that constant
# substituted for any non-default scale.
CHURN_DEFAULT_ITERATIONS = 50000
CHURN_STRESS_ITERATIONS = 300000
CHURN_SCALE_PRESETS = {"default": CHURN_DEFAULT_ITERATIONS, "stress": CHURN_STRESS_ITERATIONS}

# env-injection: the runner injects this one variable into every child process
# for that task only (merged over os.environ). The checksum folds the value's
# bytes, so a different value produces a different output.
ENV_INJECTION_VARIABLE = "CP3_BENCH_ENV"
ENV_INJECTION_VALUE = "cp3-benchmark-value"

# Expected outputs. Fixed-value tasks are embedded here; string-churn values
# depend on the chosen scale. All values were computed independently by
# benchmarks/runner/oracle.py, which re-implements every workload in plain
# Python; see benchmarks/README.md ("measured, not estimated").
EXPECTED = {
    "compute-single": "131434424",
    "compute-single-int": "131434424",
    "compute-multi": "131434424",
    "hello-world": "Hello, world!",
    "sha256-churn": "218229",
    "buffer-copy": "688024",
    "typed-array-sum": "135618465",
    "math-kernel": "177047762",
    "word-frequency": "37425343",
    "object-churn": "90641269",
    "array-growth": "200000010000000",
    "closure-capture": "16666688333331 998521500000",
    "interface-dispatch": "37999986 66000000",
    "generics-mono": "998986007 998986007",
    "json-roundtrip": "664346",
    "async-fanout": "99898",
    "env-injection": "133644",
    "file-roundtrip": "698140",
    "path-join": "425577",
    "date-format": "796897",
    "os-probe": "208981",
    "binary-trees": "1348958",
    "map-churn": "942471769",
    "utf8-text": "78156718",
    "regex-scan": "735703090",
    "json-parse-large": "613781",
    "deep-recursion": "292249866",
    "exception-cost": "446742259 446742259",
    "file-big": "779913",
    "sum-array": "4000000\n15987891712",
}
CHURN_EXPECTED = {
    CHURN_DEFAULT_ITERATIONS: "852167",
    CHURN_STRESS_ITERATIONS: "254546",
}

GATE_BIG_NATIVE = _GATE_BIG
GATE_WARN_NATIVE = _GATE_WARN
GATE_SCRIPT = _GATE_SCRIPT
GATE_MULTI = _GATE_MULTI
NOISE_FLOOR_MS = _NOISE_FLOOR
GATE_REFERENCES = _GATE_REFS
NATIVE_REFERENCES = _NATIVE_REFS
MULTICORE_TASKS = _MULTICORE
GATE_TARGET = ("D64 target: cp3 within about 5% of cpp/rust (WARN over 1.05x); "
               "BIG WARN over 1.20x cpp/rust or slower than go/js/python/java/dotnet/goose at all; "
               "multi-core rows must be faster than cpp")

# Registry omits (calc/interp) merge in on top of the legacy block above.
if _REGISTRY_OK:
    for _k, _v in _TASK_OMIT.items():
        TASK_OMIT_LANGUAGES.setdefault(_k, _v)

GATE_WARNINGS: list[str] = []   # tier lines (BIG WARN / WARN), printed at the end
GATE_NOTES: list[str] = []      # noise-scale notes, printed with the gate block
LOAD_PROCS: list = []   # live load-client processes, killed on Ctrl+C
TASK_SECTIONS: dict = {}   # task -> (stamp, text) for results.md
GATE_BLOCKERS: list[str] = []   # CP3002 rows that cannot be measured at all


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------
def percentile(values: list[float], p: float) -> float:
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = round((p / 100.0) * (len(ordered) - 1))
    return ordered[max(0, min(len(ordered) - 1, index))]


def median(values: list[float]) -> float | None:
    """Conventional median; unlike a best-of measurement this resists outliers."""
    return statistics.median(values) if values else None


def cv_percent(values: list[float]) -> float | None:
    """Sample coefficient of variation, as a percentage of the mean."""
    if len(values) < 2:
        return None
    average = statistics.fmean(values)
    if average == 0:
        return None
    return statistics.stdev(values) / abs(average) * 100.0


def cv_cell(values: list[float]) -> str:
    value = cv_percent(values)
    return f"{value:.1f}" if value is not None else "-"


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


def run_measured(argv: list[str], cwd: Path | None = None, timeout: float = 900.0,
                 env: dict[str, str] | None = None):
    """run_command plus wall time plus peak-RSS sampling of the child process.

    Returns (CompletedProcess, wall_ms, peak_bytes_or_None). The peak is the
    kernel-maintained PeakWorkingSetSize sampled from Python; on other
    platforms, or when the process exits before a sample lands, it is None and
    every report prints '-' honestly. `env` is the complete child environment
    (callers merge over os.environ); None means inherit."""
    start = time.perf_counter()
    process = subprocess.Popen(
        argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        env=env
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
def newest_mtime(paths: list[Path]) -> float:
    return max((path.stat().st_mtime for path in paths if path.exists()), default=0.0)


def output_current(output: Path, inputs: list[Path]) -> bool:
    return output.exists() and output.stat().st_mtime >= newest_mtime(inputs)


def find_built_exe(root: Path, preferred: str | None = None) -> Path | None:
    """Newest executable under a build tree, preferring a known target name."""
    candidates = [path for path in root.rglob(f"*{EXE}")
                  if path.is_file() and path.stat().st_size > 0]
    if preferred:
        named = [path for path in candidates if path.stem == preferred]
        if not named:
            return None
        candidates = named
    return max(candidates, key=lambda path: path.stat().st_mtime, default=None)


def java_classpath() -> str:
    jars = sorted(JAVA_DEPS.glob("*.jar"))
    return os.pathsep.join(str(jar) for jar in jars)


def python_server_executable() -> Path:
    """The FastAPI venv interpreter when the setup script has been run."""
    candidates = [
        DEPS / "python" / "Scripts" / "python.exe",
        DEPS / "python" / "bin" / "python",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return Path(sys.executable)


def prepare_server_entry(language: str, task: str, port: int,
                         row: dict) -> tuple[bool, list[str], str, str]:
    """Build and return (ok, argv, variant, note) for a non-cp3 server row."""
    base = _find_task_dir(task) if _REGISTRY_OK else (CASES / task)
    stem = task
    note = ""

    if language == "cpp":
        cpp_dir = base / "cpp"
        cmake_file = cpp_dir / "CMakeLists.txt"
        drogon_source = cpp_dir / "drogon_main.cpp"
        if cmake_file.exists() and drogon_source.exists() and DROGON_PREFIX.exists():
            build_dir = BUILD / f"{stem}-cpp-drogon"
            target = f"{stem}-drogon"
            output = find_built_exe(build_dir, target)
            inputs = [cmake_file, drogon_source]
            if output is None or not output_current(output, inputs):
                start = time.perf_counter()
                toolchain = VCPKG_ROOT / "scripts" / "buildsystems" / "vcpkg.cmake"
                configure = [
                    "cmake", "-S", str(cpp_dir), "-B", str(build_dir),
                    "-G", "Visual Studio 18 2026", "-A", "x64",
                    "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_PREFIX_PATH={DROGON_PREFIX}",
                ]
                if toolchain.exists():
                    configure.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
                result = run_command(configure, timeout=600)
                if result.returncode != 0:
                    note = f"Drogon configure failed: {first_diagnostic(result.stderr or result.stdout)}"
                else:
                    result = run_command(
                        ["cmake", "--build", str(build_dir), "--config", "Release"],
                        timeout=1200)
                    if result.returncode != 0:
                        note = f"Drogon build failed: {first_diagnostic(result.stderr or result.stdout)}"
                row["build_ms"] = (time.perf_counter() - start) * 1000.0
                output = find_built_exe(build_dir, target)
            else:
                note = "build: cached"
            if output is not None and not note.startswith("Drogon"):
                row["binary_kb"] = stripped_size_kb(output)
                return True, [str(output), str(port)], "Drogon (MSVC, best C++ HTTP)", note
            note = (note + "; " if note else "") + "falling back to raw sockets"
        # Dependency-free fallback: keep the benchmark runnable even when the
        # Drogon/vcpkg toolchain is not present.
        output = BUILD / f"{stem}-cpp{EXE}"
        source = cpp_dir / "main.cpp"
        if not output_current(output, [source]):
            start = time.perf_counter()
            argv = ["g++", "-std=c++20", "-O2", "-o", str(output), str(source)]
            if os.name == "nt":
                argv.append("-lws2_32")
            result = run_command(argv)
            row["build_ms"] = (time.perf_counter() - start) * 1000.0
            if result.returncode != 0:
                return False, [], "", f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
        else:
            note = "build: cached"
        row["binary_kb"] = stripped_size_kb(output)
        return True, [str(output), str(port)], "raw sockets (Drogon unavailable)", note

    if language == "rust":
        rust_dir = base / "rust"
        cargo_file = rust_dir / "Cargo.toml"
        source = rust_dir / "src" / "main.rs"
        if not source.exists():
            source = rust_dir / "main.rs"
        if cargo_file.exists():
            binary_name = SERVER_RUST_BIN[task]
            output = rust_dir / "target" / "release" / f"{binary_name}{EXE}"
            inputs = [cargo_file, source, rust_dir / "Cargo.lock"]
            if not output_current(output, inputs):
                start = time.perf_counter()
                result = run_command(["cargo", "build", "--release"], cwd=rust_dir, timeout=900)
                row["build_ms"] = (time.perf_counter() - start) * 1000.0
                if result.returncode != 0:
                    return False, [], "", f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
            else:
                note = "build: cached"
            row["binary_kb"] = stripped_size_kb(output)
            return True, [str(output), str(port)], "Axum + Tokio (best Rust HTTP)", note
        output = BUILD / f"{stem}-rust{EXE}"
        if not output_current(output, [source]):
            start = time.perf_counter()
            result = run_command(["rustc", "-O", "-o", str(output), str(source)])
            row["build_ms"] = (time.perf_counter() - start) * 1000.0
            if result.returncode != 0:
                return False, [], "", f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
        else:
            note = "build: cached"
        row["binary_kb"] = stripped_size_kb(output)
        return True, [str(output), str(port)], "std::net (Axum unavailable)", note

    if language == "go":
        go_dir = base / "go"
        output = BUILD / f"{stem}-go{EXE}"
        source = go_dir / "main.go"
        inputs = [source, go_dir / "go.mod", go_dir / "go.sum"]
        if not output_current(output, inputs):
            start = time.perf_counter()
            if (go_dir / "go.mod").exists():
                argv = ["go", "build", "-o", str(output), "."]
                cwd = go_dir
            else:
                argv = ["go", "build", "-o", str(output), str(source)]
                cwd = None
            result = run_command(argv, cwd=cwd, timeout=600)
            row["build_ms"] = (time.perf_counter() - start) * 1000.0
            if result.returncode != 0:
                return False, [], "", f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
        else:
            note = "build: cached"
        row["binary_kb"] = stripped_size_kb(output)
        return True, [str(output), str(port)], "Gin (best Go HTTP)", note

    if language == "java":
        # Prefer Spring Boot (java-spring/) when present; Undertow fallback.
        spring_dir = base / "java-spring"
        spring_jar = spring_dir / "target" / f"{stem}-spring.jar"
        if (spring_dir / "pom.xml").exists() or (spring_dir / "build.gradle").exists():
            if not spring_jar.exists():
                start = time.perf_counter()
                if (spring_dir / "pom.xml").exists():
                    argv = ["mvn", "package", "-DskipTests", "-q"]
                else:
                    argv = ["./gradlew", "bootJar", "--offline"]
                result = run_command(argv, cwd=spring_dir, timeout=1200)
                row["build_ms"] = (time.perf_counter() - start) * 1000.0
                if result.returncode != 0:
                    return False, [], "", f"Spring Boot build failed: {first_diagnostic(result.stderr or result.stdout)}"
            row["binary_kb"] = stripped_size_kb(spring_jar) if spring_jar.exists() else None
            return (True, ["java", "-jar", str(spring_jar), f"--server.port={port}"],
                    "Spring Boot (Tomcat, best Java HTTP)", note)
        source = base / "java" / "Main.java"
        outdir = BUILD / f"{stem}-java-server"
        class_file = outdir / "Main.class"
        if not JAVA_DEPS.is_dir() or not any(JAVA_DEPS.glob("*.jar")):
            return False, [], "", "Java server deps missing: run benchmarks/runner/setup-java-undertow.ps1"
        cp = java_classpath()
        if not output_current(class_file, [source, *JAVA_DEPS.glob("*.jar")]):
            start = time.perf_counter()
            outdir.mkdir(parents=True, exist_ok=True)
            result = run_command(["javac", "-cp", cp, "-d", str(outdir), str(source)], timeout=300)
            row["build_ms"] = (time.perf_counter() - start) * 1000.0
            if result.returncode != 0:
                return False, [], "", f"build failed: {first_diagnostic(result.stderr or result.stdout)}"
        else:
            note = "build: cached"
        row["binary_kb"] = stripped_size_kb(class_file)
        return True, ["java", "-cp", os.pathsep.join([str(outdir), cp]), "Main", str(port)], (
            "Undertow (best Java HTTP)"), note

    if language == "dotnet":
        csproj = base / "dotnet" / f"{stem}.csproj"
        if not csproj.exists():
            return False, [], "", "dotnet entry missing"
        dll = base / "dotnet" / "bin" / "Release" / "net8.0" / f"{stem}.dll"
        if not dll.exists():
            start = time.perf_counter()
            result = run_command(["dotnet", "publish", "-c", "Release", "-o", str(base / "dotnet" / "publish")],
                                 cwd=base / "dotnet", timeout=900)
            row["build_ms"] = (time.perf_counter() - start) * 1000.0
            if result.returncode != 0:
                return False, [], "", f"dotnet publish failed: {first_diagnostic(result.stderr or result.stdout)}"
            dll = base / "dotnet" / "publish" / f"{stem}.dll"
        row["binary_kb"] = stripped_size_kb(dll) if dll.exists() else None
        return True, ["dotnet", str(dll), str(port)], "ASP.NET Core (Kestrel)", note

    if language == "goose":
        return False, [], "", "goose server not supported (compute only)"

    if language == "js":
        express = base / "js" / "express" / "server.mjs"
        if express.exists() and (base / "js" / "express" / "node_modules").is_dir():
            return True, ["node", str(express), str(port)], "Express (installed)", ""
        return True, ["node", str(base / "js" / "main.mjs"), str(port)], (
            "node:cluster" if task.endswith("-multi") else "node:http (zero-dep)"), ""

    if language == "js-fastify":
        fastify = base / "js" / "fastify" / "server.mjs"
        if fastify.exists() and (base / "js" / "fastify" / "node_modules").is_dir():
            return True, ["node", str(fastify), str(port)], "Fastify (installed)", ""
        return False, [], "", "Fastify not installed: run npm install in js/fastify"

    if language == "python":
        fastapi = base / "python" / "fastapi" / "main.py"
        python = python_server_executable()
        if fastapi.exists() and python != Path(sys.executable):
            return True, [str(python), str(fastapi), str(port)], (
                "FastAPI + Uvicorn (workers)" if task.endswith("-multi")
                else "FastAPI + Uvicorn"), ""
        return True, [sys.executable, str(base / "python" / "main.py"), str(port)], (
            "multiprocessing (shared socket)" if task.endswith("-multi")
            else "http.server (threaded; FastAPI venv missing)"), ""

    raise ValueError(language)


def try_cp3_build(entry: Path) -> tuple[bool, str]:
    """Build a cp3 entry. Returns (ok, diagnostic-or-exe-path)."""
    # Keep the generated C++ and the runtime it calls inspectable for every
    # benchmark build: .cp3/build/core/cp3-runtime/ is a snapshot only, so it
    # does not change the measured executable.
    result = run_command([str(CP3C), "build", "src/index.cp3", "--show-runtime"],
                         cwd=entry)
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
    LOAD_PROCS[:] = procs
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
    base = _find_task_dir(task) if _REGISTRY_OK else (CASES / task)
    row: dict = {"language": language, "variant": "", "build_ms": None,
                 "boot_ms": None, "p50": None, "p95": None, "rps": None,
                 "requests": 0, "errors": 0, "peak_rss_mb": None,
                 "binary_kb": None, "note": "", "latencies": []}
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
        ok, argv, variant, detail = prepare_server_entry(language, task, port, row)
        row["variant"] = variant
        if not ok:
            row["note"] = detail
            return row
        if detail:
            row["note"] = detail

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
            row["latencies"] = latencies
            row["p50"] = median(latencies)
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


def run_server_repeated(language: str, task: str, reps: int, latency_count: int,
                        warmup: int, seconds: float, concurrency: int, cp3c: Path,
                        clients: int = 4, single_clients: int = 2) -> dict:
    """Run independent server processes and combine their measured trials.

    Boot and throughput use medians across trials. Latency percentiles are
    calculated over the pooled latency requests from every successful trial,
    which uses all samples instead of averaging percentiles. A failed attempt
    remains visible in the attempt/error counts rather than disappearing.
    """
    trials: list[dict] = []
    non_retryable = ("not runnable", "build failed", "Java server deps missing",
                     "Fastify not installed")
    for _ in range(reps):
        trial = run_server_trial(
            language, task, latency_count, warmup, seconds, concurrency, cp3c,
            clients=clients, single_clients=single_clients)
        trials.append(trial)
        if trial["boot_ms"] is None and trial["note"].startswith(non_retryable):
            break

    # Preserve the last diagnostic when nothing ran; otherwise use the first
    # successful trial as the template for static fields such as the variant.
    good = [trial for trial in trials
            if trial["boot_ms"] is not None and trial["rps"] is not None]
    row = dict(good[0] if good else trials[-1])
    row["attempts"] = len(trials)
    row["successful_runs"] = len(good)
    row["runs_boot_ms"] = [trial["boot_ms"] for trial in good]
    row["runs_rps"] = [trial["rps"] for trial in good]
    row["requests"] = sum(trial["requests"] for trial in trials)
    row["errors"] = sum(trial["errors"] for trial in trials)
    row["latencies"] = [value for trial in good for value in trial["latencies"]]

    if good:
        row["boot_ms"] = median(row["runs_boot_ms"])
        row["rps"] = median(row["runs_rps"])
        row["p50"] = median(row["latencies"])
        row["p95"] = percentile(row["latencies"], 95)
        rss_values = [trial["peak_rss_mb"] for trial in good
                      if trial["peak_rss_mb"] is not None]
        row["peak_rss_mb"] = median(rss_values)
        build_values = [trial["build_ms"] for trial in trials
                        if trial["build_ms"] is not None]
        row["build_ms"] = median(build_values)
        detail = next((trial["note"] for trial in reversed(good)
                       if "warmup " in trial["note"]), "")
        prefix = detail.split(", warmup ", 1)[0] if ", warmup " in detail else ""
        run_detail = "warmup " + detail.split(", warmup ", 1)[1] if ", warmup " in detail else detail
        row["note"] = (f"{prefix}, " if prefix else "") + run_detail
        if len(good) != len(trials):
            failure = next((trial["note"] for trial in trials
                            if trial["boot_ms"] is None), "trial failed")
            row["note"] += f"; {len(trials) - len(good)} failed attempt(s): {failure}"
    return row


# --------------------------------------------------------------------------
# compute trials
# --------------------------------------------------------------------------
def compute_build_spec(language: str, task: str) -> tuple[list[str], Path, Path] | None:
    """(build argv, output binary, source file), or None when there is no build step."""
    base = _find_task_dir(task) if _REGISTRY_OK else (CASES / task)
    if language == "cpp":
        output = BUILD / f"{task}-cpp{EXE}"
        return (["g++", "-std=c++20", "-O3", "-o", str(output), str(base / "cpp" / "main.cpp")],
                output, base / "cpp" / "main.cpp")
    if language == "rust":
        output = BUILD / f"{task}-rust{EXE}"
        return (
        ["rustc", "-C", "opt-level=3", "-o", str(output), str(base / "rust" / "main.rs")],
        output,
        base / "rust" / "main.rs",)

    if language == "go":
        output = BUILD / f"{task}-go{EXE}"
        return (["go", "build", "-o", str(output), str(base / "go" / "main.go")],
                output, base / "go" / "main.go")
    if language == "java":
        output = BUILD / f"{task}-java" / "Main.class"
        source = base / "java" / "Main.java"
        outdir = output.parent
        outdir.mkdir(parents=True, exist_ok=True)
        return (["javac", "-d", str(outdir), str(source)], output, source)
    if language == "goose":
        output = BUILD / f"{task}-goose{EXE}"
        source = base / "goose" / "main.goose"
        return (["goose", "build", "-o", str(output), str(source)], output, source)
    if language == "dotnet":
        output = BUILD / f"{task}-dotnet" / "app.dll"
        return None
    return None


def compute_run_argv(language: str, task: str, extra: tuple[str, ...] = ()) -> list[str]:
    base = _find_task_dir(task) if _REGISTRY_OK else (CASES / task)
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
    if language == "java":
        return ["java", "-cp", str(BUILD / f"{task}-java"), "Main", *extra]
    if language == "goose":
        return [str(BUILD / f"{task}-goose{EXE}"), *extra]
    raise ValueError(language)


def prepare_cp3_churn_variant(entry: Path, iterations: int) -> Path:
    """Build cp3's string-churn entry at a non-default scale.

    The cp3 entry keeps the workload size as a compile-time constant. This
    writes a copy of the entry under .cache/build with ONLY that constant
    substituted (the same workload, one pinned number changed) and returns the
    generated entry directory. The committed source is untouched and is used
    directly at the default scale."""
    source_path = entry / "src" / "index.cp3"
    text = source_path.read_text(encoding="utf-8")
    pattern = re.compile(r"const ITERATIONS = \d+;")
    if not pattern.search(text):
        raise SystemExit(f"[runner] cannot find the ITERATIONS constant in {source_path}")
    replaced = pattern.sub(f"const ITERATIONS = {iterations};", text, count=1)
    target = BUILD / "string-churn-cp3-variant"
    (target / "src").mkdir(parents=True, exist_ok=True)
    (target / "src" / "index.cp3").write_text(replaced, encoding="utf-8")
    package = entry / "package.json"
    if package.exists():
        shutil.copyfile(package, target / "package.json")
    return target


def run_compute_trial(language: str, task: str, reps: int, cp3c: Path,
                      extra: tuple[str, ...] = (), cp3_entry: Path | None = None) -> dict:
    base = _find_task_dir(task) if _REGISTRY_OK else (CASES / task)
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
        argv = [detail]  # cp3 compute entries bake their workload scale into the source
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
        child_env = None
        if task == "env-injection":
            child_env = os.environ.copy()
            child_env[ENV_INJECTION_VARIABLE] = ENV_INJECTION_VALUE
        result, wall_ms, peak = run_measured(argv, timeout=900, env=child_env)
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
def median_ms_of(rows: list[dict], language: str) -> float | None:
    row = next((r for r in rows if r["language"] == language), None)
    if row is None or not row["runs_ms"]:
        return None
    return median(row["runs_ms"])


def gate_row(task: str, cp3_value: float | None,
             references: dict[str, float | None], unit: str) -> None:
    """Fill the tiered performance gate for one runnable task row (D67).

    Values are milliseconds (unit "ms") or requests per second (unit "rps").
    Ratios are time ratios, lower is better for cp3. BIG WARN when cp3 is over
    1.20x cpp/rust or slower than any of go/js/python at all; WARN when cp3 is
    over 1.10x cpp/rust but under the big tier. Compute rows whose reference
    median is under NOISE_FLOOR_MS are noted "noise-scale, ratio not gated"
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


def register_gate(task: str, cp3_value: float | None, cpp_value: float | None,
                  rust_value: float | None, go_value: float | None = None,
                  js_value: float | None = None,
                  python_value: float | None = None,
                  java_value: float | None = None) -> None:
    """Fill the tiered performance gate for one compute row (D67)."""
    gate_row(task, cp3_value, {"cpp": cpp_value, "rust": rust_value,
                               "go": go_value, "js": js_value,
                               "python": python_value, "java": java_value}, unit="ms")


def cp3_comparison(value: float | None, cp3_value: float | None,
                   higher_is_better: bool = False) -> str:
    """Describe a row's speed relative to cp3 in unambiguous words.

    For elapsed time, row / cp3 > 1 means the row is slower. For throughput,
    cp3 / row > 1 means the row is slower. Values below parity are inverted so
    the displayed multiplier is always at least 1x and labelled "faster".
    """
    if value is None or cp3_value is None or value <= 0 or cp3_value <= 0:
        return "-"
    ratio = cp3_value / value if higher_is_better else value / cp3_value
    if math.isclose(ratio, 1.0, rel_tol=0.0, abs_tol=0.0005):
        return "1.00x"
    if ratio > 1.0:
        return f"{ratio:.2f}x slower"
    return f"{1.0 / ratio:.2f}x faster"


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
    show_runs = False    # print every raw repetition when --show-runs is used


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
    "js": "yellow", "js-fastify": "yellow", "python": "green",
    "java": "red", "java-spring": "red", "dotnet": "blue",
    "cp3": "magenta", "goose": "cyan",
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
    for label, header, full in truncated:
        print(f"{lead}  {paint(g('arrow'), 'dim')} {paint(label, 'dim')} "
              f"{paint(header + ':', 'dim')} {paint(full, 'dim')}")


def banner(title: str, subtitle: str = "") -> None:
    """One-line section heading; long explanations belong in --help, not here."""
    print()
    prefix = f"  {g('title')} {title}"
    if subtitle:
        room = max(20, UI.wrap - len(prefix) - 3)
        print(f"  {paint(g('title'), 'cyan')} {paint(title, 'bold', 'white')}"
              f"  {paint(shorten(subtitle, room), 'dim')}")
    else:
        print(f"  {paint(g('title'), 'cyan')} {paint(title, 'bold', 'white')}")


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
    if UI.show_runs:
        for text, languages in details.items():
            scope = "" if len(languages) == len(rows) else ", ".join(languages) + ": "
            note_line(g("info"), ("dim",), scope + text.replace(", ", g("sep")))


def announce_run(task: str, language: str, index: int, total: int, reps: int) -> None:
    """One compact progress entry per language, not one per repetition."""
    if not UI.live:
        print(f"  {g('run')} {task}: {language} x{reps} ...", flush=True)
        return
    current = CURRENT_LANGUAGES.index(language)
    parts = []
    for position, name in enumerate(CURRENT_LANGUAGES):
        if position < current:
            parts.append(paint(g("ok"), "green") + " " + paint(name, "dim"))
        elif position == current:
            parts.append(paint(g("cur"), LANG_STYLE[name]) + " "
                         + paint(name, LANG_STYLE[name], "bold"))
        else:
            parts.append(paint(g("pend"), "dim") + " " + paint(name, "dim"))
    live_status(f"  {paint(g('run'), 'cyan')} {paint(f'{index}/{total}', 'dim')}  "
                f"{paint(task, 'bold')} {paint(f'x{reps}', 'dim')}  " + "  ".join(parts))


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
def print_header(cpu_text: str, probes: list, reps: int) -> None:
    """Compact run manifest: environment and toolchains without a page of prose."""
    stamp = datetime.now().isoformat(timespec="seconds")
    print(f"\n  {paint('BENCHMARK SUITE', 'bold', 'white')}  "
          f"{paint(stamp + g('sep') + f'{reps} runs/test', 'dim')}")
    print(f"  {paint('machine', 'dim')}  {shorten(cpu_text, 45)}"
          f"{g('sep')}{os.cpu_count()} logical CPUs")
    print(f"  {paint('os', 'dim')}      {shorten(platform.platform(), 70)}"
          f"{g('sep')}Python {platform.python_version()}")

    entries = []
    for name, argv in probes:
        version = probe_version(argv)
        good = version not in ("missing", "error")
        mark = paint(g("ok"), "green") if good else paint(g("bad"), "red", "bold")
        text = shorten(version, 28)
        text = paint(text, "dim") if good else paint(text, "red")
        entries.append(f"{mark}{paint(name, LANG_STYLE.get(name, 'white'))} {text}")
    per_line = max(1, min(3, (UI.width - 4) // 42))
    for start in range(0, len(entries), per_line):
        print("  " + paint(" | ", "dim").join(entries[start:start + per_line]))
    print(f"  {paint('method', 'dim')}  median headline; best/p95/CV retained; "
          f"HTTP uses keep-alive")


def print_time_wait_warning(waiting: int) -> None:
    print()
    note_line(f"{g('warn')} warn", ("yellow", "bold"),
              f"{waiting} sockets are in TIME_WAIT; a previous fresh-connection "
              "tool may still be congesting ephemeral ports -- failures below may "
              "be that, not the servers. Wait a couple of minutes and rerun for "
              "clean numbers.", ("yellow",), indent=2)


def print_server_table(task: str, rows: list[dict]) -> None:
    live_clear()
    banner(task, "median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %")
    cp3_row = next((r for r in rows if r["language"] == "cp3"), None)
    cp3_rps = cp3_row["rps"] if cp3_row and cp3_row["rps"] else None
    references = {name: next((r["rps"] for r in rows
                              if r["language"] == name and r["rps"]), None)
                  for name in GATE_REFERENCES}
    gate_row(task, cp3_rps, references, unit="rps")

    ran = {r["language"]: r["boot_ms"] is not None for r in rows}
    best_boot = best_langs({r["language"]: r["boot_ms"] for r in rows})
    best_p50 = best_langs({r["language"]: r["p50"] for r in rows})
    best_p95 = best_langs({r["language"]: r["p95"] for r in rows})
    best_rps = best_langs({r["language"]: r["rps"] for r in rows if ran[r["language"]]},
                          higher=True)
    best_rss = best_langs({r["language"]: r["peak_rss_mb"] for r in rows})
    best_bin = best_langs({r["language"]: r["binary_kb"] for r in rows})

    columns = [Col("lang", "lang", "l")]
    if UI.show_runs:
        columns += [Col("variant", "variant", "l", flex=True, min_width=14),
                    Col("build", "build", optional=True)]
    columns += [
        Col("ok", "ok/run"), Col("boot", "boot ms"), Col("p50", "p50 ms"),
        Col("p95", "p95 ms"), Col("rps", "req/s"), Col("cv", "RPS CV%"),
        Col("errors", "errors"), Col("vs_cp3", "vs cp3"),
        Col("rss", "RSS MiB", optional=True), Col("bin", "bin KiB", optional=True),
    ]
    # Higher throughput is faster. Failed/incomplete rows always sort last.
    display_rows = sorted(rows, key=lambda r: (not bool(r["rps"]), -(r["rps"] or 0.0)))
    table_rows: list = []
    for row in display_rows:
        language = row["language"]
        cached, _ = split_note(row["note"])
        is_cp3 = language == "cp3"
        attempts = row.get("attempts", 1 if ran[language] else 0)
        successful = row.get("successful_runs", 1 if ran[language] else 0)
        cells = {
            "lang": lang_cell(language),
            "variant": cell(row["variant"] or "-", "dim"),
            "build": build_cell(row, cached),
            "ok": cell(f"{successful}/{attempts}", "green" if successful == attempts else "yellow"),
            "errors": value_cell(str(row["errors"])),
            "rss": value_cell(rss_cell(row), language in best_rss),
            "bin": value_cell(binary_cell(row), language in best_bin),
        }
        if row["errors"]:
            cells["errors"] = cell(str(row["errors"]), "yellow", "bold")
        if ran[language]:
            cells["boot"] = value_cell(f"{row['boot_ms']:.1f}", language in best_boot)
            cells["p50"] = value_cell(f"{row['p50']:.2f}", language in best_p50)
            cells["p95"] = value_cell(f"{row['p95']:.2f}", language in best_p95)
            cells["rps"] = value_cell(f"{row['rps']:,.0f}", language in best_rps)
            cells["cv"] = cell(cv_cell(row.get("runs_rps", [])), "dim")
        else:
            for key in ("boot", "p50", "p95", "cv"):
                cells[key] = cell("-", "dim")
            cells["rps"] = cell("no run", "red", "bold")
        comparison = cp3_comparison(row["rps"] if ran[language] else None,
                                    cp3_rps, higher_is_better=True)
        cells["vs_cp3"] = cell("baseline" if is_cp3 and comparison != "-" else comparison)
        table_rows.append({"label": language, "cells": cells,
                           "styles": ("bold",) if is_cp3 else ()})
    render_table(columns, table_rows)
    print_row_notes(rows)
    if UI.show_runs:
        for row in display_rows:
            if ran[row["language"]]:
                boots = "/".join(f"{value:.1f}" for value in row.get("runs_boot_ms", []))
                throughputs = "/".join(f"{value:.0f}" for value in row.get("runs_rps", []))
                note_line(g("info"), ("dim",),
                          f"{row['language']} boot: {boots} ms{g('sep')}req/s: {throughputs}")


def _print_compute_like(task: str, rows: list[dict], header_text: str,
                        expected: str | None, with_median: bool) -> None:
    live_clear()
    banner(task, header_text + " | median/best/p95 ms; CV = variation %")
    reference_values = {name: median_ms_of(rows, name) for name in GATE_REFERENCES}
    cp3_median = median_ms_of(rows, "cp3")
    register_gate(task, cp3_median, reference_values["cpp"],
                  reference_values["rust"], reference_values["go"], reference_values["js"],
                  reference_values["python"], reference_values["java"])

    medians = {r["language"]: (median(r["runs_ms"]) if r["runs_ms"] else None)
               for r in rows}
    bests = {r["language"]: (min(r["runs_ms"]) if r["runs_ms"] else None) for r in rows}
    p95s = {r["language"]: (percentile(r["runs_ms"], 95) if r["runs_ms"] else None)
            for r in rows}
    best_median = best_langs(medians)
    best_best = best_langs(bests)
    best_p95 = best_langs(p95s)
    best_rss = best_langs({r["language"]: r["peak_rss_mb"] for r in rows})
    best_bin = best_langs({r["language"]: r["binary_kb"] for r in rows})

    def ms(value: float) -> str:
        return f"{value:.2f}" if value < 100 else f"{value:.1f}"

    columns = [Col("lang", "lang", "l")]
    if UI.show_runs:
        columns.append(Col("build", "build", optional=True))
    columns += [
        Col("median", "median ms"), Col("best", "best ms"), Col("p95", "p95 ms"),
        Col("cv", "CV%"), Col("vs_cp3", "vs cp3"),
        Col("rss", "RSS MiB", optional=True), Col("bin", "bin KiB", optional=True),
    ]

    # Lower elapsed time is faster. Failed/incomplete rows always sort last.
    display_rows = sorted(rows, key=lambda r: (not bool(r["runs_ms"]),
                                               medians[r["language"]] or math.inf))
    table_rows: list = []
    for row in display_rows:
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
            cells["median"] = value_cell(ms(medians[language]), language in best_median)
            cells["best"] = value_cell(ms(bests[language]), language in best_best)
            cells["p95"] = value_cell(ms(p95s[language]), language in best_p95)
            cells["cv"] = cell(cv_cell(row["runs_ms"]), "dim")
        else:
            for key in ("median", "best", "p95", "cv"):
                cells[key] = cell("-", "dim")
        comparison = cp3_comparison(medians[language], cp3_median)
        cells["vs_cp3"] = cell("baseline" if is_cp3 and comparison != "-" else comparison)
        table_rows.append({"label": language, "cells": cells,
                           "styles": ("bold",) if is_cp3 else ()})
    render_table(columns, table_rows)
    print_row_notes(rows)

    completed = [row for row in rows if row["runs_ms"]]
    outputs = {row["output"] for row in completed}
    mismatches = [row for row in completed
                  if (expected is not None and row["output"] != expected)
                  or (expected is None and len(outputs) > 1
                      and row["output"] != completed[0]["output"])]
    if mismatches:
        for row in mismatches:
            note_line(g("bad"), ("red", "bold"),
                      f"{row['language']} output {row['output']!r}; expected "
                      f"{expected if expected is not None else completed[0]['output']!r}",
                      ("red",))
    elif completed:
        target = f"oracle {expected!r}" if expected is not None else "across languages"
        note_line(g("ok"), ("green",),
                  f"output verified: {len(completed)}/{len(completed)} match {target}",
                  ("dim",))
    if UI.show_runs:
        for row in completed:
            raw = "/".join(ms(value) for value in row["runs_ms"])
            note_line(g("info"), ("dim",), f"{row['language']} runs: {raw} ms")


def print_compute_table(task: str, rows: list[dict], header_text: str,
                        expected: str | None = None) -> None:
    _print_compute_like(task, rows, header_text, expected, with_median=False)


def print_hello_table(task: str, rows: list[dict], header_text: str,
                      expected: str | None = None) -> None:
    _print_compute_like(task, rows, header_text, expected, with_median=True)


def print_gate() -> None:
    live_clear()
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
    """Writes everywhere the runner prints: console plus any capture sinks.

    The console keeps colour; every secondary sink is plain text."""

    def __init__(self, primary, *secondaries):
        self.primary = primary
        self.secondaries = secondaries

    def write(self, text: str):
        self.primary.write(text)
        clean = ANSI_RE.sub("", text)
        for sink in self.secondaries:
            sink.write(clean)

    def flush(self):
        self.primary.flush()
        for sink in self.secondaries:
            sink.flush()


RESULTS_MD = BENCHMARKS / "results.md"
LATEST_RESULTS_START = "<!-- latest-full-run:start -->"
LATEST_RESULTS_END = "<!-- latest-full-run:end -->"


def write_latest_results(captured: str, argv: list[str]) -> None:
    """Replace the generated latest-run block in benchmarks/results.md.

    Only a complete, default-scale full-suite run calls this. The historical
    blocks below the generated block are never touched.
    """
    display = list(argv)
    if display:
        try:
            if Path(display[0]).resolve() == Path(__file__).resolve():
                display[0] = "benchmarks/runner/run_suite.py"
        except OSError:
            pass
    command = "python " + " ".join(display)
    stamp = datetime.now().isoformat(timespec="seconds")
    block = (
        f"{LATEST_RESULTS_START}\n"
        f"_Generated by `{command}` on {stamp}._\n\n"
        "```text\n"
        f"{captured.strip()}\n"
        "```\n"
        f"{LATEST_RESULTS_END}"
    )
    if RESULTS_MD.exists():
        text = RESULTS_MD.read_text(encoding="utf-8")
    else:
        text = (
            "# Benchmark results\n\n"
            "`README.md` documents the suite; this file stores the recorded\n"
            "numbers. The latest full-suite run is rewritten automatically.\n\n"
            "## Latest full run\n\n"
            f"{LATEST_RESULTS_START}\n_No full-suite run recorded yet._\n{LATEST_RESULTS_END}\n"
        )
    pattern = re.compile(
        re.escape(LATEST_RESULTS_START) + r".*?" + re.escape(LATEST_RESULTS_END),
        re.DOTALL,
    )
    if pattern.search(text):
        text = pattern.sub(lambda _match: block, text, count=1)
    else:
        text = text.rstrip() + "\n\n## Latest full run\n\n" + block + "\n"
    RESULTS_MD.write_text(text, encoding="utf-8", newline="\n")


LATEST_TASKS_START = "<!-- latest-by-test:start -->"
LATEST_TASKS_END = "<!-- latest-by-test:end -->"


def record_task(task: str, printer) -> None:
    """Run a table printer and keep its plain text for the per-test section of results.md."""
    buffer = io.StringIO()
    previous = sys.stdout
    sys.stdout = Tee(previous, buffer)
    try:
        printer()
    finally:
        sys.stdout = previous
    gate = [line for line in GATE_BLOCKERS + GATE_WARNINGS + GATE_NOTES if f" {task}: " in line]
    text = buffer.getvalue().strip("\n")
    if gate:
        text += "\n\n" + "\n".join("gate: " + line for line in gate)
    TASK_SECTIONS[task] = (datetime.now().isoformat(timespec="seconds"), text)


def write_task_sections(sections: dict, argv: list[str]) -> None:
    """Rewrite only the per-test sections of results.md that this run measured."""
    display = list(argv)
    if display:
        display[0] = "benchmarks/runner/run_benchmark.py"
    command = "python " + " ".join(display)
    text = RESULTS_MD.read_text(encoding="utf-8") if RESULTS_MD.exists() else "# Benchmark results\n"
    block_re = re.compile(r"<!-- test:([a-z0-9-]+):start -->.*?<!-- test:\1:end -->", re.DOTALL)
    region_re = re.compile(re.escape(LATEST_TASKS_START) + r"(.*?)" + re.escape(LATEST_TASKS_END), re.DOTALL)
    match = region_re.search(text)
    blocks = {m.group(1): m.group(0) for m in block_re.finditer(match.group(1))} if match else {}
    for task, (stamp, body) in sections.items():
        blocks[task] = (f"<!-- test:{task}:start -->\n### {task}\n_Updated {stamp} by `{command}`._\n\n```text\n{body}\n```\n<!-- test:{task}:end -->")
    ordered = [blocks[name] for name in ALL_TASKS if name in blocks]
    ordered += [block for name, block in blocks.items() if name not in ALL_TASKS]
    region = LATEST_TASKS_START + "\n" + "\n\n".join(ordered) + "\n" + LATEST_TASKS_END
    if match:
        text = text[:match.start()] + region + text[match.end():]
    else:
        text = text.rstrip() + "\n\n## Latest results by test\n\n" + region + "\n"
    RESULTS_MD.write_text(text, encoding="utf-8", newline="\n")


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


def positive_int(text: str) -> int:
    try:
        value = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a positive integer") from exc
    if value < 1:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(
        description="benchmarks comparison suite (single entry point: benchmarks/runner/run_benchmark.py)")
    parser.add_argument("--only", default="", help="comma list of: " + ",".join(ALL_TASKS))
    parser.add_argument("--except", dest="skip", default="", help="comma list of tests to skip (same names as --only)")
    parser.add_argument("--seconds", type=float, default=10.0, help="throughput phase seconds per server")
    parser.add_argument("--concurrency", type=int, default=8, help="throughput phase connections")
    parser.add_argument("--clients", type=int, default=4,
                        help="client OS processes for the multicore server row (default 4)")
    parser.add_argument("--single-clients", type=int, default=2,
                        help="client OS processes for the single-core server row (default 2)")
    parser.add_argument("--latency", type=int, default=300, help="latency phase request count")
    parser.add_argument("--warmup", type=int, default=100, help="warmup requests before measuring")
    parser.add_argument("--reps", type=positive_int, default=15,
                        help="independent measured runs per test/language (default: 3)")
    parser.add_argument("--hello-reps", type=positive_int, default=None,
                        help="optional hello-world override; otherwise uses --reps")
    parser.add_argument("--show-runs", action="store_true",
                        help="also show raw repetitions, variants and build details")
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
    BUILD.mkdir(parents=True, exist_ok=True)

    if args.only.strip():
        wanted = [part.strip() for part in args.only.split(",") if part.strip()]
        unknown = [name for name in wanted if name not in ALL_TASKS]
        if unknown:
            print(f"[runner] unknown task(s): {', '.join(unknown)}")
            print(f"[runner] known tasks: {', '.join(ALL_TASKS)}")
            return 2
    else:
        wanted = list(ALL_TASKS)

    skipped = [part.strip() for part in args.skip.split(",") if part.strip()]
    bad_skip = [name for name in skipped if name not in ALL_TASKS]
    if bad_skip:
        print(f"[runner] unknown --except task(s): {', '.join(bad_skip)}")
        print(f"[runner] known tasks: {', '.join(ALL_TASKS)}")
        return 2
    wanted = [name for name in wanted if name not in skipped]
    if not wanted:
        print("[runner] nothing left to run after --except")
        return 2
    churn_iterations, churn_label = resolve_churn_scale(args.churn_scale)
    full_run = not args.only.strip() and not args.skip.strip()
    capture = io.StringIO() if full_run and churn_label == "default" else None

    original_stdout = sys.stdout
    ui_setup(original_stdout)
    UI.show_runs = args.show_runs
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
            f"repetitions: {args.reps} (hello-world: {args.hello_reps or args.reps})",
            *cp3c_provenance(),
        ]
        (results_dir / "meta.txt").write_text("\n".join(meta) + "\n", encoding="utf-8")
        results_log = open(results_dir / "run.txt", "w", encoding="utf-8", newline="")
        sys.stdout = Tee(original_stdout, results_log, *([capture] if capture else []))
    elif capture is not None:
        sys.stdout = Tee(original_stdout, capture)

    completed = False
    try:
        probes = [
            ("g++", ["g++", "--version"]), ("rustc", ["rustc", "--version"]),
            ("cargo", ["cargo", "--version"]),
            ("go", ["go", "version"]), ("node", ["node", "--version"]),
            ("python", [sys.executable, "--version"]),
            ("java", ["java", "-version"]), ("javac", ["javac", "-version"]),
            ("cmake", ["cmake", "--version"]), ("cp3c", [str(cp3c), "version"]),
            ("dotnet", ["dotnet", "--version"]), ("goose", ["goose", "--version"]),
        ]
        print_header(platform.processor() or platform.machine(), probes, args.reps)
        waiting = time_wait_count()
        if waiting is not None and waiting > 4000:
            print_time_wait_warning(waiting)

        def present(language: str, server: bool = False) -> bool:
            if language == "cp3":
                return cp3c.exists()
            if language in ("goose", "dotnet"):
                return have(language)
            if language == "js":
                return have("node")
            if language == "js-fastify":
                return have("node") and (_find_task_dir("health-server") / "js" / "fastify").exists()
            if language == "python":
                return True
            if language == "java":
                return have("java") and have("javac")
            if language == "rust" and server:
                return have("cargo")
            return have({"cpp": "g++", "rust": "rustc", "go": "go"}[language])

        task_total = sum(1 for name in ALL_TASKS if name in wanted)
        task_index = 0

        global CURRENT_LANGUAGES
        for task in SERVER_TASKS:
            if task not in wanted:
                continue
            task_index += 1
            CURRENT_LANGUAGES = SERVER_LANGUAGES
            rows = []
            for language in SERVER_LANGUAGES:
                if not present(language, server=True):
                    announce_skip(language)
                    continue
                announce_run(task, language, task_index, task_total, 1)
                rows.append(run_server_repeated(
                    language, task, 1 , args.latency, args.warmup,
                    args.seconds, args.concurrency, cp3c,
                    clients=args.clients, single_clients=args.single_clients))
            record_task(task, lambda: print_server_table(task, rows))

        for task in COMPUTE_TASKS:
            if task not in wanted:
                continue
            task_index += 1
            CURRENT_LANGUAGES = COMPUTE_LANGUAGES
            extra: tuple[str, ...] = ()
            cp3_entry = None
            expected = EXPECTED.get(task)
            reps = args.reps
            if task == "hello-world":
                reps = args.hello_reps or args.reps
                header = (f"expected = {EXPECTED[task]} | spawn -> exit wall time; "
                          f"{reps} runs")
            elif task == "string-churn":
                extra = (str(churn_iterations),)
                if churn_iterations != CHURN_DEFAULT_ITERATIONS:
                    cp3_entry = prepare_cp3_churn_variant(CASES / task / "cp3", churn_iterations)
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
            elif task == "binary-trees":
                header = (f"expected = {EXPECTED[task]} | full binary trees to depth 13, "
                          f"recursive build and check")
            elif task == "map-churn":
                header = (f"expected = {EXPECTED[task]} | 100,000-key hash map, "
                          f"delete/reinsert/update churn")
            elif task == "utf8-text":
                header = (f"expected = {EXPECTED[task]} | 417,792-byte UTF-8 text, "
                          f"32 manual decode rounds")
            elif task == "regex-scan":
                header = (f"expected = {EXPECTED[task]} | 240,000-byte text, "
                          f"global [0-9]+ scan, 8 rounds")
            elif task == "json-parse-large":
                header = (f"expected = {EXPECTED[task]} | 204,472-byte JSON record, "
                          f"20 parse rounds")
            elif task == "deep-recursion":
                header = (f"expected = {EXPECTED[task]} | 5,000-deep recursion, "
                          f"200 rounds")
            elif task == "exception-cost":
                header = (f"expected = {EXPECTED[task]} | 1,000,000 throw/catch vs "
                          f"sentinel-return iterations")
            elif task == "file-big":
                header = (f"expected = {EXPECTED[task]} | 8 MiB file, "
                          f"8 write/read rounds")
            else:
                header = f"expected = {EXPECTED[task]}"
            rows = []
            omitted = TASK_OMIT_LANGUAGES.get(task, {})
            for language in COMPUTE_LANGUAGES:
                if language in omitted:
                    announce_omit(task, language, omitted[language])
                    continue
                if not present(language):
                    announce_skip(language)
                    continue
                announce_run(task, language, task_index, task_total, reps)
                rows.append(run_compute_trial(language, task, reps, cp3c,
                                              extra=extra, cp3_entry=cp3_entry))
            if task == "hello-world":
                record_task(task, lambda: print_hello_table(task, rows, header, expected=expected))
            else:
                record_task(task, lambda: print_compute_table(task, rows, header, expected=expected))

        print_gate()
        completed = True
    finally:
        live_clear()
        if results_log is not None or capture is not None:
            sys.stdout = original_stdout
        if results_log is not None:
            results_log.close()
        if TASK_SECTIONS:
            try:
                write_task_sections(TASK_SECTIONS, sys.argv)
            except Exception as exc:
                print(f"[runner] results.md sections not updated: {exc}", file=original_stdout)
        if capture is not None and completed:
            try:
                write_latest_results(capture.getvalue(), sys.argv)
            except Exception as exc:  # a results-file write must not fail the run
                print(f"[runner] results.md not updated: {exc}", file=original_stdout)

    if GATE_BLOCKERS:
        return 1
    if args.strict and GATE_WARNINGS:
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        for proc in LOAD_PROCS:
            try:
                proc.kill()
            except OSError:
                pass
        live_clear()
        print("\n  interrupted (Ctrl+C): servers and load clients stopped; finished tests were saved to results.md")
        sys.exit(130)