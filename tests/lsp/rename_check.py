#!/usr/bin/env python3
# =============================================================================
# tests/lsp/rename_check.py - acceptance pins for Family I (rename propagates
# to only a subset of references), 10 checks.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party
# packages) and prints one machine-checkable PASS / FAIL line per check.
# Exit code 0 when every check passes, 1 otherwise. The LSP is driven
# exactly the way tests/lsp/intel_check.py drives it (imports its Session
# driver), and the fixtures live in multi-file workspaces, one fresh
# TEMP/opencode/<name>-<rand> directory per fixture group.
#
# The pins state the desired behaviour. Checks that fail at HEAD are the
# open Family I defects: rename produces edits but misses constructor
# calls, type annotations, implements clauses and the other sites listed
# below. They are deliberately pinned as desired behaviour; nothing here
# weakens a pin to green. Expected reds at HEAD (from reading the pipeline,
# not from running it): I-02, I-03, I-04, I-08 (constructor-call bindings
# of user classes are never published into SemaResult::resolvedBindings,
# and type positions - NamedType, implements clauses - never produce an
# Identifier node, so the occurrence collector cannot see them). The
# remaining checks are regression guards; I-05/I-10 hinge on the
# member-rename fallback (lsp_intel_members.cpp) accepting the family at
# HEAD, which reading suggests but cannot prove.
#
# Every edit's range is converted to absolute character offsets against the
# fixture text (ASCII fixtures: byte, UTF-16 and character offsets coincide)
# and coverage is asserted by offset, never by re-stringing the file.
# =============================================================================

import argparse
import json
import os
import sys
import tempfile
from collections import Counter

sys.dont_write_bytecode = True  # import intel_check without creating .pyc files here

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")

RENAME_TIMEOUT = 30.0
BARRIER_TIMEOUT = 30.0

# -----------------------------------------------------------------------------
# Fixtures (ASCII only: byte, UTF-16 and character offsets then coincide).
# The nine-line fixture block of the report is split across four workspace
# groups, one fresh TEMP/opencode/<name>-<rand> multi-file workspace per
# group. Each workspace carries an unrelated second module; it is never
# imported and never participates in the rename graph, but it keeps every
# workspace honestly multi-file.
# -----------------------------------------------------------------------------

WIDGET_FIXTURE = (
    'class Widget {}\n'
    'const w = new Widget();\n'
    'const f = (): Widget => new Widget();\n'
    'const g = (x: Widget): Widget => x;\n'
    'function show(x: Widget): void { console.log(x); }\n'
    'const extra = new Widget();\n'
)

HOLDER_FIXTURE = (
    'class Holder { constructor() { console.log(this.label); } label: string = "w"; }\n'
    'const holder = new Holder();\n'
    'console.log(holder.label);\n'
)

SIZED_FIXTURE = (
    'interface Sized { size: number; }\n'
    'class I implements Sized { size: number = 0; }\n'
)

COMBINE_FIXTURE = (
    'export function combine(n: number): number { return n + 1; }\n'
    'function show(x: (n: number) => number): void { console.log(x(1)); }\n'
    'const f = combine;\n'
    'show(combine);\n'
    'const t = combine(3);\n'
)

SIDE_FIXTURE = 'export const SIDE = 1;\n'

GROUPS = {
    "widget": [("widget.cp3", WIDGET_FIXTURE), ("side_widget.cp3", SIDE_FIXTURE)],
    "member": [("holder.cp3", HOLDER_FIXTURE), ("side_member.cp3", SIDE_FIXTURE)],
    "sized": [("sized.cp3", SIZED_FIXTURE), ("side_sized.cp3", SIDE_FIXTURE)],
    "combine": [("combine.cp3", COMBINE_FIXTURE), ("side_combine.cp3", SIDE_FIXTURE)],
}

FILE_BY_GROUP = {
    "widget": "widget.cp3",
    "member": "holder.cp3",
    "sized": "sized.cp3",
    "combine": "combine.cp3",
}


# -----------------------------------------------------------------------------
# Small helpers
# -----------------------------------------------------------------------------

def abs_index(text, needle, occurrence=0):
    """Absolute character offset of the occurrence-th `needle` (raises)."""
    index = -1
    for _ in range(occurrence + 1):
        index = text.find(needle, index + 1)
        if index < 0:
            raise RuntimeError("fixture needle not found: %r" % needle)
    return index


def line_starts(text):
    starts = [0]
    for at, ch in enumerate(text):
        if ch == "\n":
            starts.append(at + 1)
    return starts


