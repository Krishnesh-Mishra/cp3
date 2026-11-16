#!/usr/bin/env python3
# =============================================================================
# tests/lsp/cross_module_check.py - verification suite for the cross-module
# defect families (A, D and E) pinned from the defects report. Each check
# states the DESIRED behaviour; checks that fail at this build are open
# compiler defects, marked with the family and item they pin. Checks that
# pass are regression guards. Do not weaken the pins to make this file pass.
#
#   Family A (items A-01..A-16) - cross-module resolution through barrels,
#     re-exports, aliases and default exports. gotoDef / references / rename /
#     hover at the use site should land on the underlying declaration in the
#     source module even when the import chain passes through
#     `export { x as y } from`, `export * from`, a mixed plain/aliased barrel,
#     or an `export default` re-export.
#   Family D (items D-01..D-11) - use-site navigation across modules (the
#     companion-file family): a sibling module exports a class, an interface,
#     a type alias, an enum and a default export; the importing file must be
#     able to jump to, hover on, find references to and rename those exports,
#     including member access on receivers typed by imported declarations.
#   Family E (item E-01) - hover on an imported const reports nothing; the
#     hover must render the const's type and value information.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party
# packages) and prints one machine-checkable PASS / FAIL line per check.
# Exit code 0 when every check passes, 1 otherwise. The LSP is driven exactly
# the way tests/lsp/defects_check.py drives it (imports its stdio driver from
# tests/lsp/intel_check.py). Fixtures are written into a fresh
# TEMP/opencode/cross-module-check-<pid> directory under $TEMP.
#
# Item ids: A-01..A-16 (family A), D-01..D-11 (family D), E-01 (family E).
# Checks that likely already pass at this build: A-06 (definition on a direct
# local import) and possibly D-08 (hover on a plain local variable); the rest
# pin the failing families. Families A and D are known-failing end to end at
# HEAD for the cross-module legs; family E is pinned as failing by design.
# Every check prints a short label; the detail after FAIL prints the actual
# location / count information observed.
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

# --- Family A fixtures: a.cp3 is the source module; the barrels and the ----
# --- consumers re-export and import through the alias chains. ---------------
A_COMBINE = (
    '/** Combines the given numbers into one. */\n'
    'export function combine(xs: number[]): number {\n'
    '    return xs.length;\n'
    '}\n'
    '\n'
    '/** A box that holds a number. */\n'
    'export class Box {\n'
    '    /** The boxed value. */\n'
    '    value: number = 0;\n'
    '}\n'
    '\n'
    '/** The answer to everything. */\n'
    'export const ANSWER = 42;\n'
)

BARREL = (
    'export { combine as merge } from "./a.cp3";\n'
    'export { Box as Crate } from "./a.cp3";\n'
    'export { ANSWER as LIMIT } from "./a.cp3";\n'
)

CONSUMER = (
    'import { merge, Crate, LIMIT } from "./barrel.cp3";\n'
    '\n'
    'const s = merge([1, 2]);\n'
    'const box = new Crate();\n'
    'const limit = LIMIT;\n'
)

BARREL_STAR = (
    'export * from "./a.cp3";\n'
)

CONSUMER_STAR = (
    'import { combine } from "./barrel_star.cp3";\n'
    '\n'
    'const starTotal = combine([1, 2]);\n'
)

DIRECT_CONSUMER = (
    'import { combine } from "./a.cp3";\n'
    '\n'
    'const directTotal = combine([4, 5]);\n'
)

A_DEFAULT = (
    '/** Doubles the given number. */\n'
    'export default function doubleIt(n: number): number {\n'
    '    return n * 2;\n'
    '}\n'
)

BARREL_DEFAULT = (
    'import doubleIt from "./a_default.cp3";\n'
    '\n'
    'export default doubleIt;\n'
)

CONSUMER_DEFAULT = (
    'import doubleIt from "./barrel_default.cp3";\n'
    '\n'
    'const doubled = doubleIt(21);\n'
)

