#!/usr/bin/env python3
# =============================================================================
# tests/lsp/hierarchy_parser_check.py - acceptance driver for the LSP
# hierarchy answers and for two parser error-expectation pins.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party
# packages) the same way tests/lsp/defects_check.py drives it (it imports the
# shared intel_check stdio driver) and prints one machine-checkable
# PASS / FAIL line per check. Exit code 0 when every check passes, 1 when any
# check fails.
#
# Coverage (11 checks, ids J-01..J-08, K-01, L-01, L-02):
#
#   Family J (call hierarchy, pins the "edges missing / caller naming wrong"
#   report): prepareCallHierarchy at the callee's or the caller's position,
#   then callHierarchy/incomingCalls and callHierarchy/outgoingCalls.
#     J-01  prepare at `helper` answers an item named `helper`
#     J-02  incoming calls for `helper` (called from `driver`) include the
#           `driver` caller entry
#     J-03  the incoming edge for `driver` carries BOTH call sites of
#           `driver`'s body (two distinct fromRanges, both on driver's line)
#     J-04  outgoing calls from `driver` include `helper`
#     J-05  method-to-method edge: incoming calls for `prepare` (private
#           method of Worker) include the `render` caller (this.prepare)
#     J-06  method-to-interface-typed-call edge: incoming calls for `handle`
#           (Echo implements Handler; poke calls this.handle()) include
#           `poke`
#     J-07  incoming calls for `layout` (the abstract Frame member,
#           overridden by Panel) include `Frame.draw` (draw calls
#           this.layout())
#     J-08  the caller entries' `name` in incoming results is reasonable
#           (not empty, not "unknown") for BOTH J-02 (driver) and J-05
#           (render)
#
#   Family K (type hierarchy at the implements site):
#     K-01  prepareTypeHierarchy at the `Sized` position in
#           `class Impl implements Sized { ... }` answers a NON-EMPTY items
#           list (the defect report says it answers nothing)
#
#   Family L (parser accepts illegal syntax silently; ERROR-EXPECTED pins):
#     L-01  `import {} from "std/math";` in a plain program produces a
#           diagnostic (an empty named-import specifier should not pass
#           silently). Expected RED at HEAD: the parser and sema both treat
#           the empty specifier list as empty.
#     L-02  `function f(): number { return 1\n+ 2; }` (binary continuation on
#           the next line after `return 1`, no semicolon) produces a
#           diagnostic. NOTE: this compiler binds binary operators across
#           newlines (parseAdditive does not gate on precededByNewline, and
#           consumeSemicolon applies ASI silently across the newline), so the
#           line is legal today and the continuation BINDS (the argument is
#           `1 + 2`). Per the task brief, the check therefore pins the ACTUAL
#           behavior (no error diagnostic) instead of forcing a red; a future
#           restricted ASI production would turn this pin red on purpose.
#
# Harness notes:
#   * The LSP is driven exactly the way tests/lsp/intel_check.py drives it
#     (this file imports intel_check's Session driver); defects_check does
#     the same.
#   * Family L is pinned through the `cp3c check` CLI subprocess (exit code +
#     the D2 machine line on stderr), not through the LSP, because the task
#     brief prefers the CLI relay for error-expected pins and the harness
#     supports subprocess spawning (intel_check spawns `cp3c fmt` / `cp3c
#     run` for its own command pins). The LSP didOpen + publishDiagnostics
#     path that intel_check/defects_check expose was deliberately not used
#     here; both L fixtures are also NOT opened in the LSP session so an
#     intentionally illegal file cannot perturb the J/K checks.
#   * J-01..J-04 prepare at declaration positions; J-05..J-07 prepare at the
#     CALL SITE (`this.prepare()` / `this.handle()` / `this.layout()`),
#     because prepare at a method DECLARATION name resolves to nothing at
#     HEAD (that is the defects-check item 3 pin, not a Family-J edge
#     defect). Edge matching at HEAD is by callee NAME, not by declaration
#     identity; J-07's `layout` has two declarations (Frame's abstract one
#     and Panel's override) and the pin accepts the single name match.
#   * fromRanges is hardcoded to an empty array at the LSP layer at HEAD
#     (src/lsp/src/intel.cpp, callHierarchyIncomingResult /
#     callHierarchyOutgoingResult), so J-03 is a red at HEAD by design.
#   * K-01 at HEAD is honestly uncertain without running the compiler: the
#     task brief records "the defect: returns nothing", while code reading of
#     src/cli/src/commands/lsp/intel/lsp_intel_hierarchy.cpp
#     (typeHierarchyPrepareAt, path (iii)) suggests the implements-clause
#     NamedType resolves via declarationNamed("Sized") to a non-empty item.
#     The pin pins the desired behavior either way and must not be weakened.
#   * Expected reds at HEAD (by code reading, not by execution): J-03 and
#     L-01; K-01 is the defect-report expectation but may already pass at
#     this build (defects-check documents item 9 as not reproducing here).
#
# Exit code contract: 0 only when every check passes, 1 otherwise. Every
# check prints one PASS / FAIL line; the last line is `RESULT: N/11 passed,
# K failed`.
#
# Usage (from the repo root):
#   python tests\lsp\hierarchy_parser_check.py --cp3c build\windows-release\bin\cp3c.exe
# =============================================================================

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import intel_check  # noqa: E402  (the LSP agent's shared stdio driver)

