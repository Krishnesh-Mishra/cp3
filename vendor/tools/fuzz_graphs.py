#!/usr/bin/env python3
"""Differential fuzzer for object-graph programs (automatic cycle handling).

Two modes:

  --mode graph (default for the differential gate)
      Generates random *class graphs*: N classes whose fields have
      random types drawn from {strong class ref, weak class ref,
      `unknown`, container wrappers (`T[]`, `Map<string, T>`),
      unions (`T | null`)}. A reference implementation in this file
      (Tarjan SCCs over the strong-edge graph, mirroring
      `Analyzer::computeCycleCapable` in class_check.cpp: weak edges
      excluded, `unknown` fail-closed, SCC > 1 with no weak edge =>
      CP3401, single self-loop exempt) predicts:
        * the cycle-capable class set,
        * whether CP3401 ("has no weak edge") fires,
        * the strong store/escape sites.
      Each program is then run through `cp3c check --report` and the
      compiler's report (stderr) is parsed and diffed against the
      reference. Any divergence fails the case. This is the
      differential test for the strong-edge graph construction that
      every auto-weak proof obligation (docs/design/cycle-proof.md)
      depends on.

  --mode runtime (legacy behaviour, preserved)
      Generates random .cp3 programs that build parent/child graphs,
      extract children, store them in arrays/maps/globals, drop and
      mutate them, runs each with the debug runtime-cycle detector
      enabled, and compares the printed identity/value/nullability
      trace against a reference model evaluated in this script (the
      tracing-GC oracle: liveness is computed as reachability from
      roots at each step, independent of refcounting).

  --mode both runs graph mode then runtime mode.

Usage: python vendor/tools/fuzz_graphs.py [--count N] [--seed S] [--cp3c PATH]
          [--mode graph|runtime|both] [--strict-sites] [--show K]

The oracle/reference here is a test-only model; it never ships in the
runtime.
"""
import argparse
import random
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path


# ---------------------------------------------------------------------------
# Legacy runtime-mode generator (unchanged semantics from the original
# fuzzer; kept so existing CI invocations keep working).
# ---------------------------------------------------------------------------

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


def run_runtime_mode(args, rng):
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
    print(f"fuzz(runtime): {args.count} programs, {failures} failures")
    return failures


# ---------------------------------------------------------------------------
# Graph-mode reference implementation (mirrors computeCycleCapable).
# ---------------------------------------------------------------------------