def edit_span(text, edit, starts_cache):
    """Absolute (start, end) character offsets of one edit, or None."""
    if not isinstance(edit, dict):
        return None
    rng = edit.get("range")
    if not isinstance(rng, dict):
        return None
    start, end = rng.get("start"), rng.get("end")

    def offset_of(pos):
        if not isinstance(pos, dict):
            return None
        line, character = pos.get("line"), pos.get("character")
        if not isinstance(line, int) or not isinstance(character, int):
            return None
        if line < 0 or character < 0:
            return None
        starts = starts_cache.setdefault(text, line_starts(text))
        if line >= len(starts):
            return None
        return starts[line] + character

    first = offset_of(start)
    last = offset_of(end)
    if first is None or last is None or last < first:
        return None
    if last > len(text):
        return None
    return (first, last)


def all_edits(changes):
    """Every (uri, edit) pair of a WorkspaceEdit.changes map."""
    out = []
    if not isinstance(changes, dict):
        return out
    for uri, edits in changes.items():
        if not isinstance(edits, list):
            continue
        for edit in edits:
            if isinstance(edit, dict):
                out.append((uri, edit))
    return out


def edit_count(changes):
    total = 0
    if not isinstance(changes, dict):
        return 0
    for edits in changes.values():
        if isinstance(edits, list):
            total += len(edits)
    return total


def covered(changes, text_by_uri, starts_cache, offset):
    """True when some edit's range covers absolute offset `offset`."""
    for uri, edit in all_edits(changes):
        text = text_by_uri.get(uri)
        if text is None:
            continue
        span = edit_span(text, edit, starts_cache)
        if span is not None and span[0] <= offset < span[1]:
            return True
    return False


# -----------------------------------------------------------------------------
# The checks (I-01 .. I-10)
# -----------------------------------------------------------------------------