DEFAULT_CP3C = os.path.join(REPO, "build", "windows-release", "bin", "cp3c.exe")

# -----------------------------------------------------------------------------
# Fixtures (ASCII only: byte, UTF-16 and character offsets then coincide).
# -----------------------------------------------------------------------------

# Family J: every declaration the call-hierarchy pins need. One file; the
# analysis is whole-program anyway and the pins only reference same-file
# callables.
J_CALLS = (
    'class Worker { render(): void { this.prepare(); } private prepare(): void { } }\n'
    'class App { run(): void { new Worker().render(); } }\n'
    'interface Handler { handle(): void; }\n'
    'class Echo implements Handler { handle(): void { } poke(): void { this.handle(); } }\n'
    'class BaseEcho { protected tap(): void { } }\n'
    'class SubEcho extends BaseEcho { useIt(): void { this.tap(); } }\n'
    'function helper(n: number): number { return n * 2; }\n'
    'function driver(x: number): number { const r = helper(x); return r + helper(x + 1); }\n'
    'abstract class Frame { abstract layout(): void; draw(): void { this.layout(); } }\n'
    'class Panel extends Frame { layout(): void { } }\n'
)

# Family K: `interface Sized` exists so the implements-site position has a
# resolution target.
K_SIZED = (
    'interface Sized {\n'
    '    size: number;\n'
    '}\n'
    '\n'
    'class Impl implements Sized {\n'
    '    size: number = 0;\n'
    '}\n'
)

# Family L: intentionally illegal programs; isolated to `cp3c check`
# subprocesses, never opened in the LSP session.
L_EMPTY_IMPORT = 'import {} from "std/math";\n'
L_ASI_RETURN = 'function f(): number { return 1\n+ 2; }\n'


# -----------------------------------------------------------------------------
# Small helpers
# -----------------------------------------------------------------------------

def write(work, name, text):
    path = os.path.join(work, name)
    with open(path, "wb") as handle:
        handle.write(text.encode("utf-8"))
    return path


def pos(text, needle, offset=0, occurrence=0):
    return intel_check.position(text, needle, offset, occurrence)


def prepare_call_hierarchy(session, uri, text, needle, offset=0, occurrence=0):
    response = session.request("textDocument/prepareCallHierarchy", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    }, timeout=30.0)
    result = response.get("result")
    return result if isinstance(result, list) else []


def prepare_type_hierarchy(session, uri, text, needle, offset=0, occurrence=0):
    response = session.request("textDocument/prepareTypeHierarchy", {
        "textDocument": {"uri": uri},
        "position": pos(text, needle, offset, occurrence),
    }, timeout=30.0)
    result = response.get("result")
    return result if isinstance(result, list) else []


def incoming_calls(session, item):
    response = session.request("callHierarchy/incomingCalls", {
        "item": item,
    }, timeout=30.0)
    result = response.get("result")
    return result if isinstance(result, list) else []


def outgoing_calls(session, item):
    response = session.request("callHierarchy/outgoingCalls", {
        "item": item,
    }, timeout=30.0)
    result = response.get("result")
    return result if isinstance(result, list) else []


