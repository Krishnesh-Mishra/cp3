#!/usr/bin/env python3
# =============================================================================
# tests/lsp/completion_visibility_check.py - LSP completion-visibility pins.
#
# Companion to tests/lsp/intel_check.py (the shared stdio driver) and
# tests/lsp/defects_check.py. Drives a real `cp3c lsp` process over stdio
# (no ports, no third-party packages) and pins two defect families filed from
# the editor side:
#
#   * Family G (G-01..G-03, member completion for imported types and sibling
#     subclasses). Member completion today resolves receivers with tables that
#     only see the entry file: kMemberUniverse has no user-declared member,
#     appendShapeFields scans document.entry->ast->body only (an imported class
#     or interface is not there), the synthetic probe is fed exactly those
#     names, and the class `extends` chain is never walked for completion.
#
#       G-01 completion at `s.` (s = new Store(), Store imported) offers
#           `limit`.
#       G-02 completion at `x.` (x: P, P an imported interface) offers
#           `retries`.
#       G-03 completion at `sib.` (Sib extends Base, a sibling subclass of
#           Sub in the same module) offers the inherited `report` and does
#           NOT offer Sub's own `get`.
#
#   * Family H (H-01..H-11, contextual completion visibility: keywords,
#     locals, closure params).
#
#       H-01 completion on the empty line inside an empty `try` block offers at
#           least one of [`if`, `const`, `return`] (the statement-start
#           keyword group).
#       H-02 the same on the empty line inside `finally { ... }`.
#       H-03 completion after the partial `a` at statement-start position of an
#           async function offers `await`.
#       H-04 completion after `const c = inn` offers the nested function
#           declaration `inner`.
#       H-05 completion at statement position (an empty line) inside `outer`'s
#           body offers `inner` too. (The report says "member position"; the
#           server's CompletionPosition::MemberPosition enum exists but is
#           never produced - member access is served by memberCandidates -
#           so the pin is expressed at the other position a body offers
#           names at: the statement position. Interpretation documented.)
#       H-06 recursion: completion after `return fi` offers `fib`.
#       H-07 closure capture: completion at the tail of
#           `const g = (x: number): number => x + li` offers `limit`.
#       H-08 loop variable: `i` offered after `i` inside the loop body, and
#           NOT offered after `i` outside the loop in the same function. Both
#           polarities are pinned. The task pins the two polarities as "two
#           checks"; they travel in ONE check line here so the suite keeps
#           exactly 14 checks (3 G + 11 H). The detail output reports the
#           failing polarity when the check fails.
#       H-09 completion after `o.retr` (o = object literal with `retries`)
#           offers `retries` - a partial member prefix must not be swallowed
#           by a keyword set.
#       H-10 the loop variable of an enclosing `for ... of` loop is offered
#           inside a nested arrow closure after typing `y` at the tail of
#           `return y`. (The report's parenthetical fixture uses
#           `arr.forEach((z) => ...)`; arrays have no forEach in the language,
#           so the nested closure is pinned as a plain arrow function declared
#           in the loop body - same capture path, no unsupported std API.)
#       H-11 completion at statement position inside a `catch (e) { ... }`
#           block offers a statement-start keyword (same class as H-01/H-02).
#           Member completion on the untyped catch binding `e.` is NOT pinned:
#           an untyped catch parameter is ambient, so that answer is not
#           deterministic; the statement-keyword fallback was chosen per the
#           report's instructions.
#
# Requirements honoured: fixtures live in a fresh TEMP/opencode/<name>-<rand>
# directory; ASCII only (byte, UTF-16 and character offsets then coincide);
# every request carries a bounded timeout; English identifiers only; `src/`
# and the existing test files are not touched.
#
# Exit code 0 when every check passes, 1 otherwise.
#
# Usage (from the repo root):
#   python tests\lsp\completion_visibility_check.py --cp3c build\windows-release\bin\cp3c.exe
#
# Expected at HEAD (source-derived PREDICTIONS, this file was written without
# running the server; run it to confirm):
#   PASS  H-01, H-02, H-06, H-07, H-08, H-09, H-10, H-11  (8)
#   FAIL  G-01, G-02, G-03  (imported-type member completion not wired)
#   FAIL  H-03              (statement-start keyword set lacks `await`)
#   FAIL  H-04, H-05        (nested declarations are never listed:
#                            appendDeclarationCandidates walks the top-level
#                            body only, scopeCandidates binds parameters and
#                            variable declarators, never function
#                            declarations)
# => 8/14 expected at HEAD. Pins are NOT weakened to reach green; a green
#    run here means the defects are fixed, not that the tests were relaxed.
# =============================================================================
import argparse
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")