# The mixed-style barrel: a plain (not aliased) re-export next to an aliased
# one from the same source module; the four navigation verbs below target the
# plain `combine` re-export.
MIXED_BARREL = (
    'export { combine } from "./a.cp3";\n'
    'export { Box as Crate } from "./a.cp3";\n'
)

MIXED_CONSUMER = (
    'import { combine } from "./mixed_barrel.cp3";\n'
    '\n'
    'const mixedTotal = combine([1, 2, 3]);\n'
)

# --- Family D fixtures: two sibling companion modules and one consumer. -----
COMPANION = (
    '/** A client bound to a configuration. */\n'
    'export class Client {\n'
    '    /** The client identifier. */\n'
    '    id: number = 0;\n'
    '}\n'
    '\n'
    '/** Runtime configuration for the client. */\n'
    'export interface Config {\n'
    '    /** Maximum number of retry attempts. */\n'
    '    retries: number;\n'
    '    /** The wire mode. */\n'
    '    mode: string;\n'
    '}\n'
    '\n'
    '/** The kind of shade to use. */\n'
    'export type Kind = "shade" | "tinge";\n'
    '\n'
    '/** A named shade. */\n'
    'export enum Shade {\n'
    '    A,\n'
    '    B,\n'
    '}\n'
)

SECOND = (
    '/** A ledger of entries. */\n'
    'export class Ledger {\n'
    '    /** The number of entries recorded. */\n'
    '    total: number = 0;\n'
    '}\n'
    '\n'
    '/** Creates a fresh ledger. */\n'
    'export default function createLedger(): Ledger {\n'
    '    return new Ledger();\n'
    '}\n'
)

D_CONSUMER = (
    'import { Client, Config, Kind, Shade } from "./companion.cp3";\n'
    'import createLedger, { Ledger } from "./second.cp3";\n'
    '\n'
    'const client: Client = new Client();\n'
    'const config: Config = { retries: 3, mode: "tcp" };\n'
    'const kind: Kind = "shade";\n'
    'const shade: Shade = Shade.A;\n'
    'const retries = config.retries;\n'
    'const created = createLedger();\n'
    'const ledger = new Ledger();\n'
    'const recorded = ledger.total;\n'
)

# --- Family E fixtures: hover on an imported const. --------------------------
EXPORTER = (
    'export const LIMIT = 42;\n'
)

E_CONSUMER = (
    'import { LIMIT } from "./exporter.cp3";\n'
    'const x = LIMIT + 1;\n'
)


def write(work, name, text):
    path = os.path.join(work, name)
    with open(path, "wb") as handle:
        handle.write(text.encode("utf-8"))
    return intel_check.file_uri(path), text


def pos(text, needle, offset=0, occurrence=0):
    return intel_check.position(text, needle, offset, occurrence)


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


def rename(session, uri, text, needle, new_name, offset=0, occurrence=0):
    r = session.request("textDocument/rename", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
        "newName": new_name,
    })
    return (r.get("result") or {}).get("changes") or {}


def uri_base(uri):
    if not isinstance(uri, str):
        return ""
    return uri.replace("\\", "/").rsplit("/", 1)[-1].lower()


def locations(result):
    # textDocument/definition may answer Location, Location[], or null.
    if isinstance(result, dict):
        return [result]
    if isinstance(result, list):
        return [item for item in result if isinstance(item, dict)]
    return []


def def_in(result, name):
    return any(uri_base(loc.get("uri", "")) == name for loc in locations(result))


def def_start(result):
    for loc in locations(result):
        start = (loc.get("range") or {}).get("start")
        if isinstance(start, dict):
            return start
    return None


def start_eq(start, expected):
    # Compare by line/character only: the server's Position objects might
    # carry extra keys, and the wanted fact is the exact name offset.
    if not isinstance(start, dict):
        return False
    return (start.get("line"), start.get("character")) == (
        expected.get("line"), expected.get("character"))