def to_items(calls):
    items = []
    for call in calls:
        if isinstance(call, dict):
            item = call.get("to")
            items.append(item if isinstance(item, dict) else {})
    return items


def from_items(calls):
    items = []
    for call in calls:
        if isinstance(call, dict):
            item = call.get("from")
            items.append(item if isinstance(item, dict) else {})
    return items


def from_ranges(calls, name):
    ranges = []
    for call in calls:
        if not isinstance(call, dict):
            continue
        frm = call.get("from")
        if not isinstance(frm, dict) or frm.get("name") != name:
            continue
        for entry in call.get("fromRanges") or []:
            if isinstance(entry, dict):
                ranges.append(entry)
    return ranges


def names_of(items):
    return [item.get("name") for item in items]


def reasonable_name(name):
    if not isinstance(name, str):
        return False
    stripped = name.strip().lower()
    return bool(stripped) and stripped not in (
        "unknown", "<unknown>", "unknown caller", "?", "unnamed", "<unnamed>",
    )


def indent_join(lines):
    return "; ".join(lines)


# -----------------------------------------------------------------------------
# The CP3 check relay for Family L (error-expected pins)
# -----------------------------------------------------------------------------

def cp3_check(cp3c, fixture_path):
    """Runs `cp3c check <file>` and returns (returncode, combined output).

    `cp3c check` renders diagnostics to stderr in the D2 machine-line format
    (`<path>:<line>:<col>: <severity>: <CODE>: <message>`), so Family L pins
    substring-match severity+code on the captured output.
    """
    proc = subprocess.run([cp3c, "check", fixture_path], capture_output=True,
                          timeout=180)
    return proc.returncode, (proc.stderr or b"") + (proc.stdout or b"")


def has_error_machine_line(blob):
    for line in blob.splitlines():
        for marker in (b": error: ", b": error[", b"error: CP", b"error[CP"):
            if marker in line:
                return True
    return False


def has_any_machine_diagnostic(blob):
    for line in blob.splitlines():
        for marker in (b": error: CP", b": warning: CP", b": note: CP",
                       b"error: CP", b"warning: CP", b"note: CP",
                       b"error[CP", b"warning[CP", b"note[CP"):
            if marker in line:
                return True
    return False


# -----------------------------------------------------------------------------
# Checks
# -----------------------------------------------------------------------------

def check_j01(report, session, uri, text):
    items = prepare_call_hierarchy(session, uri, text, "helper", 0, 0)
    report.check(
        "J-01: prepareCallHierarchy at helper answers an item named helper",
        bool(items) and any(item.get("name") == "helper" for item in items),
        items)


def check_j02(report, session, uri, text):
    items = prepare_call_hierarchy(session, uri, text, "helper", 0, 0)
    incoming = incoming_calls(session, items[0]) if items else []
    callers = names_of(from_items(incoming))
    report.check(
        "J-02: incoming calls for helper include the driver caller",
        "driver" in callers,
        indent_join([
            "prepare items: %r" % ([item.get("name") for item in items],),
            "incoming callers: %r" % (callers,),
        ]))


def check_j03(report, session, uri, text):
    items = prepare_call_hierarchy(session, uri, text, "helper", 0, 0)
    incoming = incoming_calls(session, items[0]) if items else []
    driver_line = pos(text, "function driver")["line"]
    ranges = from_ranges(incoming, "driver")
    distinct = {
        (
            entry.get("start", {}).get("line"),
            entry.get("start", {}).get("character"),
            entry.get("end", {}).get("line"),
            entry.get("end", {}).get("character"),
        )
        for entry in ranges
    }
    on_driver_line = bool(ranges) and all(
        entry.get("start", {}).get("line") == driver_line
        and entry.get("end", {}).get("line") == driver_line
        for entry in ranges)
    report.check(
        "J-03: incoming edge for driver carries both call sites in driver's body",
        len(distinct) >= 2 and on_driver_line,
        indent_join([
            "distinct ranges on line %d: %d" % (driver_line, len(distinct)),
            "ranges: %r" % (ranges,),
        ]))


def check_j04(report, session, uri, text):
    items = prepare_call_hierarchy(session, uri, text, "driver", 0, 0)
    outgoing = outgoing_calls(session, items[0]) if items else []
    callees = names_of(to_items(outgoing))
    report.check(
        "J-04: outgoing calls from driver include helper",
        "helper" in callees,
        indent_join([
            "prepare items: %r" % ([item.get("name") for item in items],),
            "outgoing callees: %r" % (callees,),
        ]))