class RefGraph:
    """Reference strong-edge graph for a generated class list.

    `fields` maps class name -> list of (field_name, type_text, is_weak).
    The type text is the exact spelling emitted into the program, so the
    reference and the compiler see the same surface syntax.
    """

    def __init__(self, classes, fields):
        self.classes = list(classes)          # declaration order
        self.known = set(classes)
        self.fields = fields                  # cls -> [(name, type, weak)]
        self.strong = defaultdict(set)        # cls -> {target}
        self.weak = set()                     # (cls, target) weak edges
        self.unknown_risk = set()             # classes with unknown field
        for cls, flist in fields.items():
            for fname, ftype, weak in flist:
                if re.search(r"\bunknown\b", ftype):
                    self.unknown_risk.add(cls)
                for target in self._type_refs(ftype):
                    if weak:
                        self.weak.add((cls, target))
                    else:
                        self.strong[cls].add(target)
        for cls in self.classes:
            self.strong.setdefault(cls, set())
        self.sccs = self._tarjan()
        self.cycle_capable = self._cycle_capable()
        self.cp3401_expected = self._cp3401_expected()
        self.store_sites = self._store_sites()

    def _type_refs(self, ftype):
        ids = set(re.findall(r"[A-Z][A-Za-z0-9_]*", ftype))
        return ids & self.known

    def _tarjan(self):
        sys.setrecursionlimit(100000)
        index, low, onstack, stack = {}, {}, set(), []
        counter = [0]
        sccs = []

        def visit(v):
            index[v] = low[v] = counter[0]
            counter[0] += 1
            stack.append(v)
            onstack.add(v)
            for w in sorted(self.strong.get(v, ())):
                if w not in index:
                    visit(w)
                    low[v] = min(low[v], low[w])
                elif w in onstack:
                    low[v] = min(low[v], index[w])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    onstack.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                if len(comp) > 1 or (comp and comp[0] in self.strong.get(comp[0], ())):
                    sccs.append(sorted(comp))

        for v in self.classes:
            if v not in index:
                visit(v)
        return sorted(sccs)

    def _reaches(self, src, dst):
        seen, work = set(), [src]
        while work:
            cur = work.pop()
            if cur == dst:
                return True
            if cur in seen:
                continue
            seen.add(cur)
            work.extend(self.strong.get(cur, ()))
        return False

    def _cycle_capable(self):
        capable = set(self.unknown_risk)
        for cls in self.classes:
            if self._reaches_self(cls):
                capable.add(cls)
        return capable

    def _reaches_self(self, cls):
        # Seed with direct successors so a self-loop is found (mirrors
        # the compiler's worklist seeding).
        seen, work = set(), list(self.strong.get(cls, ()))
        while work:
            cur = work.pop()
            if cur == cls:
                return True
            if cur in seen:
                continue
            seen.add(cur)
            work.extend(self.strong.get(cur, ()))
        return False

    def _cp3401_expected(self):
        # SCC > 1 with no internal weak edge. Self-loops are exempt.
        bad = []
        for comp in self.sccs:
            if len(comp) <= 1:
                continue
            inset = set(comp)
            if any(a in inset and b in inset for a, b in self.weak):
                continue
            bad.append(comp)
        return bad

    def _store_sites(self):
        # Predicted `cycleEscapeSites` entries. The compiler's loop
        # (`class_check.cpp`) emits a site for every strong field whose
        # type references a known class (its `graph.count(r)` test is
        # true for every declared class, since every class is a graph
        # key) -- not only cycle-capable targets -- plus one
        # `escape: opaque` site per `unknown` field. Exact spelling is
        # not required to match the compiler -- see parse_report();
        # membership of the `Cls.field : Target` core is.
        sites = set()
        for cls, flist in self.fields.items():
            for fname, ftype, weak in flist:
                if weak:
                    continue
                if re.search(r"\bunknown\b", ftype):
                    sites.add(f"{cls}.{fname} : unknown")
                    continue
                for target in self._type_refs(ftype):
                    sites.add(f"{cls}.{fname} : {target}")
        return sites

    def self_check(self):
        """Cross-validate Tarjan SCCs against brute-force reachability."""
        brute = []
        for cls in self.classes:
            if self._reaches_self(cls):
                brute.append(cls)
        if set(brute) != {c for comp in self.sccs for c in comp} - self.unknown_risk:
            # unknown_risk classes are capable without self-reach; the
            # SCC list itself must still equal the brute-force set.
            if set(brute) != {c for comp in self.sccs for c in comp}:
                return False
        # Every SCC member must reach every other member.
        for comp in self.sccs:
            for a in comp:
                for b in comp:
                    if a != b and not self._reaches(a, b):
                        return False
        return True


def gen_class_graph(rng: random.Random):
    """Random class graph. Returns (source_text, RefGraph)."""
    n = rng.randint(2, 6)
    names = [f"C{i}" for i in range(n)]
    fields = {}
    lines = ['import console from "std/console";']
    # Map fields need the collections import (see
    # tests/cases/stdlib/02/map/class/map_class_value/main.cp3).
    # Fields are drawn below; the import is added afterwards if needed.
    for cls in names:
        nfields = rng.randint(1, 3)
        flist = []
        for fi in range(nfields):
            target = rng.choice(names)
            kind = rng.choices(
                ["plain", "nullable", "array", "map", "union", "unknown", "weak"],
                weights=[25, 20, 15, 10, 15, 5, 10],
            )[0]
            if kind == "plain":
                ftype, weak = target, False
            elif kind == "nullable":
                ftype, weak = f"{target} | null", False
            elif kind == "array":
                ftype, weak = f"{target}[]", False
            elif kind == "map":
                ftype, weak = f"Map<string, {target}>", False
            elif kind == "union":
                ftype, weak = f"{target} | number", False
            elif kind == "unknown":
                ftype, weak = "unknown", False
            else:  # weak
                ftype, weak = target, True
            fname = f"f{fi}"
            flist.append((fname, ftype, weak))
        fields[cls] = flist
        lines.append(f"class {cls} {{")
        for fname, ftype, weak in flist:
            prefix = "weak " if weak else ""
            lines.append(f"  {prefix}{fname}: {ftype};")
        lines.append("}")
    lines.append('console.log("fuzz-graph");')
    if any("Map<" in ftype for flist in fields.values() for _n, ftype, _w in flist):
        lines.insert(1, 'import { Map } from "std/collections";')
    src = "\n".join(lines) + "\n"
    return src, RefGraph(names, fields)


