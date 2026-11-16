#!/usr/bin/env python3
# =============================================================================
# tests/lsp/generic_intel_check.py - LSP pins for Family F: generic
# instantiation/inference intel is missing (12 checks).
#
# Every check states the DESIRED behaviour. Checks that fail today are open
# compiler defects, marked with the F- item they pin. Checks that pass today
# are regression guards. RED at HEAD is allowed and expected for most of the
# hover pins; nothing here is weakened to look green.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party
# packages) exactly the way tests/lsp/intel_check.py and
# tests/lsp/defects_check.py do (it imports their Session driver), prints one
# machine-checkable `PASS <id> <desc>` / `FAIL <id> <desc>` line per check and
# exits 1 when any check fails. Output is ASCII only; fixture identifiers are
# English. Fixtures live in a fresh
#   %TEMP%\opencode\generic-intel-check-<rand>
# directory on every run; every request rides the harness default timeouts.
# This script writes nothing inside the repo (no evidence directory).
#
# The pins probe ONE workspace document. Adaptations from the report's
# motivating code, documented here:
#   * cp3 has no intersection type, so `(...) as unknown as Named & Sized` is
#     expressed as `interface NamedSized extends Named, Sized { }` (interfaces
#     may name multiple base extends types, which is supported) with a single
#     object-literal initializer. The pins check member access on a receiver
#     that carries both members, which is the report's intent.
#   * The union-arm sub-question in the report ("does not offer members that
#     belong to only one arm incorrectly?") is NOT pinned: the report hedges
#     it with a question mark, and for `Named | Sized` neither `name` nor
#     `size` is valid on the union as a whole, so no desired pin can be
#     written without a rule the report does not settle.
#   * F-10 pins HOVER at `this.value` inside a generic class's method body
#     (deterministic: the body is checked under the class's own type
#     parameter, so it renders `T`). The report offers either that or
#     per-instantiation substitution for public bodies; substitution is only
#     meaningful at instantiation sites, which F-01..F-05 pin instead. The
#     completion variant of the same bullet (this.value inside the body) is
#     subsumed by the hover pin.
#   * F-06 hovers the call expression `identity<number>(3)` itself, the way
#     tests/lsp/defects_check.py item6 hovers the callee token; the
#     parenthetical "(the call result)" in the report is the same token.
#
# Dropped pins (out of harness reach / not determinable from one document):
#   * The union-arm question above.
#   * Per-public-instantiation substitution of `this.value` inside the class
#     body (the class is checked generically; there is no per-instantiation
#     body to see from one document).
# Usage (from the repo root):
#   python tests\lsp\generic_intel_check.py
# or: python tests\lsp\generic_intel_check.py --cp3c build\windows-release\bin\cp3c.exe
# =============================================================================
import argparse
import os
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")


# -----------------------------------------------------------------------------
# The single workspace document every check probes.
#
# The trailing `probe...` lines are unfinished on purpose: completion is
# requested right after the dangling dot / type-argument bracket. ASCII only,
# so byte, UTF-16 and character offsets coincide.
# -----------------------------------------------------------------------------

FIXTURE = (
    'import Math from "std/math";\n'
    '\n'
    'class Box<T> {\n'
    '  value: T = 0 as T;\n'
    '  function get(): T {\n'
    '    return this.value;\n'
    '  }\n'
    '}\n'
    '\n'
    'const bs = new Box<string>();\n'
    'const bn = new Box<number>();\n'
    'const vs = bs.value;\n'
    'const vn = bn.value;\n'
    'const direct = new Box<string>().value;\n'
    'const used = Math.abs(-1);\n'
    '\n'
    'function identity<T>(x: T): T {\n'
    '  return x;\n'
    '}\n'
    '\n'
    'const n = identity<number>(3);\n'
    '\n'
    'interface Named {\n'
    '  name: string;\n'
    '}\n'
    '\n'
    'interface Sized {\n'
    '  size: number;\n'
    '}\n'
    '\n'
    'interface NamedSized extends Named, Sized {\n'
    '}\n'
    '\n'
    'const both: NamedSized = { name: "a", size: 1 };\n'
    'const bothName = both.name;\n'
    'const bothSize = both.size;\n'
    '\n'
    'function first<T extends Named>(x: T): string {\n'
    '  return x.name;\n'
    '}\n'
    '\n'
    'const firstName = first<Named>({ name: "x" });\n'
    '\n'
    'const probeBs = bs.\n'
    'const probeBn = bn.\n'
    'const probeBoth = both.\n'
    'const probeIdentity = identity<\n'
)

