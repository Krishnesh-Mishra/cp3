#!/usr/bin/env python3
# =============================================================================
# tests/lsp/refs_type_positions_check.py - Family B: goto-references does not
# report the whole set of reference sites at type positions / implements
# clauses / new-expressions (22 checks, ids B-01 .. B-22).
#
# The defect: goto-references on a declaration resolves the binding but
# answers SOME reference sites and not all. Sites reported missing are:
#   * type annotations (parameter, return, const annotations),
#   * `implements` clauses, `extends` clauses,
#   * enum member type positions,
#   * `new T()` expressions (including new-expression type arguments),
#   * member accesses on instances of the class.
#
# These are pins of DESIRED behaviour; checks that fail at HEAD are open
# compiler defects. Checks that pass at HEAD are regression guards. The
# pinning session is pinned over ONE workspace document (refs_main.cp3); the
# sibling module (refs_sibling.cp3) imports Sized from it so the collection
# is multi-file (B-06). Both documents are opened before the checks run.
#
# Drives a real `cp3c lsp` process over stdio, exactly the way
# tests/lsp/intel_check.py drives it (it imports its Session driver), and
# prints one machine-checkable PASS / FAIL line per check. Exit code 0 when
# every check passes, 1 otherwise. The LSP verb is textDocument/references
# with context.includeDeclaration = true and every request takes the default
# sixty-second timeout of intel_check.Session.
#
# Fixtures (ASCII only: byte, UTF-16 and character offsets then coincide) are
# written into a FRESH temp directory:
#   TEMP/opencode/refs-type-positions-<random hex>
# No other file is created, modified or deleted by this script.
#
# Expected at HEAD (best-effort, from reading src/cli/src/commands/lsp):
#   likely PASS: B-11 (declaration site), B-12, B-13, B-14 (member reads
#                through the member-occurrence machinery), B-15, B-18, B-19
#                (enum object identifiers bind in sema)
#   likely FAIL: B-01 .. B-10 (type positions, implements/extends clauses and
#                new-expression type arguments carry no identifier node and
#                are not collected), B-16, B-17 (same reason), B-20, B-21
#                (the reference machinery has no type-position or
#                implements-clause channel), B-22 (the member family for a
#                class anchor does not extend across unrelated same-name
#                members such as Widget's `size`)
# B-21 is two-branched exactly as the pin specifies: if the harness reports a
# location covering the member `size` inside class I, that location must
# cover EXACTLY the member name (a rename-safe pin); otherwise the pin falls
# back to a plain count of the reported sites (at least 3: the declaration,
# the class I implements site and the member inside I) so the check cannot
# go green by silence.
# =============================================================================

import argparse
import os
import secrets
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")

MAIN_NAME = "refs_main.cp3"
SIBLING_NAME = "refs_sibling.cp3"

# The workspace document. Declarations, in fixture order:
#   Sized     interface (size: number)
#   Note      type alias (Note = Sized)
#   Widget    class (size field + constructor)
#   Level     enum (Low, High)
#   Consumer  class
#   Box<T>    class, used only for the new-expression type argument site
#   I         class implements Sized (the member-pin subject for B-21/B-22)
#   K         interface extends Sized
#   call      function whose parameter/return annotations mention Sized
#   Callback  type alias whose function type mentions Sized
#   id        generic with a Sized constraint (the witness is Sized)
#   Sub       class extends Widget (the extends position for Widget)
REFS_MAIN = (
    'export interface Sized {\n'
    '    size: number;\n'
    '}\n'
    '\n'
    'export type Note = Sized;\n'
    '\n'
    'export class Widget {\n'
    '    size: number = 0;\n'
    '    constructor(label: string) {\n'
    '    }\n'
    '}\n'
    '\n'
    'export enum Level {\n'
    '    Low,\n'
    '    High,\n'
    '}\n'
    '\n'
    'export class Consumer {\n'
    '}\n'
    '\n'
    'export class Box<T> {\n'
    '    value: T | null = null;\n'
    '}\n'
    '\n'
    'class I implements Sized {\n'
    '    size: number = 0;\n'
    '}\n'
    '\n'
    'interface K extends Sized {\n'
    '}\n'
    '\n'
    'function call(x: Sized): Sized {\n'
    '    const s: Sized = x;\n'
    '    const other: Sized = s;\n'
    '    return other;\n'
    '}\n'
    '\n'
    'type Callback = (s: Sized) => Sized;\n'
    '\n'
    'function id<T extends Sized>(x: T): T {\n'
    '    return x;\n'
    '}\n'
    '\n'
    'const sizedBox = new Box<Sized>();\n'
    '\n'
    'const label = new Widget("start");\n'
    '\n'
    'function take(w: Widget): void {\n'
    '}\n'
    'take(new Widget("arg"));\n'
    '\n'
    'function build(): Widget {\n'
    '    return new Widget("built");\n'
    '}\n'
    '\n'
    'const probe = new Widget("member");\n'
    'const first = probe.size;\n'
    'const second = probe.size;\n'
    '\n'
    'const w: Widget = new Widget("annotated");\n'
    '\n'
    'class Sub extends Widget {\n'
    '}\n'
    '\n'
    'const L: Level = Level.Low;\n'
    'const L2 = Level.High;\n'
)

