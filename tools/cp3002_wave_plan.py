#!/usr/bin/env python3
# =============================================================================
# build/scratch/rounds/cp3002_wave_plan.py - build the CP3002 implementation
# wave plan from the live marker inventory.
#
# Scans src/ for `notImplemented(` call sites, extracts the marker message of
# each, groups the sites by the feature area that owns them, and chunks the
# work items into agent-sized assignments (about ten items each).
#
# Output: build/scratch/rounds/CP3002-WAVE-PLAN.md
#
# Usage (from the repo root):
#   python build\scratch\rounds\cp3002_wave_plan.py
# =============================================================================
from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "src"
OUT = REPO / "docs" / "rough" / "CP3002-WAVE-PLAN.md"

# The feature area that owns each source file. A file appears once; the
# roadmap's group table (docs/rough/roadmap.md section 6) is the authority.
AREA_OF_FILE = {
    # 1 - spread arguments and spreads
    "src/codegen/src/emit/call_args.cpp": "1 Spread arguments and spreads",
    "src/codegen/src/emit/patterns.cpp": "1 Spread arguments and spreads",
    "src/codegen/src/emit/async.cpp": "1 Spread arguments and spreads",
    "src/codegen/src/emit/async_promise.cpp": "1 Spread arguments and spreads",
    "src/std/typed-arrays/codegen.cpp": "1 Spread arguments and spreads",
    "src/codegen/src/support/generic.cpp": "1 Spread arguments and spreads",
    # 2 - rest parameters
    "src/codegen/src/emit/class_decl.cpp": "2 Rest parameters",
    # 3 - destructuring
    "src/codegen/src/emit/destructure.cpp": "3 Destructuring",
    "src/codegen/src/emit/stmt.cpp": "3 Destructuring",
    "src/codegen/src/emit/module_var.cpp": "3 Destructuring",
    # 4 - compound assignment and update targets
    "src/codegen/src/emit/assignment.cpp": "4 Compound assignment and update targets",
    # 5 - object literal edge cases
    "src/codegen/src/emit/objects.cpp": "5 Object literal edge cases",
    "src/std/console/codegen.cpp": "5 Object literal edge cases",
    "src/codegen/src/emit/class_expr.cpp": "5 Object literal edge cases",
    # 6 - class declaration and value surfaces
    "src/codegen/src/support/class_collect.cpp": "6 Class declaration and value surfaces",
    "src/sema/src/checks/class_check.cpp": "6 Class declaration and value surfaces",
    # 7 - generic calls and specialization
    "src/sema/src/checks/generic_check.cpp": "7 Generic calls and specialization",
    # 8 - type aliases and declared types
    "src/sema/src/support/declarations.cpp": "8 Type aliases and declared types",
    "src/codegen/src/emit/decl.cpp": "8 Type aliases and declared types",
    # 9 - enum edge cases
    "src/codegen/src/emit/enum.cpp": "9 Enum edge cases",
    "src/sema/src/checks/enum_check.cpp": "9 Enum edge cases",
    # 10 - expression operators and formatting
    "src/codegen/src/emit/expr.cpp": "10 Expression operators and formatting",
    "src/codegen/src/emit/typeof.cpp": "10 Expression operators and formatting",
    "src/codegen/src/emit/equality.cpp": "10 Expression operators and formatting",
    # 11 - control flow
    "src/codegen/src/emit/control.cpp": "11 Control flow",
    # 12 - array methods and callbacks
    "src/std/arrays/codegen_methods.cpp": "12 Array methods and callbacks",
    # 13 - typed arrays and constructors
    "src/std/typed-arrays/codegen_methods.cpp": "13 Typed arrays and constructors",
    # 14 - Map and Set
    "src/std/collections/codegen.cpp": "14 Map and Set",
    # 15 - network
    "src/std/network/codegen.cpp": "15 Network",
    "src/std/network/codegen_methods.cpp": "15 Network",
    # 16 - FS, process, env, console
    "src/std/fs/codegen.cpp": "16 FS, process, env, console",
    "src/std/process/codegen.cpp": "16 FS, process, env, console",
    "src/std/env/codegen.cpp": "16 FS, process, env, console",
    # 17 - async and Promise
    # (async.cpp is claimed by group 1; its Promise markers land here)
    # 18 - worker and parallel modules
    "src/codegen/src/emit/worker.cpp": "18 Worker and parallel modules",
    "src/codegen/src/emit/worker_handles.cpp": "18 Worker and parallel modules",
    "src/codegen/src/emit/worker_scan.cpp": "18 Worker and parallel modules",
    "src/codegen/src/emit/parallel.cpp": "18 Worker and parallel modules",
    # CLI scaffolding
    "src/cli/src/commands/run.cpp": "19 CLI scaffolding",
    "src/cli/src/commands/check.cpp": "19 CLI scaffolding",
    "src/cli/src/commands/common.cpp": "19 CLI scaffolding",
    "src/cli/src/detail/commands.hpp": "19 CLI scaffolding",
    # codegen entry points
    "src/codegen/src/code_generator.cpp": "20 Codegen entry points",
    "src/codegen/src/emit/call.cpp": "20 Codegen entry points",
    "src/codegen/src/emit/std/common.cpp": "20 Codegen entry points",
    "src/std/json/codegen.cpp": "20 Codegen entry points",
    "src/sema/src/checks/expr_check.cpp": "20 Codegen entry points",
}