def check_j05(report, session, uri, text):
    # Preparation at the `this.prepare()` call site: at HEAD a method
    # DECLARATION cannot be prepared (item 3), so the edge is pinned at the
    # caller's call site.
    items = prepare_call_hierarchy(session, uri, text, "this.prepare()", 5, 0)
    incoming = incoming_calls(session, items[0]) if items else []
    callers = names_of(from_items(incoming))
    report.check(
        "J-05: incoming calls for prepare include the render caller",
        "render" in callers,
        indent_join([
            "prepare items: %r" % ([item.get("name") for item in items],),
            "incoming callers: %r" % (callers,),
        ]))


def check_j06(report, session, uri, text):
    items = prepare_call_hierarchy(session, uri, text, "this.handle()", 5, 0)
    incoming = incoming_calls(session, items[0]) if items else []
    callers = names_of(from_items(incoming))
    report.check(
        "J-06: incoming calls for handle include the poke caller",
        "poke" in callers,
        indent_join([
            "prepare items: %r" % ([item.get("name") for item in items],),
            "incoming callers: %r" % (callers,),
        ]))


def check_j07(report, session, uri, text):
    items = prepare_call_hierarchy(session, uri, text, "this.layout()", 5, 0)
    incoming = incoming_calls(session, items[0]) if items else []
    callers = names_of(from_items(incoming))
    report.check(
        "J-07: incoming calls for layout include the draw caller",
        "draw" in callers,
        indent_join([
            "prepare items: %r" % ([item.get("name") for item in items],),
            "incoming callers: %r" % (callers,),
        ]))


def check_j08(report, session, uri, text):
    probes = (
        ("J-02 (driver)", prepare_call_hierarchy(session, uri, text, "helper", 0, 0),
         "driver"),
        ("J-05 (render)", prepare_call_hierarchy(session, uri, text,
                                                 "this.prepare()", 5, 0),
         "render"),
    )
    bad = []
    for label, items, want in probes:
        incoming = incoming_calls(session, items[0]) if items else []
        names = names_of(from_items(incoming))
        hit = next((name for name in names if name == want), None)
        if not reasonable_name(hit):
            bad.append("%s incoming edge carries no reasonable name (got %r; all callers: %r)"
                       % (label, hit, names))
    report.check(
        "J-08: caller entries carry a reasonable name (driver, render)",
        not bad,
        "; ".join(bad) if bad else "driver and render names are reasonable")


def check_k01(report, session, uri, text):
    items = prepare_type_hierarchy(session, uri, text,
                                   "implements Sized", len("implements "), 0)
    report.check(
        "K-01: prepareTypeHierarchy at the implements-site Sized answers a non-empty item list",
        isinstance(items, list) and len(items) > 0,
        items)


def check_l01(report, cp3c, fixture_path):
    try:
        returncode, blob = cp3_check(cp3c, fixture_path)
    except subprocess.TimeoutExpired:
        report.check("L-01: import {} produces a diagnostic", False,
                     "cp3c check exceeded the 180 s timeout")
        return
    except OSError as exc:
        report.check("L-01: import {} produces a diagnostic", False,
                     "cp3c check could not run: %r" % (exc,))
        return
    report.check(
        "L-01: import {} produces a diagnostic",
        has_any_machine_diagnostic(blob),
        "returncode=%d; output: %r" % (returncode, blob[:300]))


def check_l02(report, cp3c, fixture_path):
    try:
        returncode, blob = cp3_check(cp3c, fixture_path)
    except subprocess.TimeoutExpired:
        report.check("L-02: return with ASI continuation produces no error diagnostic",
                     False, "cp3c check exceeded the 180 s timeout")
        return
    except OSError as exc:
        report.check("L-02: return with ASI continuation produces no error diagnostic",
                     False, "cp3c check could not run: %r" % (exc,))
        return
    # Actual behaviour at HEAD, pinned deliberately: this compiler binds
    # binary operators across newlines (parseAdditive has no precededByNewline
    # gate; consumeSemicolon applies ASI silently), so `return 1` + newline +
    # `+ 2;` parses as `return (1 + 2)` with NO diagnostic. A restricted ASI
    # production would turn this pin red on purpose.
    report.check(
        "L-02: return with ASI continuation produces no error diagnostic",
        not has_error_machine_line(blob),
        "returncode=%d; output: %r" % (returncode, blob[:300]))


