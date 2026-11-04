#!/usr/bin/env python3
"""Server-rows entry point (split from run_suite.py).

Usage: python run_server.py --only health-server,health-server-multi
Thin wrapper: forwards to run_suite.main. See tasks.py / langs.py for registries.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

if __name__ == "__main__":
    import run_suite
    sys.argv[0] = str(HERE / "run_suite.py")
    raise SystemExit(run_suite.main())