def refs_in(refs, name):
    return any(uri_base(ref.get("uri", "")) == name for ref in refs or [])


def refs_count_in(refs, name):
    return sum(1 for ref in refs or []
               if uri_base(ref.get("uri", "")) == name)


def rename_counts(changes):
    counts = {}
    if not isinstance(changes, dict):
        return counts
    for uri, edits_list in changes.items():
        counts[uri_base(uri)] = len(edits_list or [])
    return counts


def counts_detail(changes):
    counts = rename_counts(changes)
    return "files=%s total=%d" % (json.dumps(counts, sort_keys=True),
                                 sum(counts.values()))


def def_nav(session, uri, text, needle, offset, occurrence, sibling):
    definition_result = definition(session, uri, text, needle, offset, occurrence)
    refs = references(session, uri, text, needle, offset, occurrence)
    def_ok = def_in(definition_result, sibling)
    refs_ok = refs_in(refs, sibling)
    detail = ("def_locs=%d def_in_%s=%s refs=%d refs_in_%s=%d"
              % (len(locations(definition_result)), sibling, def_ok,
                 len(refs or []), sibling, refs_count_in(refs, sibling)))
    return def_ok and refs_ok, detail


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="LSP cross-module verification")
    parser.add_argument("--cp3c", default=DEFAULT_CP3C)
    args = parser.parse_args(argv)

    work = os.path.join(os.environ.get("TEMP") or ".", "opencode",
                        "cross-module-check-%d" % os.getpid())
    os.makedirs(work, exist_ok=True)

    fixture_specs = [
        ("a.cp3", A_COMBINE),
        ("barrel.cp3", BARREL),
        ("consumer.cp3", CONSUMER),
        ("barrel_star.cp3", BARREL_STAR),
        ("consumer_star.cp3", CONSUMER_STAR),
        ("direct_consumer.cp3", DIRECT_CONSUMER),
        ("a_default.cp3", A_DEFAULT),
        ("barrel_default.cp3", BARREL_DEFAULT),
        ("consumer_default.cp3", CONSUMER_DEFAULT),
        ("mixed_barrel.cp3", MIXED_BARREL),
        ("mixed_consumer.cp3", MIXED_CONSUMER),
        ("companion.cp3", COMPANION),
        ("second.cp3", SECOND),
        ("consumer_d.cp3", D_CONSUMER),
        ("exporter.cp3", EXPORTER),
        ("consumer_e.cp3", E_CONSUMER),
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

    # ---- Family A: aliased named re-exports (items A-01..A-03, A-07..A-09) ---
    uri = uris["consumer.cp3"]
    text = texts["consumer.cp3"]
    combine_decl = pos(A_COMBINE, "function combine", 9)
    box_decl = pos(A_COMBINE, "class Box", 6)
    answer_decl = pos(A_COMBINE, "ANSWER", 0)

    found = definition(session, uri, text, "merge", 0, 1)
    check("A-01: definition on the aliased re-export of a function lands in a.cp3",
          def_in(found, "a.cp3") and start_eq(def_start(found), combine_decl),
          "locs=%s start=%s" % (locations(found), def_start(found)))
    found = definition(session, uri, text, "Crate", 0, 1)
    check("A-02: definition on the aliased re-export of a class lands in a.cp3",
          def_in(found, "a.cp3") and start_eq(def_start(found), box_decl),
          "locs=%s start=%s" % (locations(found), def_start(found)))
    found = definition(session, uri, text, "LIMIT", 0, 1)
    check("A-03: definition on the aliased re-export of a const lands in a.cp3",
          def_in(found, "a.cp3") and start_eq(def_start(found), answer_decl),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    # ---- Family A: export * and export default re-exports (A-04, A-05) ------
    found = definition(session, uris["consumer_star.cp3"], texts["consumer_star.cp3"],
                       "combine", 0, 1)
    check("A-04: definition through an export * re-export lands in a.cp3",
          def_in(found, "a.cp3") and start_eq(def_start(found), combine_decl),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    found = definition(session, uris["consumer_default.cp3"], texts["consumer_default.cp3"],
                       "doubleIt", 0, 1)
    check("A-05: definition on the default re-export imported from a barrel lands in a_default.cp3",
          def_in(found, "a_default.cp3")
          and start_eq(def_start(found), pos(A_DEFAULT, "function doubleIt", 9)),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    # ---- Family A: direct local import (item A-06; likely already works) -----
    found = definition(session, uris["direct_consumer.cp3"], texts["direct_consumer.cp3"],
                       "combine", 0, 1)
    check("A-06: definition on a direct ./a.cp3 import lands in a.cp3",
          def_in(found, "a.cp3") and start_eq(def_start(found), combine_decl),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    # ---- Family A: hover and references through the aliased barrel -----------
    hovered = hover(session, uri, text, "merge", 0, 1)
    check("A-07: hover at the re-exported use site renders the a.cp3 declaration docs",
          hovered is not None and "Combines" in hovered, hovered)

    refs = references(session, uris["barrel.cp3"], BARREL, "merge", 0, 0)
    check("A-08: references on the barrel re-export specifier find references in the consumer module",
          refs_in(refs, "consumer.cp3"),
          "refs=%d in_consumer=%d" % (len(refs or []), refs_count_in(refs, "consumer.cp3")))

    refs = references(session, uris["a.cp3"], A_COMBINE, "function combine", 9, 0)
    check("A-09: references on the original declaration find references through the barrel in the consumer module",
          refs_in(refs, "consumer.cp3"),
          "refs=%d in_consumer=%d" % (len(refs or []), refs_count_in(refs, "consumer.cp3")))

    # ---- Family A: the mixed-style barrel (plain named re-exports) -----------
    mixed_uri = uris["mixed_consumer.cp3"]
    mixed_text = texts["mixed_consumer.cp3"]
    found = definition(session, mixed_uri, mixed_text, "combine", 0, 1)
    check("A-10: definition through the mixed-style barrel lands in a.cp3",
          def_in(found, "a.cp3") and start_eq(def_start(found), combine_decl),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    refs = references(session, mixed_uri, mixed_text, "combine", 0, 1)
    ok = refs_in(refs, "a.cp3") and refs_in(refs, "mixed_consumer.cp3")
    check("A-11: references through the mixed-style barrel span a.cp3 and the consumer module",
          ok,
          "refs=%d in_a=%d in_consumer=%d" % (len(refs or []),
                                              refs_count_in(refs, "a.cp3"),
                                              refs_count_in(refs, "mixed_consumer.cp3")))

    changes = rename(session, mixed_uri, mixed_text, "combine", "sumAll", 0, 1)
    counts = rename_counts(changes)
    ok = (counts.get("a.cp3", 0) >= 1 and counts.get("mixed_barrel.cp3", 0) >= 1
          and counts.get("mixed_consumer.cp3", 0) >= 1)
    check("A-12: rename through the mixed-style barrel edits a.cp3, mixed_barrel.cp3 and the consumer module",
          ok, counts_detail(changes))

    hovered = hover(session, mixed_uri, mixed_text, "combine", 0, 1)
    check("A-13: hover through the mixed-style barrel renders the a.cp3 declaration docs",
          hovered is not None and "Combines" in hovered, hovered)

    # ---- Family A: rename at the aliased-import use site (A-14..A-16) ---------
    # One rename request; each check pins that the edits cover that file. The
    # task pins edits in all three files; the counts printed are honest about
    # how many locations were actually affected in each file.
    changes = rename(session, uri, text, "merge", "combineAll", 0, 1)
    counts = rename_counts(changes)
    detail = counts_detail(changes)
    check("A-14: rename at the aliased-import use site produces edits in consumer.cp3",
          counts.get("consumer.cp3", 0) >= 1, detail)
    check("A-15: rename at the aliased-import use site produces edits in barrel.cp3",
          counts.get("barrel.cp3", 0) >= 1, detail)
    check("A-16: rename at the aliased-import use site produces edits in a.cp3",
          counts.get("a.cp3", 0) >= 1, detail)

    # ---- Family D: navigation at imported sibling declarations (D-01..D-05) ---
    d_uri = uris["consumer_d.cp3"]
    d_text = texts["consumer_d.cp3"]

    ok, detail = def_nav(session, d_uri, d_text, "Client", 0, 1, "companion.cp3")
    check("D-01: definition and references at the imported class use site reach companion.cp3",
          ok, detail)
    ok, detail = def_nav(session, d_uri, d_text, "Config", 0, 1, "companion.cp3")
    check("D-02: definition and references at the imported interface use site reach companion.cp3",
          ok, detail)
    ok, detail = def_nav(session, d_uri, d_text, "Kind", 0, 1, "companion.cp3")
    check("D-03: definition and references at the imported type alias use site reach companion.cp3",
          ok, detail)
    ok, detail = def_nav(session, d_uri, d_text, "Shade", 0, 1, "companion.cp3")
    check("D-04: definition and references at the imported enum use site reach companion.cp3",
          ok, detail)
    ok, detail = def_nav(session, d_uri, d_text, "createLedger", 0, 1, "second.cp3")
    check("D-05: definition and references at the imported default export reach second.cp3",
          ok, detail)

    # ---- Family D: member access on the imported-typed receiver (D-06, D-11) --
    found = definition(session, d_uri, d_text, "retries", 0, 2)
    check("D-06: definition on the member read jumps to the Config field in companion.cp3",
          def_in(found, "companion.cp3")
          and start_eq(def_start(found), pos(COMPANION, "retries: number", 0)),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    # ---- Family D: hover on the sibling chain (D-07, D-08) -------------------
    hovered = hover(session, d_uri, d_text, "Config", 0, 0)
    check("D-07: hover on the import specifier renders the sibling declaration docs",
          hovered is not None and "Runtime configuration" in hovered, hovered)

    hovered = hover(session, d_uri, d_text, "const config", 6)
    check("D-08: hover on the imported-typed variable renders its type",
          hovered is not None and "Config" in hovered, hovered)

    # ---- Family D: references and rename on the sibling decl (D-09, D-10) -----
    refs = references(session, uris["companion.cp3"], COMPANION, "interface Config", 10, 0)
    check("D-09: references on the sibling declaration find the importing file",
          refs_in(refs, "consumer_d.cp3"),
          "refs=%d in_consumer=%d" % (len(refs or []), refs_count_in(refs, "consumer_d.cp3")))

    changes = rename(session, d_uri, d_text, "config.retries", "attempts", 7, 0)
    counts = rename_counts(changes)
    ok = counts.get("consumer_d.cp3", 0) >= 1 and counts.get("companion.cp3", 0) >= 1
    check("D-10: rename at the member use site edits the consumer and the sibling declaration",
          ok, counts_detail(changes))

    found = definition(session, d_uri, d_text, "ledger.total", 7, 0)
    check("D-11: definition on a member read of a different sibling import reaches second.cp3",
          def_in(found, "second.cp3")
          and start_eq(def_start(found), pos(SECOND, "total: number", 0)),
          "locs=%s start=%s" % (locations(found), def_start(found)))

    # ---- Family E: hover on an imported const (E-01) --------------------------
    hovered = hover(session, uris["consumer_e.cp3"], texts["consumer_e.cp3"], "LIMIT", 0, 1)
    ok = (hovered is not None
          and ("number" in hovered or "42" in hovered))
    check("E-01: hover on an imported const renders its type and value",
          ok, hovered)

    session.shutdown()

    passed = sum(1 for ok in results if ok)
    total = len(results)
    print()
    print("checks: %d" % total)
    print("RESULT: %d/%d passed, %d failed" % (passed, total, total - passed))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