FIXTURE_NAME = "generic_intel_main.cp3"


def fresh_work_dir():
    base = (os.environ.get("TEMP") or os.environ.get("TMP")
            or os.environ.get("TMPDIR") or "/tmp")
    root = os.path.join(base, "opencode")
    os.makedirs(root, exist_ok=True)
    path = os.path.join(root, "generic-intel-check-%s" % uuid.uuid4().hex[:8])
    os.makedirs(path)
    return path


def pos(text, needle, offset=0, occurrence=0):
    return intel_check.position(text, needle, offset, occurrence)


def hover(session, uri, text, needle, offset=0, occurrence=0):
    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    })
    result = response.get("result")
    if not isinstance(result, dict):
        return None
    contents = result.get("contents")
    if not isinstance(contents, dict):
        return repr(contents)
    value = contents.get("value")
    return value if isinstance(value, str) else repr(value)


def completion_labels(session, uri, text, needle, offset=0):
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset),
    })
    result = response.get("result")
    if not isinstance(result, dict):
        return None
    items = result.get("items")
    if not isinstance(items, list):
        return None
    return [item.get("label", "") for item in items]


def ascii_safe(value):
    return str(value).encode("ascii", "replace").decode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp3c", default=DEFAULT_CP3C, help="path to cp3c.exe")
    args = parser.parse_args()

    cp3c = os.path.abspath(args.cp3c)
    work = fresh_work_dir()
    fixture_path = os.path.join(work, FIXTURE_NAME)
    with open(fixture_path, "wb") as handle:
        handle.write(FIXTURE.encode("utf-8"))
    uri = intel_check.file_uri(fixture_path)

    print("cp3c: %s" % ascii_safe(cp3c))
    print("fixtures: %s" % ascii_safe(work))

    state = {"passed": 0, "failed": 0, "done": set()}
    pin_order = ["F-%02d" % i for i in range(1, 13)]

    def check(check_id, desc, ok, detail=""):
        if ok:
            state["passed"] += 1
            line = "PASS %s %s" % (check_id, desc)
        else:
            state["failed"] += 1
            line = "FAIL %s %s" % (check_id, desc)
            if detail:
                line += "  [%s]" % ascii_safe(detail)[:300]
        print(line)
        state["done"].add(check_id)

    session = intel_check.Session(cp3c, work)
    try:
        session.request("initialize", {
            "processId": None,
            "rootUri": intel_check.file_uri(work),
            "capabilities": {},
        })
        session.notify("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "cp3",
                "version": 1,
                "text": FIXTURE,
            },
        })
        # Barrier: lets the coalesced diagnostics drain before the checks.
        session.request("textDocument/hover", {
            "textDocument": {"uri": uri},
            "position": {"line": 0, "character": 0},
        }, timeout=30.0)

        # F-01: hover on `value` inside a new-expression member access renders
        # the instantiated member (`value: string`), not `value: T`.
        value = hover(session, uri, FIXTURE, "new Box<string>().value",
                      len("new Box<string>()."))
        check("F-01",
              "hover on `new Box<string>().value` renders the instantiated member",
              value is not None and "value: string" in value, value)

        # F-02: hover on `bs.value` renders `string` (Box<string>).
        value = hover(session, uri, FIXTURE, "bs.value", len("bs."))
        check("F-02",
              "hover on `bs.value` (Box<string>) renders `value: string`",
              value is not None and "value: string" in value, value)

        # F-03: hover on `bn.value` renders `number` (Box<number>) and does
        # not leak the Box<string> instantiation (isolation of instantiations
        # of the same generic).
        value = hover(session, uri, FIXTURE, "bn.value", len("bn."))
        check("F-03",
              "hover on `bn.value` (Box<number>) renders `value: number`",
              value is not None and "value: number" in value, value)

        # F-04: completion at `bs.` offers the instantiated member `value`.
        found = completion_labels(session, uri, FIXTURE, "probeBs = bs.",
                                  len("probeBs = bs."))
        check("F-04",
              "completion at `bs.` offers `value`",
              found is not None and "value" in found, found)

        # F-05: completion at `bn.` offers the instantiated member `value`.
        found = completion_labels(session, uri, FIXTURE, "probeBn = bn.",
                                  len("probeBn = bn."))
        check("F-05",
              "completion at `bn.` offers `value`",
              found is not None and "value" in found, found)

        # F-06: hover on the call `identity<number>(3)` renders the
        # instantiation (`number`), not `T`.
        value = hover(session, uri, FIXTURE, "identity<number>(3)")
        check("F-06",
              "hover on `identity<number>(3)` renders the instantiation",
              value is not None and ": number" in value, value)

        # F-07: completion inside a generic function's type-argument position
        # offers the primitive types and never the callee's own type
        # parameter name `T` as an instantiable choice.
        found = completion_labels(session, uri, FIXTURE,
                                  "probeIdentity = identity<",
                                  len("probeIdentity = identity<"))
        check("F-07",
              "completion at `identity<|>` offers number/string/boolean, not `T`",
              found is not None
              and all(name in found for name in ("number", "string", "boolean"))
              and "T" not in found,
              found)

        # F-08: completion at `both.` offers the receiver's declared members
        # and no foreign members (no Math or string-surface entries leak in).
        found = completion_labels(session, uri, FIXTURE, "probeBoth = both.",
                                  len("probeBoth = both."))
        check("F-08",
              "completion at `both.` offers name/size and no foreign member",
              found is not None
              and "name" in found and "size" in found
              and not any(name in found
                          for name in ("abs", "sin", "join", "toUpperCase")),
              found)

        # F-09: hover at `both.name` renders string, hover at `both.size`
        # renders number.
        name_value = hover(session, uri, FIXTURE, "both.name", len("both."))
        size_value = hover(session, uri, FIXTURE, "both.size", len("both."))
        check("F-09",
              "hover at `both.name` renders string and `both.size` renders number",
              name_value is not None and "name: string" in name_value
              and size_value is not None and "size: number" in size_value,
              "name hover: %r / size hover: %r" % (name_value, size_value))

        # F-10: hover on `this.value` inside the generic method body renders
        # `T`. Documented choice: the body is checked under the class's own
        # type parameters; substitution is pinned at the instantiation sites
        # by F-01..F-05.
        value = hover(session, uri, FIXTURE, "return this.value;",
                      len("return this."))
        check("F-10",
              "hover at `this.value` inside the generic body renders `T`",
              value is not None and ": T" in value, value)

        # F-11: hover at `x.name` in `first<T extends Named>` renders string
        # (the constraint drives member-type intel).
        value = hover(session, uri, FIXTURE, "return x.name;", len("return x."))
        check("F-11",
              "hover at `x.name` in a constrained generic renders `string`",
              value is not None and "name: string" in value, value)

        # F-12: hover on `new Box<string>()` renders the class with the
        # instantiation (`Box<string>`), not bare `Box`.
        value = hover(session, uri, FIXTURE, "new Box<string>()",
                      len("new "))
        check("F-12",
              "hover on `new Box<string>()` renders `Box<string>`",
              value is not None and "Box<string>" in value, value)

        shutdown, exit_code = session.shutdown()
        # The shutdown is not one of the 12 pinned checks; it is best effort.
        _ = shutdown, exit_code
    except Exception as exc:
        for remaining in pin_order:
            if remaining not in state["done"]:
                check(remaining,
                      "not completed: driver exception", False, repr(exc))
        try:
            session.proc.kill()
        except Exception:
            pass

    summary = "RESULT: %d/12 passed" % state["passed"]
    print(summary)
    return 0 if state["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
