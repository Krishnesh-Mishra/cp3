#!/usr/bin/env python3
"""Benchmark task registry (split from run_suite.py).

Adding a new benchmark is now: 1) create `cases/<category>/<task>/`
with per-language folders, 2) add one line to the matching category list
below. `run_suite.py` never needs to be edited, so concurrent agents
adding tasks cannot damage the main runner.

Categories mirror the folder layout under `cases/`:
    compute  -> cpu/math/string/loop kernels
    memory   -> allocation-heavy DSA (trees, graphs, lists, caches)
    network  -> HTTP server rows
    stdlib   -> one-row-per-stdlib-module probes
"""
from __future__ import annotations

from pathlib import Path

HERE = Path(__file__).resolve().parent
BENCHMARKS = HERE.parent
CASES = BENCHMARKS / "cases"

SERVER_TASKS = ["health-server", "health-server-multi"]

COMPUTE_TASKS = [
    # compute (existing)
    "compute-single", "compute-single-int", "compute-multi",
    "hello-world", "string-churn", "sha256-churn", "buffer-copy",
    "typed-array-sum", "math-kernel", "word-frequency", "object-churn",
    "array-growth", "closure-capture", "interface-dispatch",
    "generics-mono", "json-roundtrip", "async-fanout", "env-injection",
    "file-roundtrip", "path-join", "date-format", "os-probe",
    "binary-trees", "map-churn", "utf8-text", "regex-scan",
    "json-parse-large", "deep-recursion", "exception-cost", "file-big",
    # memory/DSA (new, added in small commits; runner skips when dir absent)
    "tree-walk", "graph-bfs", "strlist", "lru", "interp", "blur",
    "calc", "particles", "push", "records", "scene", "sexp",
    "respond", "sum-array",
]
ALL_TASKS = SERVER_TASKS + COMPUTE_TASKS

CATEGORIES: dict[str, str] = {
    # network
    "health-server": "network", "health-server-multi": "network",
    # compute (pure cpu / loops / dispatch)
    **{t: "compute" for t in [
        "compute-single", "compute-single-int", "compute-multi",
        "hello-world", "closure-capture", "interface-dispatch",
        "generics-mono", "async-fanout", "deep-recursion", "exception-cost",
    ]},
    # dsa-array (vectors, buffers, typed arrays)
    **{t: "dsa-array" for t in [
        "array-growth", "buffer-copy", "typed-array-sum", "sum-array", "file-big", "blur",
    ]},
    # dsa-object (objects, trees, scene)
    **{t: "dsa-object" for t in [
        "object-churn", "binary-trees", "tree-walk", "scene", "interp", "records",
    ]},
    # dsa-string (string/word scans)
    **{t: "dsa-string" for t in [
        "string-churn", "utf8-text", "regex-scan", "word-frequency", "strlist", "sexp",
    ]},
    # dsa-map (hash maps, caches, graphs)
    **{t: "dsa-map" for t in [
        "map-churn", "lru", "graph-bfs", "push",
    ]},
    # stdlib (one row per stdlib module)
    **{t: "stdlib" for t in [
        "sha256-churn", "math-kernel", "os-probe", "date-format", "env-injection",
        "file-roundtrip", "path-join", "json-roundtrip", "json-parse-large",
        "respond", "calc", "particles",
    ]},
    # memory (allocation-heavy fallback)
    **{t: "memory" for t in [
        "calc", "particles",
    ]},
}

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
    "calc": {
        "rust": "no exceptions in Rust; error codes are the returned-value path, not throw/catch",
        "go": "no exceptions in Go; error codes are the returned-value path",
    },
    "interp": {
        "rust": "no virtual dispatch in Rust; enum + match is the idiomatic analogue (kept for cross-language comparison but listed here to match exception-cost)",
        "go": "no inheritance in Go; interface + type-switch is the idiomatic analogue",
    },
}

# Expected outputs for fixed-value tasks (verified by oracle.py).
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
    "tree-walk": "16383\n66134180",
    "push": "1000000\n15625\n499758894813\n750050",
    "blur": "256\n7734030",
    "strlist": "100000\n92177148",
    # DSA/memory tasks: expected values filled in per-task commit (oracle.py).
}
CHURN_EXPECTED = {50000: "852167", 300000: "254546"}
CHURN_DEFAULT_ITERATIONS = 50000
CHURN_STRESS_ITERATIONS = 300000
CHURN_SCALE_PRESETS = {"default": CHURN_DEFAULT_ITERATIONS, "stress": CHURN_STRESS_ITERATIONS}
ENV_INJECTION_VARIABLE = "CP3_BENCH_ENV"
ENV_INJECTION_VALUE = "cp3-benchmark-value"


def find_task_dir(task: str) -> Path:
    """Locate <task> under cases/<category>/ or legacy cases/<task>/."""
    cached = find_task_dir.__dict__.get("_cache", {})
    if task in cached:
        return cached[task]
    category = CATEGORIES.get(task)
    candidates = []
    if category:
        candidates.append(CASES / category / task)
    candidates.append(CASES / task)
    for cand in candidates:
        if cand.is_dir():
            cached[task] = cand
            return cand
    found = next((p for p in CASES.rglob(task) if p.is_dir()), None)
    if found is not None:
        cached[task] = found
        return found
    cached[task] = candidates[0]
    return candidates[0]


def category_of(task: str) -> str:
    return CATEGORIES.get(task, "compute")


def task_exists(task: str) -> bool:
    return find_task_dir(task).is_dir()
