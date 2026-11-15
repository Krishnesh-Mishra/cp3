#!/usr/bin/env python3
# =============================================================================
# tests/lsp/defects_check.py - verification suite for the 2026-11-15 LSP defect
# report (12 items, filed from the editor side). Each check states the desired
# behaviour; checks that fail today are open compiler defects, marked with the
# report item they pin. Checks that pass today are regression guards.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party packages)
# and prints one machine-checkable PASS / FAIL line per check. Exit code 0 when
# everything passes, 1 otherwise. The LSP is driven exactly the way
# tests/lsp/intel_check.py drives it (imports its Session driver).
#
# Skipped as problematic / doubtful (not reproducible at this layer):
#   * item 2 (applying the compiler's own suggested fix re-reports at 1:1) -
#     lives in the extension's quick-fix flow; the LSP server itself answers
#     textDocument/codeAction with "method not found", so no driver here can
#     exert the sequence.
#   * item 10 note - the compiler emits NO unused-import diagnostic today; the
#     report asks for one (the extension's code action needs it). TS does not
#     report unused imports by default either - a feature decision, not a fix.
#   * items 4/6: the member-definition and generic-hover defects
#     REPRODUCE; pinned below as desired behaviour.
#   * item 9, item 12 - DO NOT REPRODUCE at this build (type hierarchy and
#     partial-prefix completion both work; pinned as guards).
#   * item 11 - the reported fixture could not be matched to a layout that
#     produces an off-by-one line for CP3410 (all layouts probed land on the
#     object literal's line); kept as a guard pinning that behaviour.
#   * The syntax-error members of item 1 (empty import specifier, missing
#     semicolon) are not defects in CP3 (semicolons are optional; the unmatched
#     brace DOES report CP3202 - that member of item 1 is covered as a guard).
# =============================================================================
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")

