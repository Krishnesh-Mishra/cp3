#!/usr/bin/env python3
"""Stage 0 cycle measurement for the automatic cycle handling task.

Scans .cp3 sources (tests/cases + examples), extracts class/interface names
and their fields, builds the class->class field graph (including arrays,
Map/Set values and unions, conservatively), and reports cycle statistics:
self-references, mutual cycles, longer cycles, and how many already use weak.

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
    total_fields = 0
    class_edges = 0
    for cls, fields in raw.items():
        for fname, ftype, weak in fields:
            total_fields += 1
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
    for comp in sccs:
        comp_edges = [
            (a, b) for a in comp for b in graph.get(a, ()) if b in comp
        ]
        if not any(e in weak_edges for e in comp_edges):
            unresolved += 1
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
    lines.append("Cycle groups:")
    for comp in sorted(sccs, key=lambda c: sorted(c)):
        comp_edges = [
            (a, b) for a in comp for b in graph.get(a, ()) if b in comp
        ]
        has_weak = any(e in weak_edges for e in comp_edges)
        members = ", ".join(
            f"{c} ({loc.get(c, '?')})" for c in sorted(comp)
        )
        lines.append(f"- {members} [weak: {'yes' if has_weak else 'no'}]")
    report = "\n".join(lines) + "\n"
    print(report, end="")
    if args.out:
        Path(args.out).write_text(report, encoding="utf-8")


if __name__ == "__main__":
    main()