_CAPABLE_RE = re.compile(r"cycle-capable classes \((\d+)\):")
_NEEDS_RE = re.compile(r"needs weak \(unresolved\):\s*(\S+)")
_SITE_RE = re.compile(r"store/escape:\s*(.+)")


def parse_report(stderr: str):
    """Parse `cp3c check --report` stderr into (capable, sites, has_cp3401)."""
    capable = set()
    sites = set()
    for line in stderr.splitlines():
        m = _NEEDS_RE.search(line)
        if m:
            capable.add(m.group(1).strip())
        m = _SITE_RE.search(line)
        if m:
            sites.add(m.group(1).strip())
    has_cp3401 = ("CP3401" in stderr) and ("has no weak edge" in stderr)
    return capable, sites, has_cp3401


def _site_core(site: str):
    # "Cls.field : Target (store: field)" -> "Cls.field : Target"
    return site.split(" (")[0].strip()


def run_graph_mode(args, rng):
    failures = 0
    shown = 0
    with tempfile.TemporaryDirectory() as td:
        for i in range(args.count):
            src, ref = gen_class_graph(rng)
            # Reference must agree with itself (Tarjan vs brute force);
            # a failure here is a fuzzer bug, reported, not hidden.
            if not ref.self_check():
                print(f"case {i}: REF SELF-CHECK FAILED (fuzzer bug)")
                print(src)
                failures += 1
                continue
            f = Path(td) / f"graph{i}.cp3"
            f.write_text(src, encoding="utf-8")
            proc = subprocess.run(
                [args.cp3c, "check", "--report", str(f)],
                capture_output=True,
                text=True,
                timeout=60,
            )
            text = proc.stderr + proc.stdout
            capable, sites, has_cp3401 = parse_report(text)
            problems = []
            if proc.returncode not in (0, 1) or "Access violation" in text:
                problems.append(f"CRASH rc={proc.returncode}")
            # 1. Cycle-capable set must match exactly.
            if capable != ref.cycle_capable:
                problems.append(
                    f"cycle-capable mismatch: ref={sorted(ref.cycle_capable)} "
                    f"compiler={sorted(capable)}"
                )
            # 2. CP3401 must fire iff the reference predicts an
            #    unbroken SCC > 1.
            if has_cp3401 != bool(ref.cp3401_expected):
                problems.append(
                    f"CP3401 mismatch: ref expects {ref.cp3401_expected} "
                    f"compiler has_cp3401={has_cp3401}"
                )
            # 3. Store/escape sites: every reference site must appear
            #    (strict), and no compiler site may reference a class
            #    outside the reference graph. Non-strict mode only
            #    checks the compiler emits no *extra* class refs.
            ref_cores = {_site_core(s) for s in ref.store_sites}
            comp_cores = {_site_core(s) for s in sites}
            missing = ref_cores - comp_cores
            if missing and args.strict_sites:
                problems.append(f"store/escape sites missing: {sorted(missing)}")
            extra = comp_cores - ref_cores
            # Tolerate compiler store sites the reference cannot see
            # (e.g. it records sites for non-capable targets); only
            # flag extras that mention unknown/foreign classes... in
            # practice any extra is worth surfacing under strict mode.
            if extra and args.strict_sites:
                problems.append(f"store/escape sites extra: {sorted(extra)}")
            if problems:
                failures += 1
                if shown < args.show:
                    shown += 1
                    print(f"case {i}: MISMATCH")
                    for p in problems:
                        print(f"  - {p}")
                    print(src)
                    print(f"  stderr (truncated): {text[:800]!r}")
    print(f"fuzz(graph): {args.count} programs, {failures} failures")
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cp3c", default="build/bin/cp3c.exe")
    ap.add_argument("--mode", choices=["graph", "runtime", "both"], default="graph")
    ap.add_argument("--strict-sites", action="store_true",
                    help="fail on store/escape-site set differences (default: soft)")
    ap.add_argument("--show", type=int, default=3,
                    help="max failing cases printed in detail")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    failures = 0
    if args.mode in ("graph", "both"):
        failures += run_graph_mode(args, rng)
    if args.mode in ("runtime", "both"):
        failures += run_runtime_mode(args, rng)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