# -----------------------------------------------------------------------------
# Fixtures (ASCII only)
# -----------------------------------------------------------------------------

EXPORTED_STORE = (
    'export class Store { constructor() {} limit(): number { return 5; } }\n'
)

MAIN_G01 = (
    'import { Store } from "./exported_store.cp3";\n'
    'const s = new Store();\n'
    's.\n'
)

PARAMS = (
    'export interface P { retries: number }\n'
)

INTERFACE_IMPORT = (
    'import { P } from "./params.cp3";\n'
    'function f(x: P): void {\n'
    '  x.\n'
    '}\n'
)

BASE = (
    'export class Base { report(): number { return 1; } }\n'
)

SIBLING_A = (
    'import { Base } from "./base.cp3";\n'
    'class Sub extends Base {\n'
    '    get(): number { return 1; }\n'
    '}\n'
    'const a = new Sub();\n'
    'class Sib extends Base {\n'
    '}\n'
    'const sib = new Sib();\n'
    'sib.\n'
)

# H-01: an empty `try` block at the end of the file (the editor state right
# after the user types `try {`).
H01_TRY_EMPTY = (
    'function main(): void {\n'
    '  try {\n'
    '    \n'
)

# H-02: an empty `finally` block at the end of the file.
H02_FINALLY_EMPTY = (
    'function main(): void {\n'
    '  try {\n'
    '  } finally {\n'
    '    \n'
)

H03_ASYNC_AWAIT = (
    'async function load(): Promise<void> {\n'
    '  a\n'
    '}\n'
)

# H-04/H-05 share this fixture: line 1 is the empty line at statement
# position inside `outer`'s body.
H04_NESTED_FUNCTION = (
    'function outer(): void {\n'
    '  \n'
    '  function inner(): void {}\n'
    '  const c = inn\n'
    '}\n'
)

H06_RECURSION = (
    'function fib(n: number): number {\n'
    '  return fi\n'
    '}\n'
)

H07_CLOSURE_CAPTURE = (
    'const limit = 5;\n'
    'const g = (x: number): number => x + li\n'
)

H08_LOOP_VARIABLE = (
    'function run(): void {\n'
    '  for (const i of [1, 2]) {\n'
    '    i\n'
    '  }\n'
    '  const h = i\n'
    '}\n'
)

H09_MEMBER_PREFIX = (
    'const o = { retries: 1 };\n'
    'o.retr\n'
)

H10_NESTED_CLOSURE = (
    'function main(items: number[], arr: number[]): void {\n'
    '  for (const y of items) {\n'
    '    const f = (z: number): number => {\n'
    '      return y\n'
    '    };\n'
    '    arr.push(f(y));\n'
    '  }\n'
    '}\n'
)

H11_CATCH_STATEMENT = (
    'function main(): void {\n'
    '  try {\n'
    '  } catch (e) {\n'
    '    \n'
    '  }\n'
    '}\n'
)

REQUEST_TIMEOUT = 30.0

STATEMENT_START_KEYWORDS = ("if", "const", "return")


def write(work, name, text):
    path = os.path.join(work, name)
    with open(path, "wb") as handle:
        handle.write(text.encode("utf-8"))
    return intel_check.file_uri(path)


def pos(text, needle, offset=0, occurrence=0):
    return intel_check.position(text, needle, offset, occurrence)


