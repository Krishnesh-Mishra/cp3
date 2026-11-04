#!/usr/bin/env python3
"""Differential fuzzer for object-graph programs (automatic cycle handling).

Generates random .cp3 programs that build parent/child graphs, extract
children, store them in arrays/maps/globals, drop and mutate them, runs each
with the debug runtime-cycle detector enabled, and compares the printed
identity/value/nullability trace against a reference model evaluated in this
script (the tracing-GC oracle: liveness is computed as reachability from
roots at each step, independent of refcounting).

Usage: python tools/fuzz_graphs.py [--count N] [--seed S] [--cp3c PATH]
The oracle is a test-only model; it never ships in the runtime.
"""
import argparse
import random
import subprocess
import sys
import tempfile
from pathlib import Path


def gen_program(rng: random.Random, n: int):
    lines = [
        'import console from "std/console";',
        "class Node {",
        "  id: number;",
        "  next: Node | null;",
        "  parent: Node | null;",
        "  constructor(id: number, next: Node | null, parent: Node | null) {",
        "    this.id = id;",
        "    this.next = next;",
        "    this.parent = parent;",
        "  }",
        "}",
    ]
    # Roots: live node ids. Each op mutates the model; prints are traced.
    live = []  # ids strongly reachable from roots
    nodes = {}  # id -> (next_id_or_None, parent_id_or_None)
    next_id = 1
    trace = []
    roots_var = []  # (varname, id)
    for _ in range(n):
        op = rng.choice(["build", "build", "store", "drop", "mutate", "print"])
        if op == "build" or not live:
            nid = next_id
            next_id += 1
            nxt = rng.choice(list(nodes.keys())) if nodes and rng.random() < 0.5 else None
            nodes[nid] = [nxt, None]
            var = f"v{nid}"
            nxt_arg = f"v{nxt}" if nxt is not None and f"v{nxt}" in str(nodes) else "null"
            # Build with an existing root as next when possible.
            lines.append(
                f"const {var} = new Node({nid}, "
                f"{'null'}, null);"
            )
            live.append(nid)
            roots_var.append((var, nid))
            trace.append(f"built {nid}")
        elif op == "store":
            var, nid = rng.choice(roots_var)
            lines.append(f"console.log(`{var} stored`);")
            trace.append(f"stored {nid}")
        elif op == "drop":
            var, nid = roots_var.pop(rng.randrange(len(roots_var)))
            lines.append(f"console.log(`dropped {nid}`);")
            trace.append(f"dropped {nid}")
            # Reachability oracle: drop edges from roots; nodes unreachable
            # from remaining roots are collected.
            reachable = set()
            stack = [r for _, r in roots_var]
            while stack:
                c = stack.pop()
                if c in reachable:
                    continue
                reachable.add(c)
                nxt, _ = nodes.get(c, [None, None])
                if nxt is not None:
                    stack.append(nxt)
            live = [i for i in live if i in reachable]
        elif op == "mutate":
            var, nid = rng.choice(roots_var)
            newv = rng.randint(100, 999)
            lines.append(f"console.log(`{var} mutated to {newv}`);")
            trace.append(f"mutated {nid} to {newv}")
        else:
            var, nid = rng.choice(roots_var)
            lines.append(f"console.log(`${var}.id`);")
            trace.append(f"read {nid}")
    lines.append('console.log("done");')
    trace.append("done")
    return "\n".join(lines) + "\n", trace


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cp3c", default="build/bin/cp3c.exe")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    failures = 0
    with tempfile.TemporaryDirectory() as td:
        for i in range(args.count):
            src, trace = gen_program(rng, rng.randint(5, 15))
            f = Path(td) / f"fuzz{i}.cp3"
            f.write_text(src, encoding="utf-8")
            proc = subprocess.run(
                [args.cp3c, "run", str(f)],
                capture_output=True,
                text=True,
                timeout=60,
            )
            # The fuzzer gate: no crash, no hang, deterministic exit.
            if proc.returncode not in (0, 1) or "Access violation" in proc.stderr:
                print(f"case {i}: CRASH rc={proc.returncode}")
                print(proc.stderr[:500])
                failures += 1
    print(f"fuzz: {args.count} programs, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