def make_checks(session, groups, text_by_uri, starts_cache):
    """Returns a list of (id, description, callable -> (ok, detail))."""

    def rename(group, needle, new_name, offset=0, occurrence=0):
        key = FILE_BY_GROUP[group]
        text = groups[group]["texts"][key]
        uri = groups[group]["uris"][key]
        response = session.request("textDocument/rename", {
            "textDocument": {"uri": uri},
            "position": intel_check.position(text, needle, offset, occurrence),
            "newName": new_name,
        }, timeout=RENAME_TIMEOUT)
        result = response.get("result")
        changes = result.get("changes") if isinstance(result, dict) else None
        return text, uri, changes

    widget_text = groups["widget"]["texts"][FILE_BY_GROUP["widget"]]
    member_text = groups["member"]["texts"][FILE_BY_GROUP["member"]]
    sized_text = groups["sized"]["texts"][FILE_BY_GROUP["sized"]]
    combine_text = groups["combine"]["texts"][FILE_BY_GROUP["combine"]]

    widget_decl = abs_index(widget_text, "class Widget") + 6
    new_widget_sites = []
    at = 0
    while True:
        at = widget_text.find("new Widget()", at)
        if at < 0:
            break
        new_widget_sites.append(at + 4)  # offset of the Widget token itself
        at += 1
    annotation_sites = [
        abs_index(widget_text, "(): Widget =>") + 4,   # (): Widget  (return)
        abs_index(widget_text, "(x: Widget)") + 4,      # (x: Widget) (param)
        abs_index(widget_text, "): Widget => x;") + 4,  # ): Widget    (return)
        abs_index(widget_text, "show(x: Widget)") + 8,  # show(x: Widget)
    ]

    member_decl = abs_index(member_text, "label: string")
    member_this_read = abs_index(member_text, "this.label") + 5
    member_holder_read = abs_index(member_text, "holder.label") + 7

    sized_iface = abs_index(sized_text, "interface Sized") + 10
    sized_impl = abs_index(sized_text, "implements Sized") + 11

    combine_decl = abs_index(combine_text, "export function combine") + 16
    combine_alias = abs_index(combine_text, "f = combine") + 4
    combine_arg = abs_index(combine_text, "show(combine)") + 5
    combine_call = abs_index(combine_text, "combine(3)")

    def i01():
        _, _, changes = rename("widget", "class Widget", "WidgetV2", offset=6)
        ok = covered(changes, text_by_uri, starts_cache, widget_decl)
        return ok, "decl site %d not covered by %s" % (
            widget_decl, json.dumps(changes))

    def i02():
        _, _, changes = rename("widget", "class Widget", "WidgetV2", offset=6)
        raw = json.dumps(changes).count("Widget") if changes is not None else 0
        new_sites_hit = sum(
            1 for site in new_widget_sites
            if covered(changes, text_by_uri, starts_cache, site))
        ok = raw >= 3 and len(new_widget_sites) >= 3 and \
            new_sites_hit == len(new_widget_sites)
        return ok, "raw old-token count=%d, new-Widget sites covered %d/%d" % (
            raw, new_sites_hit, len(new_widget_sites))

    def i03():
        _, _, changes = rename("widget", "class Widget", "WidgetV2", offset=6)
        hits = sum(1 for site in annotation_sites
                   if covered(changes, text_by_uri, starts_cache, site))
        return hits >= 1, "type-annotation sites covered %d/%d" % (
            hits, len(annotation_sites))

    def i04():
        # "it includes the extends/implements site" - the rename at the
        # declaration (Widget has no implementors, Sized stands in) must
        # cover the implements-clause occurrence, the same outgoing-rename
        # reading as I-02/I-03/I-05.
        _, _, changes = rename("sized", "interface Sized", "SizedV2", offset=10)
        impl = covered(changes, text_by_uri, starts_cache, sized_impl)
        return impl, "implements-clause site not covered by the Sized rename"

    def i05():
        _, _, changes = rename("member", "label: string", "label2")
        ok = covered(changes, text_by_uri, starts_cache, member_this_read)
        return ok, "this.label read inside the constructor body not covered"

    def i06():
        _, _, changes = rename("combine", "f = combine", "combine2", offset=4)
        sites = {
            "export declaration": combine_decl,
            "f = combine alias": combine_alias,
            "combine in show(combine)": combine_arg,
            "combine(3) call": combine_call,
        }
        missing = [name for name, site in sites.items()
                   if not covered(changes, text_by_uri, starts_cache, site)]
        return not missing, "same-file sites not renamed: %s" % (
            ", ".join(missing) or "none")

    def i07():
        _, _, changes = rename("widget", "class Widget", "WidgetV2", offset=6)
        malformed = 0
        per_uri = {}
        for uri, edit in all_edits(changes):
            text = text_by_uri.get(uri)
            span = edit_span(text, edit, starts_cache) if text else None
            if span is None:
                malformed += 1
                continue
            per_uri.setdefault(uri, []).append((span, edit.get("newText")))
        duplicate_positions = 0
        duplicate_full = 0
        overlapping = 0
        for uri, entries in per_uri.items():
            counter = Counter(span for span, _ in entries)
            duplicate_positions += sum(count - 1 for count in counter.values()
                                       if count > 1)
            full = Counter((span[0], span[1], text) for span, text in entries)
            duplicate_full += sum(count - 1 for count in full.values()
                                  if count > 1)
            spans = sorted(span for span, _ in entries)
            for previous, current in zip(spans, spans[1:]):
                if current[0] < previous[1]:
                    overlapping += 1
        ok = malformed == 0 and duplicate_positions == 0 \
            and duplicate_full == 0 and overlapping == 0 \
            and bool(all_edits(changes))
        return ok, ("edits=%d malformed=%d duplicate positions=%d duplicate (range,text)=%d "
                    "overlapping=%d") % (len(all_edits(changes)), malformed,
                                          duplicate_positions,
                                          duplicate_full, overlapping)

    def i08():
        _, _, changes = rename("sized", "interface Sized", "SizedV2", offset=10)
        impl = covered(changes, text_by_uri, starts_cache, sized_impl)
        iface = covered(changes, text_by_uri, starts_cache, sized_iface)
        return impl and iface, \
            "rename at the interface declaration: implements covered=%s, interface decl covered=%s" % (
                impl, iface)

    def i09():
        _, _, decl_changes = rename("widget", "class Widget", "WidgetV2", offset=6)
        _, _, use_changes = rename("widget", "new Widget()", "WidgetV2", offset=4)
        n_decl = edit_count(decl_changes)
        n_use = edit_count(use_changes)
        ok = decl_changes is not None and use_changes is not None \
            and n_decl >= 1 and n_decl == n_use
        return ok, "decl-site rename edits=%d, use-site rename edits=%d" % (
            n_decl, n_use)

    def i10():
        _, _, changes = rename("member", "label: string", "label2")
        sites = {
            "field declaration": member_decl,
            "this.label read": member_this_read,
            "holder.label read": member_holder_read,
        }
        missing = [name for name, site in sites.items()
                   if not covered(changes, text_by_uri, starts_cache, site)]
        return not missing, "member-path sites not renamed: %s" % (
            ", ".join(missing) or "none")

    return [
        ("I-01", "rename on the class declaration includes the declaration itself", i01),
        ("I-02", "rename on the class declaration renames every \'new Widget()\' site (>= 3 raw old-token replacements in the edits JSON)", i02),
        ("I-03", "rename on the class declaration renames at least one type-annotation site in (x: Widget): Widget", i03),
        ("I-04", "the Sized rename at the declaration includes the implements-clause site", i04),
        ("I-05", "rename at the label field declaration renames the reads inside the class method body", i05),
        ("I-06", "rename at a use site finds all same-file occurrences of combine, the export declaration included", i06),
        ("I-07", "rename result carries no duplicate edits at the same position (offsets and texts disjoint)", i07),
        ("I-08", "renaming Sized renames both the implements clause and the interface declaration", i08),
        ("I-09", "rename of Widget from a use site and from the declaration site produces the same number of edits", i09),
        ("I-10", "renaming label (a class field) renames the field declaration and its reads via the member path", i10),
    ]


