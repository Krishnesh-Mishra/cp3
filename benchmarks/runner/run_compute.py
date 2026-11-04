#!/usr/bin/env python3
"""Compute-rows entry point (split from run_suite.py).

Usage: python run_compute.py --only compute-single,compute-multi
Thin wrapper: forwards to run_suite.main with argv filtered to COMPUTE_TASKS.
The main runner never needs edits when adding a task (see tasks.py).
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

if __name__ == "__main__":
    import run_suite
    sys.argv[0] = str(HERE / "run_suite.py")
    raise SystemExit(run_suite.main())