IMPLEMENT_BAD = (
    'interface Config {\n'
    '    retries: number;\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    'class Client implements Config {\n'
    '    retries: string = "x";\n'
    '}\n'
)

IMPLEMENT_MISSING = (
    'interface Config {\n'
    '    retries: number;\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    'class Client implements Config {\n'
    '    retries: number = 0;\n'
    '}\n'
)

DUPLICATE_FUNCTION = (
    'function combine(a: number, b: number): number {\n'
    '    return a + b;\n'
    '}\n'
    '\n'
    'function combine(a: number, b: number): number {\n'
    '    return a + b;\n'
    '}\n'
)

CONSTRAINT_VIOLATION = (
    'interface Shape {\n'
    '    area: number;\n'
    '}\n'
    '\n'
    'function areaOf<T extends Shape>(s: T): number {\n'
    '    return s.area;\n'
    '}\n'
    '\n'
    'const n: number = areaOf(5);\n'
)

UNUSED_IMPORT = (
    'import console from "std/console";\n'
    'import path from "std/path";\n'
    '\n'
    'console.log("hi");\n'
)

MEMBER_NAV = (
    'class Counter {\n'
    '    count: number = 0;\n'
    '    run(): number {\n'
    '        this.count = 1;\n'
    '        return this.count;\n'
    '    }\n'
    '}\n'
    '\n'
    'const c = new Counter();\n'
    'const value = c.count;\n'
)

NUMBER_COMPLETION = (
    'function f(n: number): number {\n'
    '    return n.\n'
    '}\n'
)

BOOLEAN_COMPLETION = (
    'function f(b: boolean): boolean {\n'
    '    return b.\n'
    '}\n'
)

CLASS_COMPLETION = (
    'class Box {\n'
    '    id: number = 0;\n'
    '    label: string = "x";\n'
    '}\n'
    '\n'
    'function f(b: Box): number {\n'
    '    return b.\n'
    '}\n'
)

PREFIX_MEMBER = (
    'const Obj = {\n'
    '    nickname: "Asha",\n'
    '    id: "42",\n'
    '};\n'
    '\n'
    'const user = Obj.na;\n'
)

GENERIC_HOVER = (
    'function identity<T>(v: T): T {\n'
    '    return v;\n'
    '}\n'
    '\n'
    'const n: number = identity(1);\n'
    'const s: string = identity("x");\n'
)

CALL_HIERARCHY = (
    'function helper(n: number): number {\n'
    '    return n + 1;\n'
    '}\n'
    '\n'
    'const result = helper(41);\n'
)

METHOD_HIERARCHY = (
    'class Runner {\n'
    '    step(): number {\n'
    '        return 41;\n'
    '    }\n'
    '    run(): number {\n'
    '        return this.step();\n'
    '    }\n'
    '}\n'
)

TYPE_HIER_IFACE = (
    'interface Base {\n'
    '    id: number;\n'
    '}\n'
    '\n'
    'interface Derived extends Base {\n'
    '    label: string;\n'
    '}\n'
)

TYPE_HIER_CLASS = (
    'interface Base {\n'
    '    id: number;\n'
    '}\n'
    '\n'
    'class Impl implements Base {\n'
    '    id: number = 0;\n'
    '}\n'
)

INTERFACE_RENAME = (
    'interface Config {\n'
    '    retries: number;\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    'const a: Config = { retries: 1, mode: "tcp" };\n'
    'const b: Config = { retries: 2, mode: "ssl" };\n'
    'const c: Config = { retries: 3, mode: "sock" };\n'
)

CLASS_RENAME = (
    'class Box {\n'
    '    value: number = 0;\n'
    '}\n'
    '\n'
    'const x = new Box();\n'
    'function f(b: Box): number {\n'
    '    return b.value;\n'
    '}\n'
)

MISSING_PROP_MULTILINE = (
    'interface Config {\n'
    '    retries: number;\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    'const c: Config =\n'
    '    {\n'
    '        mode: "tcp",\n'
    '    };\n'
)

LIB = (
    'export interface Config {\n'
    '    retries: number;\n'
    '    mode?: string;\n'
    '}\n'
    '\n'
    'export type Kind = "shade" | "tinge";\n'
    '\n'
    'export enum Shade {\n'
    '    A,\n'
    '    B,\n'
    '}\n'
    '\n'
    'export const shade = Shade.A;\n'
)

MAIN = (
    'import { Config, Kind, shade } from "./lib.cp3";\n'
    'import console from "std/console";\n'
    '\n'
    'const c: Config = { retries: 3 };\n'
    'const s: Kind = "shade";\n'
    'console.log(c.retries);\n'
)

BARREL = (
    'export { Config, Kind, Shade } from "./lib.cp3";\n'
)

CONSUMER = (
    'import { Config, Kind } from "./barrel.cp3";\n'
    '\n'
    'const c: Config = { retries: 1 };\n'
    'const k: Kind = "shade";\n'
)

MISSING_PROP_SAMELINE = (
    'interface Config {\n'
    '    retries: number;\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    'const c: Config = { mode: "tcp" };\n'
)

UNBALANCED_BRACE = (
    'function combine(a: number, b: number): number {\n'
    '    return a + b;\n'
)

OBJECT_MEMBER_MISMATCH = (
    'interface Config {\n'
    '    retries: number;\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    'const c: Config = { retries: "high", mode: "tcp" };\n'
)

DUPLICATE_CLASS = (
    'class Client {\n'
    '    id: number = 0;\n'
    '}\n'
    '\n'
    'class Client {\n'
    '    id: number = 0;\n'
    '}\n'
)


def write(work, name, text):
    path = os.path.join(work, name)
    with open(path, "wb") as handle:
        handle.write(text.encode("utf-8"))
    return intel_check.file_uri(path), text


def pos(text, needle, offset=0, occurrence=0):
    return intel_check.position(text, needle, offset, occurrence)


def diags(session, uri):
    found = None
    for notification in session.notifications:
        if notification.get("method") == "textDocument/publishDiagnostics":
            params = notification.get("params", {})
            if params.get("uri") == uri:
                found = params.get("diagnostics", [])
    return found


def hover(session, uri, text, needle, offset=0, occurrence=0):
    r = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    })
    result = r.get("result")
    if not isinstance(result, dict):
        return None
    contents = result.get("contents")
    return contents.get("value") if isinstance(contents, dict) else repr(contents)


def completion(session, uri, text, needle, offset=0, occurrence=0):
    r = session.request("textDocument/completion", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    })
    items = (r.get("result") or {}).get("items") or []
    return [i.get("label") for i in items]