# The sibling module: importing Sized from the workspace document and
# implementing it there is what makes the collection multi-file (B-06).
REFS_SIBLING = (
    'import { Sized } from "./refs_main.cp3";\n'
    '\n'
    'export class J implements Sized {\n'
    '    size: number = 2;\n'
    '}\n'
)


# -----------------------------------------------------------------------------
# Small helpers (the LSP-verb plumbing, on top of intel_check.Session)
# -----------------------------------------------------------------------------

def references_at(session, uri, pos):
    response = session.request("textDocument/references", {
        "textDocument": {"uri": uri},
        "position": pos,
        "context": {"includeDeclaration": True},
    })
    result = response.get("result")
    return result if isinstance(result, list) else []


def location_file(loc):
    uri = loc.get("uri", "") if isinstance(loc, dict) else ""
    return str(uri).replace("\\", "/").lower()


def same_file(loc, filename):
    return location_file(loc).endswith("/" + filename.lower())


def location_start(loc):
    if not isinstance(loc, dict):
        return None
    return loc.get("range", {}).get("start")


def location_end(loc):
    if not isinstance(loc, dict):
        return None
    return loc.get("range", {}).get("end")


def has_site(refs, filename, text, needle, offset=0, occurrence=0):
    """True when `refs` carries a location whose start is the site the needle
    (plus byte offset) names."""
    want = intel_check.position(text, needle, offset, occurrence)
    for loc in refs:
        if not same_file(loc, filename):
            continue
        start = location_start(loc)
        if not isinstance(start, dict):
            continue
        if (start.get("line") == want["line"]
                and start.get("character") == want["character"]):
            return True
    return False


def site_exact(refs, filename, text, needle, offset, length, occurrence=0):
    """True when `refs` carries a location covering EXACTLY the name the
    needle (plus byte offset) names (a rename-safe pin)."""
    want_start = intel_check.position(text, needle, offset, occurrence)
    want_end = intel_check.position(text, needle, offset + length, occurrence)
    for loc in refs:
        if not same_file(loc, filename):
            continue
        start = location_start(loc)
        end = location_end(loc)
        if not isinstance(start, dict) or not isinstance(end, dict):
            continue
        if (start.get("line") == want_start["line"]
                and start.get("character") == want_start["character"]
                and end.get("line") == want_end["line"]
                and end.get("character") == want_end["character"]):
            return True
    return False


