#!/usr/bin/env python3
"""Stage 0 cycle measurement for the automatic cycle handling task.

Scans .cp3 sources (tests/cases + examples), extracts class/interface names
and their fields, builds the class->class field graph (including arrays,
Map/Set values and unions, conservatively), and reports cycle statistics:
self-references, mutual cycles, longer cycles, and how many already use weak.

Auto-weak coverage (upper bound): each cycle SCC is classified as
  - already-weak   : a manual `weak` edge already breaks the cycle.
  - exempt         : single-node self-loop (recursive type, no manual weak
                     needed today).
  - auto-weak      : SCC > 1 with no weak edge and no `unknown`-typed
                     field anywhere in the SCC -- a *candidate* for
                     compiler-injected non-owning edges. This is an
                     upper bound: the whole-program ownership proof
                     (docs/design/cycle-proof.md, obligations P2/P3:
                     narrowing + independent strong path at every
                     store/escape site) is not modelled here, so real
                     compiler coverage will be <= this figure.
  - manual-weak    : SCC > 1 with no weak edge that involves an
                     `unknown` field -- fail-closed, manual `weak`
                     required (proof obligation P8).

Usage: python tools/cycle_stats.py [--root DIR] [--out FILE]
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

DECL_RE = re.compile(r"\b(?:class|interface)\s+(\w+)")
# field: optional weak/readonly/access modifiers, name, optional ?/! , :, type
FIELD_RE = re.compile(
    r"(?:^|[;{}])\s*(weak\s+)?(?:static\s+)?(?:readonly\s+)?"
    r"(?:public\s+|private\s+|protected\s+)?"
    r"(\w+)\s*[?:!]*\s*:\s*([^;{}()]+?)\s*[;=]"
)


def strip_comments(src: str) -> str:
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    # drop strings/templates.
    src = re.sub(r'"(?:\\.|[^"\\])*"', '""', src)
    src = re.sub(r"`(?:\\.|[^`\\])*`", "``", src)
    return src


def extract_classes(src: str):
    """Yield (classname, [(fieldname, typetext, isweak), ...])."""
    for m in DECL_RE.finditer(src):
        name = m.group(1)
        start = src.find("{", m.end())
        if start < 0:
            continue
        depth = 0
        i = start
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = src[start + 1 : i]
        fields = []
        for fm in FIELD_RE.finditer(body):
            weak = bool(fm.group(1))
            fields.append((fm.group(2), fm.group(3).strip(), weak))
        yield name, fields


def type_refs(typetext: str, known: set) -> set:
    ids = set(re.findall(r"[A-Z][A-Za-z0-9_]*", typetext))
    return ids & known


def type_has_unknown(typetext: str) -> bool:
    # Mirrors the compiler's fail-closed rule: an `unknown` anywhere in
    # the field type (incl. inside unions/containers) is opaque.
    return re.search(r"\bunknown\b", typetext) is not None


def find_cycles(graph):
    sys.setrecursionlimit(100000)
    index = {}
    low = {}
    onstack = set()
    stack = []
    counter = [0]
    sccs = []

    def strong(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        onstack.add(v)
        for w in graph.get(v, ()):
            if w not in index:
                strong(w)
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
            if len(comp) > 1 or (comp and comp[0] in graph.get(comp[0], ())):
                sccs.append(comp)

    for v in list(graph):
        if v not in index:
            strong(v)
    return sccs


def classify_scc(comp, graph, weak_edges, unknown_classes):
    """Return (decision, reason) for one SCC under the auto-weak model."""
    if len(comp) == 1:
        return "exempt", "single self-referencing field; no manual weak needed"
    comp_edges = [
        (a, b) for a in comp for b in graph.get(a, ()) if b in comp
    ]
    if any(e in weak_edges for e in comp_edges):
        return "already-weak", "manual weak edge already breaks the cycle"
    if any(c in unknown_classes for c in comp):
        return (
            "manual-weak",
            "unknown-typed field in SCC (fail-closed, P8); mark one edge weak",
        )
    return (
        "auto-weak",
        "candidate for injected non-owning edge (upper bound; P2/P3 unproven)",
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    root = Path(args.root)
    files = [
        f
        for f in list((root / "tests" / "cases").rglob("*.cp3"))
        + list((root / "examples").rglob("*.cp3"))
        if f.is_file()
    ]
    known = set()
    raw = {}
    loc = {}
    for f in files:
        try:
            src = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        known.update(DECL_RE.findall(src))
    for f in files:
        try:
            src = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        for name, fields in extract_classes(src):
            raw.setdefault(name, []).extend(fields)
            loc.setdefault(name, str(f.relative_to(root)))
    graph = defaultdict(set)
    weak_edges = set()
    unknown_classes = set()
    total_fields = 0
    class_edges = 0
    for cls, fields in raw.items():
        for fname, ftype, weak in fields:
            total_fields += 1
            if type_has_unknown(ftype):
                unknown_classes.add(cls)
            for target in type_refs(ftype, known):
                graph[cls].add(target)
                class_edges += 1
                if weak:
                    weak_edges.add((cls, target))
    sccs = find_cycles(graph)
    self_loops = sum(1 for c in raw if c in graph.get(c, ()))
    mutual = sum(1 for comp in sccs if len(comp) == 2)
    longer = sum(1 for comp in sccs if len(comp) > 2)
    cyclic_classes = set()
    for comp in sccs:
        cyclic_classes.update(comp)
    acyclic = len(known) - len(cyclic_classes)
    unresolved = 0
    decisions = {}
    for comp in sccs:
        comp_edges = [
            (a, b) for a in comp for b in graph.get(a, ()) if b in comp
        ]
        if not any(e in weak_edges for e in comp_edges):
            unresolved += 1
        decisions[tuple(sorted(comp))] = classify_scc(
            comp, graph, weak_edges, unknown_classes
        )
    # Auto-weak coverage: among classes that (today) need a manual weak
    # edge to break an SCC > 1, how many would an auto-weak injection
    # candidate cover, vs how many still need manual weak?
    auto_classes = set()
    manual_classes = set()
    for comp in sccs:
        decision, _reason = decisions[tuple(sorted(comp))]
        if decision == "auto-weak":
            auto_classes.update(comp)
        elif decision == "manual-weak":
            manual_classes.update(comp)
    needing = auto_classes | manual_classes
    auto_pct = (
        100.0 * len(auto_classes) / len(needing) if needing else 0.0
    )
    lines = []
    lines.append("# Stage 0 cycle statistics (automatic cycle handling)")
    lines.append("")
    lines.append(f"- .cp3 files scanned: {len(files)}")
    lines.append(f"- classes/interfaces named: {len(known)}")
    lines.append(f"- class fields scanned: {total_fields}")
    lines.append(f"- class->class field edges: {class_edges}")
    lines.append(f"- self-referential classes: {self_loops}")
    lines.append(f"- 2-class cycles (mutual): {mutual}")
    lines.append(f"- longer cycles (>2 classes): {longer}")
    lines.append(f"- cycle-capable classes: {len(cyclic_classes)}")
    lines.append(f"- acyclic classes: {acyclic}")
    lines.append(f"- weak field edges already in corpus: {len(weak_edges)}")
    lines.append(
        f"- cycle groups with no weak edge (upper bound of step-4 manual weak): "
        f"{unresolved} of {len(sccs)} groups"
    )
    lines.append("")
    lines.append("## Auto-weak coverage (upper bound; see docs/design/cycle-proof.md)")
    lines.append("")
    lines.append(
        f"- classes needing a cycle break (SCC > 1, no weak edge): {len(needing)}"
    )
    lines.append(f"- auto-weak candidates (no unknown in SCC): {len(auto_classes)}")
    lines.append(f"- manual weak still required (unknown in SCC): {len(manual_classes)}")
    lines.append(
        f"- auto-weak coverage %: {auto_pct:.0f}% "
        f"({len(auto_classes)}/{len(needing)} classes; "
        f"manual weak share {100.0 - auto_pct:.0f}%)"
    )
    lines.append(
        "- note: upper bound only -- narrowing (P2) and independent "
        "strong-path (P3) proofs are not modelled; compiler coverage "
        "will be <= this figure."
    )
    lines.append("")
    lines.append("Cycle groups:")
    for comp in sorted(sccs, key=lambda c: sorted(c)):
        comp_edges = [
            (a, b) for a in comp for b in graph.get(a, ()) if b in comp
        ]
        has_weak = any(e in weak_edges for e in comp_edges)
        decision, reason = decisions[tuple(sorted(comp))]
        members = ", ".join(
            f"{c} ({loc.get(c, '?')})" for c in sorted(comp)
        )
        lines.append(
            f"- {members} [weak: {'yes' if has_weak else 'no'}; "
            f"auto-weak: {decision} -- {reason}]"
        )
    report = "\n".join(lines) + "\n"
    print(report, end="")
    if args.out:
        Path(args.out).write_text(report, encoding="utf-8")


if __name__ == "__main__":
    main()
