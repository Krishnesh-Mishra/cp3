#!/usr/bin/env python3
# =============================================================================
# tests/lsp/goto_same_file_check.py - LSP pins for Family M: same-file
# jump-to-declaration fails.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party
# packages) and prints one machine-checkable PASS / FAIL line per pin, then
# a `RESULT: <passed>/10 passed` summary. Exit code 0 when every pin passes,
# 1 otherwise.
#
# Drives the server exactly the way tests/lsp/defects_check.py drives it
# (imports tests/lsp/intel_check.py's Session stdio driver). The fixture is
# a single file, goto_same_file.cp3, opened from a fresh
# TEMP/opencode/goto-same-file-<rand> workspace per run. The workspace is
# left behind for inspection (same policy as defects_check.py).
#
# The ten pins (textDocument/definition):
#   M-01  at `Alias` in the annotation `const x: Alias` answers the type
#         alias declaration (same file)
#   M-02  at `f` in `declaredAbove = f(2)` answers the function declaration
#   M-03  at the later read of `declaredAbove` answers the `let` declaration
#   M-04  at `size` in `interface Sized { size: number }` answers the
#         interface member declaration (the identifier range on that line)
#   M-05  at `size` in `size: number = 1` inside class I answers I's field
#         declaration
#   M-06  at `load` in `s.load()` answers the Store method declaration
#         (the pin originally considered `.field` on the anonymous object
#         literal in `const obj = { field: f(9) }`; member navigation of at
#         least the class member kind is pinned instead, which the harness
#         can represent faithfully)
#   M-07  at `helper` in `this.helper(1)` (called from a sibling method)
#         answers the private helper declaration
#   M-08  at `retries` in `load(): number { return this.retries; }` answers
#         Store's field declaration
#   M-09  at `Sized` in the implements clause of I answers the interface
#         declaration (same-file companion)
#   M-10  at `f` in `const obj = { field: f(9) }` answers the function
#         declaration
#
# Each pin asserts exactly one location, a same-file uri, and range.start ==
# the declaration identifier's position (the same pinning discipline as
# tests/lsp/intel_check.py's definition checks). The fixture is ASCII so
# byte offsets and LSP (UTF-16) character offsets coincide.
#
# HEAD (2026-11-16, read by eye, not by running) notes:
#   * the definition hook (src/cli/src/commands/lsp/lsp_intel_define.cpp)
#     answers only where the compiler publishes a declaration target;
#     member (property/method) definitions deliberately answer nothing -
#     there is no member table to point a definition at - so M-04, M-05,
#     M-06, M-07 and M-08 are expected reds today;
#   * a type alias publishes no symbol (declareTypeAlias has no
#     publishSymbol call), and a type-position NamedType is not consulted
#     against resolvedBindings (identifier-only), so M-01 is expected red;
#   * M-02, M-03, M-09 and M-10 are expected green (function call callee,
#     variable read, interface implements target via the published interface
#     symbol, and the object-literal callee respectively).
# Pins state desired behaviour; checks that fail at HEAD are open defects,
# not a licence to weaken the pin.
#
# Usage (from the repo root):
#   python tests\lsp\goto_same_file_check.py --cp3c build\windows-release\bin\cp3c.exe
# =============================================================================
import argparse
import os
import sys
import time
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")

FIXTURE_NAME = "goto_same_file.cp3"

FIXTURE = (
    'type Alias = { retries: number; limit: number };\n'
    'const x: Alias = { retries: 1, limit: 60 };\n'
    'function f(x: number): number { return x * 2; }\n'
    'let declaredAbove = f(2);\n'
    'class Store {\n'
    '    retries: number = 1;\n'
    '    load(): number { return this.retries; }\n'
    '    run(): number { return this.helper(1); }\n'
    '    private helper(n: number): number { return n + this.retries; }\n'
    '}\n'
    'interface Sized { size: number; }\n'
    'class I implements Sized { size: number = 1; }\n'
    'const obj = { field: f(9) };\n'
    'const s = new Store();\n'
    'const loaded = s.load();\n'
    'const usage = declaredAbove + 1;\n'
)


def pos(needle, offset=0, occurrence=0):
    return intel_check.position(FIXTURE, needle, offset, occurrence)