MESSAGE_RE = re.compile(r'notImplemented\s*\(')


def call_text(text: str, open_paren: int) -> str:
    """The text of the call whose opening paren is at `open_paren`.

    The marker call is `notImplemented(<node>, "<message>")` and the call
    often spans several lines, so the text is read up to the matching close
    paren - never a fixed window, which would swallow the string literals of
    the code that follows.
    """
    depth = 0
    index = open_paren
    while index < len(text):
        char = text[index]
        if char == '"':
            # Skip a string literal so a paren inside it does not count.
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == '"':
                    break
                index += 1
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1: index]
        index += 1
    return text[open_paren + 1:]


def scan_file(path: Path) -> list[tuple[int, str]]:
    """Every notImplemented( call site in one file: (line, message)."""
    text = path.read_text(encoding="utf-8", errors="replace")
    out: list[tuple[int, str]] = []
    for match in MESSAGE_RE.finditer(text):
        open_paren = match.end() - 1
        arguments = call_text(text, open_paren)
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', arguments)
        message = strings[-1] if strings else "<unparsed>"
        line = text.count("\n", 0, match.start()) + 1
        out.append((line, message))
    return out


def main() -> int:
    by_area: dict[str, dict[str, list[str]]] = defaultdict(lambda: defaultdict(list))
    by_file: dict[str, int] = defaultdict(int)
    unmapped: dict[str, int] = defaultdict(int)

    for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.hpp")):
        rel = path.relative_to(REPO).as_posix()
        sites = scan_file(path)
        if not sites:
            continue
        by_file[rel] = len(sites)
        area = AREA_OF_FILE.get(rel)
        if area is None:
            unmapped[rel] = len(sites)
            continue
        for line, message in sites:
            by_area[area][message].append(f"{rel}:{line}")

    total = sum(by_file.values())

    # One work item per distinct message per area; the call sites are the
    # evidence of where it lives.
    work_items: list[tuple[str, str, list[str]]] = []
    for area in sorted(by_area, key=lambda a: (a.split()[0].zfill(2), a)):
        for message in sorted(by_area[area]):
            work_items.append((area, message, by_area[area][message]))

    # Chunk into agent assignments of about ten work items, never splitting an
    # area across two agents unless the area is larger than a chunk.
    TARGET = 10
    assignments: list[list[tuple[str, str, list[str]]]] = []
    current: list[tuple[str, str, list[str]]] = []
    for item in work_items:
        current.append(item)
        if len(current) >= TARGET:
            assignments.append(current)
            current = []
    if current:
        assignments.append(current)

    lines: list[str] = []
    lines.append("# CP3002 implementation wave plan")
    lines.append("")
    lines.append("Generated by `build/scratch/rounds/cp3002_wave_plan.py`.")
    lines.append("")
    lines.append(f"- Marker call sites in `src/`: **{total}**")
    lines.append(f"- Distinct work items (one per marker message per area): **{len(work_items)}**")
    lines.append(f"- Agent assignments (about {TARGET} work items each): **{len(assignments)}**")
    lines.append("- Waves of 5 parallel agents: **"
                 f"{(len(assignments) + 4) // 5}**")
    lines.append("")
    lines.append("Each assignment is one agent's lane. The agent implements the")
    lines.append("items, builds, runs the affected corpus cases, and reports. The")
    lines.append("lead verifies and merges serially, then launches the next agent.")
    lines.append("")
    lines.append("## Call sites per file")
    lines.append("")
    lines.append("| File | Sites |")
    lines.append("|---|---:|")
    for rel, count in sorted(by_file.items(), key=lambda kv: -kv[1]):
        lines.append(f"| `{rel}` | {count} |")
    lines.append("")
    if unmapped:
        lines.append("### Files not yet mapped to an area")
        lines.append("")
        for rel, count in sorted(unmapped.items(), key=lambda kv: -kv[1]):
            lines.append(f"- `{rel}` - {count}")
        lines.append("")
    lines.append("## Work items by area")
    lines.append("")
    for area in sorted(by_area, key=lambda a: (a.split()[0].zfill(2), a)):
        items = [w for w in work_items if w[0] == area]
        lines.append(f"### {area} - {len(items)} item(s)")
        lines.append("")
        for _, message, sites in items:
            shown = ", ".join(f"`{s}`" for s in sites[:4])
            more = f" (+{len(sites) - 4} more)" if len(sites) > 4 else ""
            lines.append(f"- {message} - {shown}{more}")
        lines.append("")
    lines.append("## Assignments")
    lines.append("")
    for index, assignment in enumerate(assignments, start=1):
        areas = sorted({item[0].split(" ", 1)[0] for item in assignment})
        lines.append(f"### Assignment {index} - {len(assignment)} item(s) - area(s) "
                     f"{', '.join(areas)}")
        lines.append("")
        for area, message, sites in assignment:
            shown = ", ".join(f"`{s}`" for s in sites[:3])
            more = f" (+{len(sites) - 3} more)" if len(sites) > 3 else ""
            lines.append(f"- [{area.split(' ', 1)[0]}] {message} - {shown}{more}")
        lines.append("")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT}")
    print(f"call sites: {total}, work items: {len(work_items)}, "
          f"assignments: {len(assignments)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
