#!/usr/bin/env python3
"""The one command that runs the whole benchmark suite.

    python benchmarks/run_benchmark.py

This is a thin forwarding wrapper: it passes every argument straight through
to the engine, benchmarks/bench/run_suite.py, which builds, runs and measures
every benchmark task on this machine and prints the full comparison. The
engine is the only implementation; this wrapper exists so the suite has one
documented entry point.

Useful flags (documented in benchmarks/README.md):
    --only <task[,task...]>   run a subset of tasks
    --reps N                  compute repetitions per entry (best is reported)
    --cp3c PATH               cp3 compiler binary (default build/bin/cp3c.exe)
    --strict                  exit 1 when the performance gate prints any WARN
    --hello-reps N            hello-world runs (default 15; best and median)
    --churn-scale VALUE       string-churn scale: default, stress or an integer
    --results DIR             tee the raw output into DIR/run.txt + DIR/meta.txt

Examples:
    python benchmarks/run_benchmark.py
    python benchmarks/run_benchmark.py --only hello-world,string-churn
    python benchmarks/run_benchmark.py --churn-scale stress
    python benchmarks/run_benchmark.py --reps 3 --cp3c build/bin/cp3c.exe --strict
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENGINE = HERE / "bench" / "run_suite.py"


def main() -> int:
    return subprocess.call([sys.executable, str(ENGINE), *sys.argv[1:]])


if __name__ == "__main__":
    sys.exit(main())