# (query needle, query offset, query occurrence,
#  expected declaration needle, expected offset, expected occurrence)
PIN_SPECS = [
    ("M-01",
     "gotoDef at Alias in the const x annotation answers the type alias "
     "declaration",
     ("const x: Alias", len("const x: "), 0),
     ("type Alias", len("type "), 0)),
    ("M-02",
     "gotoDef at f in declaredAbove = f(2) answers the function declaration",
     ("declaredAbove = f(2)", len("declaredAbove = "), 0),
     ("function f", len("function "), 0)),
    ("M-03",
     "gotoDef at the later read of declaredAbove answers the let declaration",
     ("const usage = declaredAbove", len("const usage = "), 0),
     ("let declaredAbove", len("let "), 0)),
    ("M-04",
     "gotoDef at size in the interface Sized declaration answers the "
     "interface member declaration",
     ("interface Sized { size", len("interface Sized { "), 0),
     ("interface Sized { size", len("interface Sized { "), 0)),
    ("M-05",
     "gotoDef at size in size: number = 1 answers the class I field "
     "declaration",
     ("size: number = 1", 0, 0),
     ("size: number = 1", 0, 0)),
    ("M-06",
     "gotoDef at load in s.load() answers the Store method declaration",
     ("s.load()", len("s."), 0),
     ("load(): number", 0, 0)),
    ("M-07",
     "gotoDef at helper in this.helper(1) answers the helper declaration",
     ("this.helper(1)", len("this."), 0),
     ("private helper(", len("private "), 0)),
    ("M-08",
     "gotoDef at retries in the load body answers the Store field "
     "declaration",
     ("return this.retries", len("return this."), 0),
     ("retries: number = 1", 0, 0)),
    ("M-09",
     "gotoDef at Sized in the implements clause answers the interface "
     "declaration",
     ("class I implements Sized", len("class I implements "), 0),
     ("interface Sized", len("interface "), 0)),
    ("M-10",
     "gotoDef at f in field: f(9) answers the function declaration",
     ("field: f(9)", len("field: "), 0),
     ("function f", len("function "), 0)),
]


def check_definition(session, uri, query, expected):
    needle, offset, occurrence = query
    position = pos(needle, offset, occurrence)
    expected_needle, expected_offset, expected_occurrence = expected
    expected_start = pos(expected_needle, expected_offset, expected_occurrence)

    response = session.request("textDocument/definition", {
        "textDocument": {"uri": uri},
        "position": position,
    }, timeout=30.0)

    error = response.get("error")
    found = intel_check.locations(response)
    if error is not None:
        return False, "error: %r" % (error,)
    if len(found) != 1:
        return False, "expected one location, got %r" % (found,)
    target_uri = str(found[0].get("uri", "")).lower()
    if not target_uri.endswith(FIXTURE_NAME):
        return False, "expected a location in %s, got uri %r" % (
            FIXTURE_NAME, target_uri)
    start = found[0].get("range", {}).get("start")
    if start != expected_start:
        return False, "range.start %r, expected %r" % (start, expected_start)
    return True, ""


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp3c", default=DEFAULT_CP3C, help="path to cp3c.exe")
    args = parser.parse_args(argv)
    cp3c = os.path.abspath(args.cp3c)

    temp_root = os.environ.get("TEMP") or os.environ.get("TMP") or "."
    work = os.path.join(temp_root, "opencode",
                        "goto-same-file-%s" % uuid.uuid4().hex[:8])
    os.makedirs(work, exist_ok=True)

    fixture_path = os.path.join(work, FIXTURE_NAME)
    with open(fixture_path, "wb") as handle:
        handle.write(FIXTURE.encode("utf-8"))
    uri = intel_check.file_uri(fixture_path)

    session = intel_check.Session(cp3c, work)
    startup_error = None
    try:
        session.notify("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "cp3",
                "version": 1,
                "text": FIXTURE,
            },
        })
        # Barrier: let the coalesced diagnostics drain before the pins.
        try:
            session.request("textDocument/hover", {
                "textDocument": {"uri": uri},
                "position": {"line": 0, "character": 0},
            }, timeout=30.0)
        except Exception:
            pass
        time.sleep(0.5)
    except Exception as exc:
        startup_error = exc

    results = []
    for pin_id, desc, query, expected in PIN_SPECS:
        if startup_error is not None:
            ok, detail = False, "driver startup failed: %r" % (startup_error,)
        else:
            try:
                ok, detail = check_definition(session, uri, query, expected)
            except Exception as exc:
                ok, detail = False, repr(exc)
        line = ("PASS " if ok else "FAIL ") + pin_id + " " + desc
        if not ok and detail:
            line += "  [%s]" % str(detail)[:300]
        print(line)
        results.append(ok)

    try:
        session.shutdown()
    except Exception:
        pass

    passed = sum(1 for ok in results if ok)
    print("RESULT: %d/10 passed" % passed)
    return 0 if passed == len(PIN_SPECS) else 1


if __name__ == "__main__":
    sys.exit(main())