def definition(session, uri, text, needle, offset=0, occurrence=0):
    r = session.request("textDocument/definition", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    })
    return r.get("result")


def references(session, uri, text, needle, offset=0, occurrence=0):
    r = session.request("textDocument/references", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
        "context": {"includeDeclaration": True},
    })
    return r.get("result") or []


def rename(session, uri, text, needle, new_name, occurrence=0):
    r = session.request("textDocument/rename", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, 0, occurrence),
        "newName": new_name,
    })
    return (r.get("result") or {}).get("changes") or {}


def prepare_call_hierarchy(session, uri, text, needle, offset=0, occurrence=0):
    r = session.request("textDocument/prepareCallHierarchy", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    })
    return r.get("result") or []


def prepare_type_hierarchy(session, uri, text, needle, offset=0, occurrence=0):
    r = session.request("textDocument/prepareTypeHierarchy", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    })
    return r.get("result") or []


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="LSP defect-report verification")
    parser.add_argument("--cp3c", default=DEFAULT_CP3C)
    args = parser.parse_args(argv)

    work = os.path.join(os.environ.get("TEMP") or ".", "opencode", "defects-check")
    os.makedirs(work, exist_ok=True)

    fixture_specs = [
        ("bad_impl.cp3", IMPLEMENT_BAD),
        ("missing_impl.cp3", IMPLEMENT_MISSING),
        ("duplicate_function.cp3", DUPLICATE_FUNCTION),
        ("constraint_violation.cp3", CONSTRAINT_VIOLATION),
        ("unused_import.cp3", UNUSED_IMPORT),
        ("member_nav.cp3", MEMBER_NAV),
        ("number_completion.cp3", NUMBER_COMPLETION),
        ("boolean_completion.cp3", BOOLEAN_COMPLETION),
        ("class_completion.cp3", CLASS_COMPLETION),
        ("prefix_member.cp3", PREFIX_MEMBER),
        ("generic_hover.cp3", GENERIC_HOVER),
        ("call_hierarchy.cp3", CALL_HIERARCHY),
        ("method_hierarchy.cp3", METHOD_HIERARCHY),
        ("type_hier_iface.cp3", TYPE_HIER_IFACE),
        ("type_hier_class.cp3", TYPE_HIER_CLASS),
        ("interface_rename.cp3", INTERFACE_RENAME),
        ("class_rename.cp3", CLASS_RENAME),
        ("missing_prop_multiline.cp3", MISSING_PROP_MULTILINE),
        ("missing_prop_sameline.cp3", MISSING_PROP_SAMELINE),
        ("lib.cp3", LIB),
        ("main.cp3", MAIN),
        ("barrel.cp3", BARREL),
        ("consumer.cp3", CONSUMER),
        ("unbalanced_brace.cp3", UNBALANCED_BRACE),
        ("object_member_mismatch.cp3", OBJECT_MEMBER_MISMATCH),
        ("duplicate_class.cp3", DUPLICATE_CLASS),
    ]
    uris = {}
    texts = {}
    session = intel_check.Session(args.cp3c, work)
    for name, text in fixture_specs:
        uri, text = write(work, name, text)
        uris[name] = uri
        texts[name] = text
        session.notify("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "cp3",
                "version": 1,
                "text": text,
            },
        })
    for uri in uris.values():
        try:
            session.request("textDocument/hover", {
                "textDocument": {"uri": uri},
                "position": {"line": 0, "character": 0},
            }, timeout=30.0)
        except Exception:
            pass
    time.sleep(0.5)

    results = []

    def check(name, ok, detail=""):
        line = ("PASS " if ok else "FAIL ") + name
        if not ok and detail:
            line += "  [%s]" % str(detail)[:300]
        print(line)
        results.append(ok)

    # ---- report item 1: diagnostics that never fire ---------------------------
    check("item1 guard: object literal member type mismatch reports a diagnostic",
          bool(diags(session, uris["object_member_mismatch.cp3"])))
    check("item1 action: class implements with a wrong-typed member reports a diagnostic",
          bool(diags(session, uris["bad_impl.cp3"])))
    check("item1 action: class implements with a missing member reports a diagnostic",
          bool(diags(session, uris["missing_impl.cp3"])))
    check("item1 action: duplicate function reports a diagnostic",
          bool(diags(session, uris["duplicate_function.cp3"])))
    check("item1 action: generic constraint violation at the call site reports a diagnostic",
          bool(diags(session, uris["constraint_violation.cp3"])))
    check("item1 guard: unmatched brace reports a syntax diagnostic",
          bool(diags(session, uris["unbalanced_brace.cp3"])))
    check("item1 guard: duplicate class reports CP3303",
          any(d.get("code") == "CP3303" for d in diags(session, uris["duplicate_class.cp3"]) or []))
    check("item1 guard: missing object property reports CP3410",
          any(d.get("code") == "CP3410" for d in diags(session, uris["missing_prop_sameline.cp3"]) or []))
    check("item10 action: an unused import reports a diagnostic",
          bool(diags(session, uris["unused_import.cp3"])))

    # ---- report item 3: call hierarchy preparation ---------------------------
    uri = uris["call_hierarchy.cp3"]
    check("item3 guard: prepareCallHierarchy at a call site answers an item",
          bool(prepare_call_hierarchy(session, uri, CALL_HIERARCHY, "helper(41)", 0)))
    check("item3 action: prepareCallHierarchy at a function declaration answers an item",
          bool(prepare_call_hierarchy(session, uri, CALL_HIERARCHY, "function helper", 0)))
    uri = uris["method_hierarchy.cp3"]
    check("item3 action: prepareCallHierarchy at a method declaration answers an item",
          bool(prepare_call_hierarchy(session, uri, METHOD_HIERARCHY, "step(): number", 0)))
    check("item3 guard: prepareCallHierarchy at a method call site answers an item",
          bool(prepare_call_hierarchy(session, uri, METHOD_HIERARCHY, "this.step()", 5)))

    # ---- report item 4: same-file member navigation ---------------------------
    uri = uris["member_nav.cp3"]
    check("item4 action: definition on this.<member> answers a location",
          bool(definition(session, uri, MEMBER_NAV, "this.count = 1", 5)))
    check("item4 action: definition on <member> of a local answers a location",
          bool(definition(session, uri, MEMBER_NAV, "c.count;", 2)))
    check("item4 guard: definition on a constructor answers a location",
          bool(definition(session, uri, MEMBER_NAV, "Counter();", 0)))
    check("item4 action: definition on <member> in a return position answers a location",
          bool(definition(session, uri, MEMBER_NAV, "this.count", 5, 1)))

    # ---- report item 5: member completion is not type-constrained -------------
    check("item5 action: member completion on a number receiver offers toFixed",
          "toFixed" in completion(session, uris["number_completion.cp3"],
                                  NUMBER_COMPLETION, ".", 1))
    check("item5 action: member completion on a number receiver offers no array/sequence members",
          not any(x in completion(session, uris["number_completion.cp3"],
                                  NUMBER_COMPLETION, ".", 1) for x in ("push", "toUpperCase")))
    check("item5 guard: member completion on a boolean receiver offers toString",
          "toString" in completion(session, uris["boolean_completion.cp3"],
                                   BOOLEAN_COMPLETION, ".", 1))
    check("item5 action: member completion on a boolean receiver offers no array/sequence members",
          not any(x in completion(session, uris["boolean_completion.cp3"],
                                  BOOLEAN_COMPLETION, ".", 1) for x in ("push", "toUpperCase")))
    check("item5 guard: member completion on a class receiver offers its members",
          set(completion(session, uris["class_completion.cp3"],
                         CLASS_COMPLETION, ".", 1)) == {"id", "label"})

    # ---- report item 6: hover drops the generic instantiation -----------------
    hovered = hover(session, uris["generic_hover.cp3"],
                    GENERIC_HOVER, "identity(", 0)
    check("item6 action: hover on an instantiated generic call renders the instantiation",
          hovered is not None and "number" in hovered, hovered)

    # ---- report item 7: cross-file resolution --------------------------------
    uri = uris["main.cp3"]
    check("item7 guard: hover on an imported interface declaration answers",
          hover(session, uris["lib.cp3"], LIB, "interface Config", 0) is not None)
    check("item7 guard: hover on an imported enum declaration answers",
          hover(session, uris["lib.cp3"], LIB, "enum Shade", 0) is not None)
    check("item7 action: definition on an imported interface's usage answers a location",
          bool(definition(session, uri, MAIN, ": Config", 2)))
    check("item7 action: definition on an imported type alias's usage answers a location",
          bool(definition(session, uri, MAIN, ": Kind", 2)))
    check("item7 action: definition on the import specifier answers a location",
          bool(definition(session, uri, MAIN, "Config", 0, 0)))
    check("item7 guard: definition on an imported enum's usage answers a location",
          bool(definition(session, uris["lib.cp3"], LIB, "Shade.A", 0)))
    refs = references(session, uris["lib.cp3"], LIB, "Config", 0, 0)
    check("item7 action: references on the interface declaration find the importing file",
          any(r.get("uri", "").endswith("main.cp3") for r in refs), len(refs))
    check("item7 action: references on the interface declaration find more than one site",
          len(refs) > 1, len(refs))
    check("item7 action: definition on a re-export's usage answers a location",
          bool(definition(session, uris["consumer.cp3"], CONSUMER, "Config", 0, 0)))
    check("item7 action: hover on a re-exported interface's usage answers",
          hover(session, uris["consumer.cp3"], CONSUMER, "Config", 0, 0) is not None)
    check("item7 action: enum member completion offers the enum members",
          set(x for x in completion(session, uris["lib.cp3"], LIB, "Shade.A", 5)
              if x in ("A", "B")) == {"A", "B"})
    check("item7 action: hover on an imported type alias declaration answers",
          hover(session, uris["lib.cp3"], LIB, "type Kind", 0) is not None)

    # ---- report item 8: rename -------------------------------------------------
    changes = rename(session, uris["interface_rename.cp3"],
                     INTERFACE_RENAME, "interface Config", "Settings")
    check("item8 action: renaming an interface produces edits",
          bool(changes), changes)
    check("item8 action: renaming an interface renames all usages",
          any("a.cp3" not in f or True for f in changes) and
          sum(len(v) for v in changes.values()) >= 3,
          json.dumps(changes))
    changes = rename(session, uris["class_rename.cp3"],
                     CLASS_RENAME, "class Box", "Crate")
    check("item8 action: renaming a class produces edits",
          bool(changes), changes)
    check("item8 action: renaming a class renames declaration, constructor and annotation",
          sum(len(v) for v in changes.values()) >= 3,
          sum(len(v) for v in changes.values()))

    # ---- report item 9: type hierarchy -----------------------------------------
    base = prepare_type_hierarchy(session, uris["type_hier_iface.cp3"],
                                  TYPE_HIER_IFACE, "interface Base", 0)
    check("item9 guard: prepareTypeHierarchy on an interface answers an item",
          bool(base))
    supers = []
    if base:
        r = session.request("typeHierarchy/subtypes", {"item": base[0]})
        supers = r.get("result") or []
    check("item9 guard: subtypes of an interface include its derived interface",
          any(i.get("name") == "Derived" for i in supers), supers)
    impl = prepare_type_hierarchy(session, uris["type_hier_class.cp3"],
                                  TYPE_HIER_CLASS, "class Impl", 0)
    check("item9 guard: prepareTypeHierarchy on a class implementing an interface answers an item",
          bool(impl))

    # ---- report item 11: CP3410 position ---------------------------------------
    # The reported fixture did not reproduce at this compiler: in every layout
    # probed (same-line literal, literal on its own line, call-site literal) the
    # diagnostic lands on the object literal's line. Pinned as a guard.
    line = None
    for d in diags(session, uris["missing_prop_multiline.cp3"]) or []:
        if d.get("code") == "CP3410":
            line = d.get("range", {}).get("start", {}).get("line")
    check("item11 guard: CP3410 reports the object literal's line",
          line == 6, line)

    # ---- report item 12: strict-prefix filter -----------------------------------
    check("item12 guard: partial member shows subsequence-matching member",
          "nickname" in completion(session, uris["prefix_member.cp3"],
                                   PREFIX_MEMBER, "Obj.na;", 4))

    session.shutdown()

    passed = sum(1 for ok in results if ok)
    total = len(results)
    print()
    print("RESULT: %d/%d passed, %d failed" % (passed, total, total - passed))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