def completion_labels(session, uri, position, timeout=REQUEST_TIMEOUT):
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": uri},
        "position": position,
    }, timeout=timeout)
    result = response.get("result")
    if not isinstance(result, dict):
        return None
    items = result.get("items")
    if not isinstance(items, list):
        return None
    return [item.get("label") for item in items if isinstance(item, dict)]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp3c", default=DEFAULT_CP3C, help="path to cp3c.exe")
    args = parser.parse_args(argv)

    temp_root = os.path.join(os.environ.get("TEMP")
                             or os.environ.get("TMP")
                             or tempfile.gettempdir(),
                             "opencode")
    os.makedirs(temp_root, exist_ok=True)
    work = tempfile.mkdtemp(prefix="cp3-completion-visibility-", dir=temp_root)

    fixture_specs = [
        ("exported_store.cp3", EXPORTED_STORE),
        ("main.cp3", MAIN_G01),
        ("params.cp3", PARAMS),
        ("interface_import.cp3", INTERFACE_IMPORT),
        ("base.cp3", BASE),
        ("sibling_a.cp3", SIBLING_A),
        ("h01_try_empty.cp3", H01_TRY_EMPTY),
        ("h02_finally_empty.cp3", H02_FINALLY_EMPTY),
        ("h03_async_await.cp3", H03_ASYNC_AWAIT),
        ("h04_nested_function.cp3", H04_NESTED_FUNCTION),
        ("h06_recursion.cp3", H06_RECURSION),
        ("h07_closure_capture.cp3", H07_CLOSURE_CAPTURE),
        ("h08_loop_variable.cp3", H08_LOOP_VARIABLE),
        ("h09_member_prefix.cp3", H09_MEMBER_PREFIX),
        ("h10_nested_closure.cp3", H10_NESTED_CLOSURE),
        ("h11_catch_statement.cp3", H11_CATCH_STATEMENT),
    ]
    uris = {name: write(work, name, text) for name, text in fixture_specs}

    print("cp3c: %s" % os.path.abspath(args.cp3c))
    print("fixtures: %s" % work)

    results = []

    def check(name, ok, detail=""):
        line = ("PASS " if ok else "FAIL ") + name
        if not ok and detail != "":
            line += "  [%s]" % str(detail)[:300]
        print(line)
        results.append(bool(ok))

    session = intel_check.Session(args.cp3c, work)
    try:
        init = session.request("initialize", {
            "processId": None,
            "rootUri": intel_check.file_uri(work),
            "capabilities": {},
        })
        if "error" in init:
            raise RuntimeError("initialize failed: %r" % init["error"])
        for name, text in fixture_specs:
            session.notify("textDocument/didOpen", {
                "textDocument": {
                    "uri": uris[name],
                    "languageId": "cp3",
                    "version": 1,
                    "text": text,
                },
            })

        # ---- Family G ------------------------------------------------------

        found = completion_labels(
            session, uris["main.cp3"],
            pos(MAIN_G01, "s.\n", 2))
        check("G-01 completion at `s.` offers `limit` (member of the imported class)",
              found is not None and "limit" in found, found)

        found = completion_labels(
            session, uris["interface_import.cp3"],
            pos(INTERFACE_IMPORT, "x.\n", 2))
        check("G-02 completion at `x.` offers `retries` (member of the imported interface)",
              found is not None and "retries" in found, found)

        found = completion_labels(
            session, uris["sibling_a.cp3"],
            pos(SIBLING_A, "sib.\n", 4))
        report_offered = found is not None and "report" in found
        get_offered = found is not None and "get" in found
        check("G-03 completion at `sib.` offers inherited `report` and not sibling `get`",
              report_offered and not get_offered,
              {"report_offered": report_offered,
               "get_offered": get_offered,
               "labels": found})

        # ---- Family H ------------------------------------------------------

        found = completion_labels(
            session, uris["h01_try_empty.cp3"],
            pos(H01_TRY_EMPTY, "    \n", 4))
        check("H-01 completion on the empty line inside an empty `try` offers a statement-start keyword",
              found is not None
              and any(name in found for name in STATEMENT_START_KEYWORDS),
              found)

        found = completion_labels(
            session, uris["h02_finally_empty.cp3"],
            pos(H02_FINALLY_EMPTY, "    \n", 4))
        check("H-02 completion on the empty line inside `finally { ... }` offers a statement-start keyword",
              found is not None
              and any(name in found for name in STATEMENT_START_KEYWORDS),
              found)

        found = completion_labels(
            session, uris["h03_async_await.cp3"],
            pos(H03_ASYNC_AWAIT, "  a\n", 3))
        check("H-03 statement-start position in an async function offers `await`",
              found is not None and "await" in found, found)

        found = completion_labels(
            session, uris["h04_nested_function.cp3"],
            pos(H04_NESTED_FUNCTION, "inn\n", 3))
        check("H-04 completion after a partial name offers the nested function `inner`",
              found is not None and "inner" in found, found)

        found = completion_labels(
            session, uris["h04_nested_function.cp3"],
            pos(H04_NESTED_FUNCTION, "\n  \n", 3))
        check("H-05 statement position in `outer`'s body offers the nested function `inner`",
              found is not None and "inner" in found, found)

        found = completion_labels(
            session, uris["h06_recursion.cp3"],
            pos(H06_RECURSION, "fi\n", 2))
        check("H-06 completion after `return fi` offers the recursive function `fib`",
              found is not None and "fib" in found, found)

        found = completion_labels(
            session, uris["h07_closure_capture.cp3"],
            pos(H07_CLOSURE_CAPTURE, "li\n", 2))
        check("H-07 completion inside the arrow offers the enclosing scope's `limit`",
              found is not None and "limit" in found, found)

        inside = completion_labels(
            session, uris["h08_loop_variable.cp3"],
            pos(H08_LOOP_VARIABLE, "    i\n", 5))
        outside = completion_labels(
            session, uris["h08_loop_variable.cp3"],
            pos(H08_LOOP_VARIABLE, "const h = i\n", len("const h = i")))
        inside_ok = inside is not None and "i" in inside
        outside_ok = outside is not None and "i" not in outside
        check("H-08 loop variable `i` offered inside the loop body and not outside it",
              inside_ok and outside_ok,
              {"inside_offers_i": inside_ok,
               "outside_offers_i": not outside_ok,
               "inside": inside,
               "outside": outside})

        found = completion_labels(
            session, uris["h09_member_prefix.cp3"],
            pos(H09_MEMBER_PREFIX, "o.retr\n", len("o.retr")))
        check("H-09 partial member prefix after an object literal offers `retries`",
              found is not None and "retries" in found, found)

        found = completion_labels(
            session, uris["h10_nested_closure.cp3"],
            pos(H10_NESTED_CLOSURE, "return y\n", len("return y")))
        check("H-10 loop variable `y` offered inside a nested arrow closure",
              found is not None and "y" in found, found)

        found = completion_labels(
            session, uris["h11_catch_statement.cp3"],
            pos(H11_CATCH_STATEMENT, "    \n", 4))
        check("H-11 completion on the empty line inside `catch (e) { ... }` offers a statement-start keyword",
              found is not None
              and any(name in found for name in STATEMENT_START_KEYWORDS),
              found)

        # No 15th check: the suite is exactly the 14 pins above. A nonzero
        # process exit is reported as a note, not as a check line.
        _, exit_code = session.shutdown()
        if exit_code != 0:
            print("note: server exited with code %s" % exit_code)
    except Exception as exc:  # noqa: BLE001 - report, never mask the driver failure
        check("driver: no exception", False, repr(exc))
        try:
            session.proc.kill()
        except Exception:
            pass

    passed = sum(1 for ok in results if ok)
    failed = len(results) - passed
    print("RESULT: %d/14 passed" % passed)
    if failed:
        print("failing: %d check(s)" % failed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