# -----------------------------------------------------------------------------
# Driver
# -----------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp3c", default=DEFAULT_CP3C, help="path to cp3c.exe")
    args = parser.parse_args(argv)

    cp3c = os.path.abspath(args.cp3c)
    temp_root = os.environ.get("TEMP") or tempfile.gettempdir()
    opencode_root = os.path.join(temp_root, "opencode")
    os.makedirs(opencode_root, exist_ok=True)

    groups = {}
    text_by_uri = {}
    starts_cache = {}
    print("cp3c: %s" % cp3c)
    for name, files in GROUPS.items():
        work = tempfile.mkdtemp(prefix="rename-%s-" % name, dir=opencode_root)
        texts, uris = {}, {}
        for file_name, content in files:
            path = os.path.join(work, file_name)
            with open(path, "wb") as handle:
                handle.write(content.encode("utf-8"))
            texts[file_name] = content
            uris[file_name] = intel_check.file_uri(path)
            text_by_uri[uris[file_name]] = content
        groups[name] = {"dir": work, "texts": texts, "uris": uris}
        print("workspace: %s" % work)

    session = intel_check.Session(cp3c, groups["widget"]["dir"])
    results = []

    def check(name, ok, detail=""):
        line = ("PASS " if ok else "FAIL ") + name
        if not ok and detail:
            line += "  [%s]" % str(detail)[:300]
        print(line)
        results.append(ok)

    try:
        for name, group in groups.items():
            for file_name, content in group["texts"].items():
                session.notify("textDocument/didOpen", {
                    "textDocument": {
                        "uri": group["uris"][file_name],
                        "languageId": "cp3",
                        "version": 1,
                        "text": content,
                    },
                })
        # Barrier: lets the coalesced diagnostics drain before the checks.
        for name, group in groups.items():
            key = FILE_BY_GROUP[name]
            try:
                session.request("textDocument/hover", {
                    "textDocument": {"uri": group["uris"][key]},
                    "position": {"line": 0, "character": 0},
                }, timeout=BARRIER_TIMEOUT)
            except Exception:
                pass

        checks = make_checks(session, groups, text_by_uri, starts_cache)
        for check_id, description, run in checks:
            name = "%s %s" % (check_id, description)
            try:
                ok, detail = run()
            except Exception as exc:  # a broken answer is a FAIL, never a crash
                ok, detail = False, repr(exc)
            check(name, ok, detail if not ok else "")

        session.shutdown()
    except Exception as exc:
        # Pad the checks that never ran so every id prints and the exit code
        # is 1; the ones that did run keep their verdicts. A shutdown-time
        # failure after all ten pins passed is reported but does not fail one.
        expected = ["I-01", "I-02", "I-03", "I-04", "I-05",
                    "I-06", "I-07", "I-08", "I-09", "I-10"]
        ran = len(results)
        for check_id in expected[ran:]:
            check("%s check did not run" % check_id, False, repr(exc))
        if ran == len(expected):
            print("driver: exception after all ten checks: %r" % exc)
        try:
            session.proc.kill()
        except Exception:
            pass

    passed = sum(1 for ok in results if ok)
    print("RESULT: %d/10 passed" % passed)
    return 0 if passed == 10 else 1


if __name__ == "__main__":
    sys.exit(main())