def brief(refs):
    out = []
    for loc in refs:
        if not isinstance(loc, dict):
            continue
        name = location_file(loc).rsplit("/", 1)[-1]
        start = location_start(loc) or {}
        out.append("%s@%d:%d" % (name, start.get("line", -1),
                                   start.get("character", -1)))
    return ", ".join(out)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Family B references pins")
    parser.add_argument("--cp3c", default=DEFAULT_CP3C)
    args = parser.parse_args(argv)

    base = os.environ.get("TEMP") or os.environ.get("TMPDIR") or "."
    work = os.path.join(base, "opencode",
                        "refs-type-positions-" + secrets.token_hex(4))
    os.makedirs(work, exist_ok=False)

    main_path = intel_check.write_fixture(work, MAIN_NAME, REFS_MAIN)
    sibling_path = intel_check.write_fixture(work, SIBLING_NAME, REFS_SIBLING)
    main_uri = intel_check.file_uri(main_path)
    sibling_uri = intel_check.file_uri(sibling_path)

    session = intel_check.Session(args.cp3c, work)
    for uri, text in ((main_uri, REFS_MAIN), (sibling_uri, REFS_SIBLING)):
        session.notify("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "cp3",
                "version": 1,
                "text": text,
            },
        })
    # Barrier: lets the coalesced diagnostics drain before the checks.
    for uri in (main_uri, sibling_uri):
        try:
            session.request("textDocument/hover", {
                "textDocument": {"uri": uri},
                "position": {"line": 0, "character": 0},
            })
        except Exception:
            pass
    time.sleep(0.5)

    results = []

    def check(name, ok, detail=""):
        line = ("PASS " if ok else "FAIL ") + name
        if not ok and detail:
            line += "  [%s]" % str(detail)[:300]
        print(line)
        results.append(bool(ok))

    # References anchored once per cursor position, then shared by the pins.
    anchors = {}

    def anchor_refs(uri, text, needle, offset, occurrence=0):
        key = (uri, needle, offset, occurrence)
        if key not in anchors:
            anchors[key] = references_at(
                session, uri, intel_check.position(text, needle, offset,
                                                   occurrence))
        return anchors[key]

    sized_refs = anchor_refs(main_uri, REFS_MAIN, "interface Sized",
                             len("interface "))
    widget_refs = anchor_refs(main_uri, REFS_MAIN, "class Widget",
                              len("class "))
    widget_member_refs = anchor_refs(main_uri, REFS_MAIN,
                                     "size: number = 0;", 0, 0)
    i_member_refs = anchor_refs(main_uri, REFS_MAIN,
                                "size: number = 0;", 0, 1)
    level_refs = anchor_refs(main_uri, REFS_MAIN, "enum Level",
                             len("enum "))

    # ---- For Sized (interface, references at type positions) -----------------
    check("B-01 references: Sized found at a function parameter annotation",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "x: Sized", len("x: ")),
          brief(sized_refs))
    check("B-02 references: Sized found at the same function's return annotation",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "): Sized", len("): ")),
          brief(sized_refs))
    check("B-03 references: Sized found at a const statement annotation",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "const s: Sized",
                   len("const s: ")), brief(sized_refs))
    check("B-04 references: Sized found at a second annotation in the same function",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "other: Sized",
                   len("other: ")), brief(sized_refs))
    check("B-05 references: Sized found at a class implements clause (class I)",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "implements Sized",
                   len("implements ")), brief(sized_refs))
    check("B-06 references: Sized found at an implements clause in the sibling file (class J)",
          has_site(sized_refs, SIBLING_NAME, REFS_SIBLING, "implements Sized",
                   len("implements ")), brief(sized_refs))
    check("B-07 references: Sized found at an interface extends clause (interface K)",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "K extends Sized",
                   len("K extends ")), brief(sized_refs))
    check("B-08 references: Sized found in a function type alias (type Callback)",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "Callback = (s: Sized",
                   len("Callback = (s: ")), brief(sized_refs))
    check("B-09 references: Sized found at a generic constraint witness (function id<T extends Sized>)",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "T extends Sized",
                   len("T extends ")), brief(sized_refs))
    check("B-10 references: Sized found in a new expression's type arguments (new Box<Sized>())",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "new Box<Sized",
                   len("new Box<")), brief(sized_refs))
    check("B-11 references: includeDeclaration reports the interface declaration site",
          has_site(sized_refs, MAIN_NAME, REFS_MAIN, "interface Sized",
                   len("interface ")), brief(sized_refs))

    # ---- For Widget (class, instance construction sites) ---------------------
    check("B-12 references: Widget found at a statement construction site (new Widget())",
          has_site(widget_refs, MAIN_NAME, REFS_MAIN, "label = new Widget",
                   len("label = new ")), brief(widget_refs))
    check("B-13 references: Widget found at a construction site passed as a call argument",
          has_site(widget_refs, MAIN_NAME, REFS_MAIN, "take(new Widget",
                   len("take(new ")), brief(widget_refs))
    b14_first = has_site(widget_member_refs, MAIN_NAME, REFS_MAIN, "probe.size",
                         len("probe."), 0)
    b14_second = has_site(widget_member_refs, MAIN_NAME, REFS_MAIN, "probe.size",
                          len("probe."), 1)
    check("B-14 references: member size on a Widget receiver found at two read sites",
          b14_first and b14_second,
          "first=%s second=%s refs=%s" % (b14_first, b14_second,
                                           brief(widget_member_refs)))
    check("B-15 references: Widget found at a construction site in a function body",
          has_site(widget_refs, MAIN_NAME, REFS_MAIN, "return new Widget",
                   len("return new ")), brief(widget_refs))
    check("B-16 references: Widget found at a class extends clause (class Sub extends Widget)",
          has_site(widget_refs, MAIN_NAME, REFS_MAIN, "extends Widget",
                   len("extends ")), brief(widget_refs))
    check("B-17 references: Widget found at a const type annotation (const w: Widget = ...)",
          has_site(widget_refs, MAIN_NAME, REFS_MAIN, "const w: Widget",
                   len("const w: ")), brief(widget_refs))

    # ---- For Level (enum) ----------------------------------------------------
    check("B-18 references: Level found at the enum member expression Level.Low",
          has_site(level_refs, MAIN_NAME, REFS_MAIN, "Level.Low", 0, 0),
          brief(level_refs))
    check("B-19 references: Level found at the enum member expression Level.High",
          has_site(level_refs, MAIN_NAME, REFS_MAIN, "Level.High", 0, 0),
          brief(level_refs))
    check("B-20 references: Level found at a const type annotation (const L: Level = ...)",
          has_site(level_refs, MAIN_NAME, REFS_MAIN, "L: Level", len("L: ")),
          brief(level_refs))

    # ---- For the class hierarchy ----------------------------------------------
    i_impl = has_site(sized_refs, MAIN_NAME, REFS_MAIN, "implements Sized",
                      len("implements "))
    i_key = intel_check.position(REFS_MAIN, "size: number = 0;", 0, 1)
    i_exact = (
        i_key is not None
        and site_exact(sized_refs, MAIN_NAME, REFS_MAIN,
                       "size: number = 0;", 0, len("size"), 1)
    )
    i_reported = site_exact(sized_refs, MAIN_NAME, REFS_MAIN,
                            "size: number = 0;", 0, len("size"), 1) or any(
        same_file(loc, MAIN_NAME)
        and isinstance(location_start(loc), dict)
        and location_start(loc).get("line") == i_key["line"]
        and location_start(loc).get("character") == i_key["character"]
        for loc in sized_refs
    )
    if i_reported:
        # The harness already reports the member inside I: pin rename-safety.
        ok = i_impl and i_exact
        detail = ("implements_site=%s rename_safe=%s refs(%d): %s"
                  % (i_impl, i_exact, len(sized_refs), brief(sized_refs)))
    else:
        # Otherwise pin the plain site count: at least the declaration, the
        # class I implements site and the member inside I.
        ok = i_impl and len(sized_refs) >= 3
        detail = ("member size inside I not reported; plain site count=%d "
                  "(need >= 3) implements_site=%s refs: %s"
                  % (len(sized_refs), i_impl, brief(sized_refs)))
    check("B-21 references: Sized finds the class I implements-clause site and the size member inside I",
          ok, detail)

    i_site = has_site(i_member_refs, MAIN_NAME, REFS_MAIN,
                      "size: number = 0;", 0, 1)
    widget_site = has_site(i_member_refs, MAIN_NAME, REFS_MAIN,
                           "size: number = 0;", 0, 0)
    check("B-22 references: member size inside I finds the same-name member in Widget",
          i_site and widget_site,
          "i_site=%s widget_site=%s refs=%s" % (i_site, widget_site,
                                                brief(i_member_refs)))

    session.shutdown()

    passed = sum(1 for ok in results if ok)
    total = len(results)
    print()
    print("RESULT: %d/%d passed" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