# -----------------------------------------------------------------------------
# Check driver
# -----------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, name, ok, detail=""):
        ok = bool(ok)
        if ok:
            self.passed += 1
            line = "PASS " + name
        else:
            self.failed += 1
            line = "FAIL " + name
            if detail:
                line += "  [%s]" % str(detail)[:300]
        print(line)

    def summary(self):
        total = self.passed + self.failed
        print("RESULT: %d/11 passed, %d failed" % (self.passed, self.failed))
        return self.failed == 0 and total == 11


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp3c", default=DEFAULT_CP3C,
                        help="path to cp3c.exe")
    args = parser.parse_args()

    cp3c = os.path.abspath(args.cp3c)
    temp = os.environ.get("TEMP") or tempfile.gettempdir()
    work = os.path.join(temp, "opencode",
                        "hierarchy-parser-check-" + uuid.uuid4().hex[:8])
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    j_path = write(work, "hierarchy_calls.cp3", J_CALLS)
    k_path = write(work, "hierarchy_sized.cp3", K_SIZED)
    l1_path = write(work, "hierarchy_empty_import.cp3", L_EMPTY_IMPORT)
    l2_path = write(work, "hierarchy_asi_return.cp3", L_ASI_RETURN)
    j_uri = intel_check.file_uri(j_path)
    k_uri = intel_check.file_uri(k_path)

    print("cp3c: %s" % cp3c)
    print("workspace: %s" % work)

    report = Report()
    done = set()

    # Every id prints exactly one line, even when the driver crashes: run()
    # turns a crash inside a check into that check's FAIL line, and any id a
    # crashed session block never reached is checked off in the handler.
    def run(check_id, fn):
        try:
            fn()
        except Exception as exc:
            report.check(check_id + ": driver exception", False, repr(exc))
        done.add(check_id)

    session = None
    try:
        session = intel_check.Session(cp3c, work)
        session.request("initialize", {
            "processId": None,
            "rootUri": intel_check.file_uri(work),
            "capabilities": {},
        }, timeout=30.0)
        for key, text in (("hierarchy_calls.cp3", J_CALLS),
                          ("hierarchy_sized.cp3", K_SIZED)):
            session.notify("textDocument/didOpen", {
                "textDocument": {
                    "uri": intel_check.file_uri(os.path.join(work, key)),
                    "languageId": "cp3",
                    "version": 1,
                    "text": text,
                },
            })
        # Barrier: let any coalesced analysis drain before the checks.
        session.request("textDocument/hover", {
            "textDocument": {"uri": j_uri},
            "position": {"line": 0, "character": 0},
        }, timeout=30.0)

        run("J-01", lambda: check_j01(report, session, j_uri, J_CALLS))
        run("J-02", lambda: check_j02(report, session, j_uri, J_CALLS))
        run("J-03", lambda: check_j03(report, session, j_uri, J_CALLS))
        run("J-04", lambda: check_j04(report, session, j_uri, J_CALLS))
        run("J-05", lambda: check_j05(report, session, j_uri, J_CALLS))
        run("J-06", lambda: check_j06(report, session, j_uri, J_CALLS))
        run("J-07", lambda: check_j07(report, session, j_uri, J_CALLS))
        run("J-08", lambda: check_j08(report, session, j_uri, J_CALLS))
        run("K-01", lambda: check_k01(report, session, k_uri, K_SIZED))
        session.shutdown()
    except Exception as exc:
        for check_id in ("J-01", "J-02", "J-03", "J-04", "J-05", "J-06",
                         "J-07", "J-08", "K-01"):
            if check_id not in done:
                report.check(check_id + ": driver exception", False, repr(exc))
                done.add(check_id)
        try:
            if session is not None:
                session.proc.kill()
        except Exception:
            pass

    # Family L runs through `cp3c check` subprocesses, not the LSP session.
    run("L-01", lambda: check_l01(report, cp3c, l1_path))
    run("L-02", lambda: check_l02(report, cp3c, l2_path))

    ok = report.summary()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
