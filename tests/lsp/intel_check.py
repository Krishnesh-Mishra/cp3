#!/usr/bin/env python3
# =============================================================================
# tests/lsp/intel_check.py - acceptance driver for the M2-LSP-INTEL providers.
#
# Drives a real `cp3c lsp` process over stdio (no ports, no third-party
# packages) and checks the contract the intel round promised:
#   * initialize advertises completion (triggers ".", '"', and ":"), hover,
#     definition, documentSymbol, workspaceSymbol, signatureHelp (triggers "("
#     and ",") and documentFormatting, while textDocumentSync stays full (1);
#   * hover reports the inferred type of `const s = "x"` as `string`;
#   * completion after `Math.` and `path.` offers the imported std namespace
#     members, and after a string variable only string members (the probe
#     analysis never offers what the compiler would reject);
#   * completion is position-aware: an expression position offers the names in
#     scope (the locals, the parameters, the imported names) and only the
#     keywords that can legally appear there - no `finally`, `catch`,
#     `abstract`, `private` or `implements`; a try block adds `catch` and
#     `finally`, a class body adds the member modifiers, a parameter list adds
#     `ref`, and a type position offers only the names that name a type;
#   * completion in a broken file (stray dot, unterminated string) and inside
#     comments is a well-formed empty answer, and the server stays alive;
#   * definition jumps to a same-file function and across a local module
#     import, and answers nothing for std imports;
#   * document and workspace symbols list declarations;
#   * signature help labels `add(number, number)` and tracks the active
#     parameter across the argument list;
#   * formatting is byte-identical to `cp3c fmt` and idempotent, and refuses
#     (no edits) a file with lexical errors just like the command;
#   * a bounded sweep sends hover/completion/definition at every line boundary
#     and requires well-formed answers with no error frame.
#
# Every check prints a machine-checkable `PASS <name>` / `FAIL <name>` line.
# The full transcript and a copy of the checks are written to the evidence
# directory. Exit code 0 when every check passes, 1 otherwise.
#
# Usage (from the repo root):
#   python tests\lsp\intel_check.py --cp3c build\scratch\rounds\build\bin\cp3c.exe
# =============================================================================

import argparse
import json
import os
import queue
import shutil
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# -----------------------------------------------------------------------------
# Fixtures (ASCII only: byte, UTF-16 and character offsets then coincide).
# -----------------------------------------------------------------------------

MAIN = (
    'import Math from "std/math";\n'
    'import path from "std/path";\n'
    '\n'
    'const s = "x";\n'
    'let n = 1 + 2;\n'
    'function add(a: number, b: number): number {\n'
    '  return a + b;\n'
    '}\n'
    'const total = add(n, 3);\n'
    'const m = Math.abs(-1);\n'
    'const p = path.join("a", "b");\n'
    'const text = "hello";\n'
    'const up = text.toUpperCase();\n'
)

UTIL = (
    'export function combine(a: string, b: string): string {\n'
    '  return a + b;\n'
    '}\n'
)

IMPORT_MAIN = (
    'import { combine } from "./intel_util.cp3";\n'
    '\n'
    'const joined = combine("a", "b");\n'
)

# The probe lines are deliberately unfinished: completion is requested right
# after the dot. These are namespace/value checks; contextual server callbacks
# below use isolated files so one incomplete expression cannot corrupt another.
MEMBER = (
    'import Math from "std/math";\n'
    'import path from "std/path";\n'
    '\n'
    'const text = "hello";\n'
    'const probeMath = Math.\n'
    'const probePath = path.\n'
    'const probeText = text.\n'
)

OBJECT_MEMBER = "const Obj = { name: 'Rahul' };\nObj.\n"
OBJECT_PARTIAL_MEMBER = "const Obj = { name: 'Rahul' };\nObj.na\n"
OBJECT_SHADOW = "const Object = { name: 'Rahul' };\nconst label = Object.name;\n"
RESERVED_IDENTIFIER = "const const = 1;\n"
ASSIGNMENT_DIAGNOSTICS = (
    'const Obj = { name: "Rahul" };\n'
    'Obj.name = "Asha";\n'
    'Obj.name = 123;\n'
    'Obj = { name: "Other" };\n'
)
NETWORK_SERVER_MEMBER = (
    'import { createServer } from "std/network";\n'
    'const app = createServer();\n'
    'app.\n'
)
NETWORK_REQUEST_MEMBER = (
    'import { createServer } from "std/network";\n'
    'const app = createServer();\n'
    'app.post("/request", (req, res) => {\n'
    '  req.\n'
    '});\n'
)
NETWORK_RESPONSE_MEMBER = (
    'import { createServer } from "std/network";\n'
    'const app = createServer();\n'
    'app.get("/response", (req, res) => {\n'
    '  res.\n'
    '});\n'
)

BROKEN_DOT = 'const x = 1.\n'
BROKEN_STRING = 'const s = "abc\n'
STDLIB_PARTIAL = 'consol\n'
STDLIB_TYPE_PARTIAL = 'const x: Map = 1;\n'
CONSOLE_EVENT_STRING = (
    'import console from "std/console";\n'
    'console.on("keyp", (key) => { console.log(key); });\n'
)
EMITTER_STRING_ARGUMENT = (
    'const ev = new EventEmitter();\n'
    'ev.emit(Strin);\n'
)
EMITTER_MEMBER = (
    'const ev = new EventEmitter();\n'
    'ev.\n'
)
CALLBACK_DIAGNOSTICS = (
    'import console from "std/console";\n'
    'const ev = new EventEmitter<string>();\n'
    'console.on("keypress", (key, aa) => { console.log(key); });\n'
)
IMPORT_SPEC = 'import Math from "std/'
LOCAL_SPEC = 'import x from "./'

# The reported screenshot: a small file whose cursor sits in an EXPRESSION
# position inside a function body. The completion must offer the names in
# scope (the locals `a` and `b`, the imported `console`) and only the
# keywords that can legally appear there.
SCOPE = (
    'import console from "std/console";\n'
    '\n'
    'function main(): void {\n'
    '  const a = 1;\n'
    '  const b = a + 1;\n'
    '  console.log();\n'
    '}\n'
)

# The same file with a `try` block: inside it `catch` and `finally` belong.
TRY_BLOCK = (
    'function main(): void {\n'
    '  try {\n'
    '    \n'
    '  } catch (e) {\n'
    '  }\n'
    '}\n'
)

# The same file without a `try` block: `catch` and `finally` cannot appear.
NO_TRY = (
    'function main(): void {\n'
    '  \n'
    '}\n'
)

PARTIAL_STATEMENT = (
    'function main(): void {\n'
    '  re\n'
    '}\n'
)

# A class body: the member modifiers belong there, the statement keywords do
# not.
CLASS_BODY = (
    'class Counter {\n'
    '  \n'
    '}\n'
)

# A parameter list: only `ref` may start there.
PARAM_LIST = (
    'function add(a: number, b: number): number {\n'
    '  return a + b;\n'
    '}\n'
)

# A type position: only the names that name a type belong there.
TYPE_POS = (
    'class Counter {\n'
    '  count: number = 0;\n'
    '}\n'
    '\n'
    'interface Shape {\n'
    '}\n'
    '\n'
    'function use(c: ): void {\n'
    '}\n'
)

# A nested block: the inner local is closer to the cursor than the outer one.
NESTED = (
    'function main(): void {\n'
    '  const outer = 1;\n'
    '  if (true) {\n'
    '    const inner = 2;\n'
    '    \n'
    '  }\n'
    '}\n'
)

# CRLF line endings, trailing spaces, uneven indentation, extra blank lines.
MESSY = (
    'import Math from "std/math";\r\n'
    '\r\n'
    'function f(n: number): number {\r\n'
    '     return n + 1;   \r\n'
    '}\r\n'
    '\r\n'
    '\r\n'
    'const x = f(1);\r\n'
    '\r\n'
)

# A documented program: a JSDoc-style block comment above a function, a `///`
# line comment above another one, and a documented class with a documented
# method.
DOCUMENTED = (
    'import console from "std/console";\n'
    '\n'
    '/**\n'
    ' * Adds two numbers.\n'
    ' * @param a The first number.\n'
    ' * @param b The second number.\n'
    ' * @returns The sum of a and b.\n'
    ' */\n'
    'function add(a: number, b: number): number {\n'
    '  return a + b;\n'
    '}\n'
    '\n'
    '/// Greets the world.\n'
    'function greet(): string {\n'
    '  return "hello";\n'
    '}\n'
    '\n'
    '/// A greeter.\n'
    'class Greeter {\n'
    '  /// Greets the given name.\n'
    '  function speak(name: string): string {\n'
    '    return "hi " + name;\n'
    '  }\n'
    '}\n'
    '\n'
    'const total = add(1, 2);\n'
    'console.log(total);\n'
    'console.log(greet());\n'
)

# The same program without a single doc comment: a doc comment must never
# change what a program does.
UNDOCUMENTED = (
    'import console from "std/console";\n'
    '\n'
    'function add(a: number, b: number): number {\n'
    '  return a + b;\n'
    '}\n'
    '\n'
    'function greet(): string {\n'
    '  return "hello";\n'
    '}\n'
    '\n'
    'class Greeter {\n'
    '  function speak(name: string): string {\n'
    '    return "hi " + name;\n'
    '  }\n'
    '}\n'
    '\n'
    'const total = add(1, 2);\n'
    'console.log(total);\n'
    'console.log(greet());\n'
)

# The std surfaces whose members must all carry documentation.
DOC_STD = (
    'import console from "std/console";\n'
    'import Math from "std/math";\n'
    'import path from "std/path";\n'
    '\n'
    'const probeConsole = console.\n'
    'const probeMath = Math.\n'
    'const probePath = path.\n'
    'const useConsole = console.log("x");\n'
    'const useMath = Math.abs(-1);\n'
)


# -----------------------------------------------------------------------------
# JSON-RPC over stdio
# -----------------------------------------------------------------------------

class Session:
    def __init__(self, cp3c, cwd):
        self.proc = subprocess.Popen(
            [cp3c, "lsp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd,
        )
        self.inbox = queue.Queue()
        self.transcript = []
        self.notifications = []
        self.stderr_lines = []
        self.next_id = 1
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()
        self.err_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self.err_reader.start()

    def _read_stderr(self):
        for line in self.proc.stderr:
            self.stderr_lines.append(line.decode("utf-8", "replace"))

    def _read_loop(self):
        stream = self.proc.stdout
        try:
            while True:
                line = stream.readline()
                if not line:
                    return
                length = None
                while line not in (b"\r\n", b"\n"):
                    if line.lower().startswith(b"content-length:"):
                        length = int(line.split(b":", 1)[1].strip())
                    line = stream.readline()
                    if not line:
                        return
                if length is None:
                    self.inbox.put(("fatal", "frame without Content-Length"))
                    return
                body = stream.read(length)
                if len(body) != length:
                    self.inbox.put(("fatal", "short frame body"))
                    return
                self.transcript.append(("recv", body))
                message = json.loads(body.decode("utf-8"))
                if "id" in message and ("result" in message or "error" in message):
                    self.inbox.put(("response", message))
                else:
                    self.inbox.put(("notification", message))
        except Exception as exc:  # pragma: no cover - diagnostic path
            self.inbox.put(("fatal", "reader failed: %s" % exc))

    def _send(self, message):
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        self.transcript.append(("sent", payload))
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(payload))
        self.proc.stdin.write(payload)
        self.proc.stdin.flush()

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=60.0):
        request_id = self.next_id
        self.next_id += 1
        self._send({
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        })
        while True:
            try:
                kind, message = self.inbox.get(timeout=timeout)
            except queue.Empty:
                raise RuntimeError(
                    "timeout waiting for %s (id %d)" % (method, request_id))
            if kind == "notification":
                self.notifications.append(message)
                continue
            if kind != "response":
                raise RuntimeError(
                    "protocol failure while waiting for %s: %r" % (method, message))
            if message.get("id") != request_id:
                raise RuntimeError(
                    "out-of-order response: expected id %d, got %r"
                    % (request_id, message.get("id")))
            return message

    def shutdown(self):
        response = self.request("shutdown", None)
        self.notify("exit", None)
        try:
            code = self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            code = None
        self.reader.join(timeout=5)
        self.err_reader.join(timeout=5)
        return response, code


# -----------------------------------------------------------------------------
# Small helpers
# -----------------------------------------------------------------------------

class Report:
    def __init__(self, path):
        self.path = path
        self.lines = []
        self.passed = 0
        self.failed = 0

    def check(self, name, ok, detail=""):
        if ok:
            self.passed += 1
            line = "PASS " + name
        else:
            self.failed += 1
            line = "FAIL " + name
            if detail:
                line += "  [" + str(detail)[:300] + "]"
        print(line)
        self.lines.append(line)

    def finish(self):
        total = self.passed + self.failed
        summary = "RESULT: %d/%d passed, %d failed" % (
            self.passed, total, self.failed)
        print(summary)
        self.lines.append(summary)
        with open(self.path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(self.lines) + "\n")
        return self.failed == 0


def file_uri(path):
    return "file:///" + path.replace("\\", "/").lstrip("/")


def position(text, needle, offset=0, occurrence=0):
    index = -1
    for _ in range(occurrence + 1):
        index = text.find(needle, index + 1)
        if index < 0:
            raise RuntimeError("fixture needle not found: %r" % needle)
    index += offset
    line = text.count("\n", 0, index)
    start = text.rfind("\n", 0, index) + 1
    return {"line": line, "character": index - start}


def write_fixture(work, name, text):
    path = os.path.join(work, name)
    with open(path, "wb") as handle:
        handle.write(text.encode("utf-8"))
    return path


def labels(items):
    return [item.get("label", "") for item in items or []]


def diagnostics_for(session, uri):
    matches = [notification.get("params", {}).get("diagnostics", [])
               for notification in session.notifications
               if notification.get("method") == "textDocument/publishDiagnostics"
               and notification.get("params", {}).get("uri") == uri]
    return matches[-1] if matches else None


def completion_items(response):
    result = response.get("result")
    if not isinstance(result, dict):
        return None
    items = result.get("items")
    return items if isinstance(items, list) else None


def hover_value(response):
    result = response.get("result")
    if not isinstance(result, dict):
        return ""
    contents = result.get("contents")
    if isinstance(contents, dict):
        return contents.get("value", "")
    return ""


def item_documentation(item):
    """The documentation text of one completion item ("" when it has none)."""
    if not isinstance(item, dict):
        return ""
    documentation = item.get("documentation")
    if isinstance(documentation, dict):
        value = documentation.get("value")
        return value if isinstance(value, str) else ""
    if isinstance(documentation, str):
        return documentation
    return ""


def signature_documentation(response):
    """The documentation of the first signature of a signature-help answer."""
    result = response.get("result")
    if not isinstance(result, dict):
        return ""
    signatures = result.get("signatures")
    if not isinstance(signatures, list) or not signatures:
        return ""
    documentation = signatures[0].get("documentation")
    if isinstance(documentation, dict):
        value = documentation.get("value")
        return value if isinstance(value, str) else ""
    if isinstance(documentation, str):
        return documentation
    return ""


def locations(response):
    result = response.get("result")
    if result is None:
        return []
    if isinstance(result, list):
        return [item for item in result if isinstance(item, dict)]
    return []


def start_line(response, index=0):
    found = locations(response)
    if len(found) <= index:
        return None
    return found[index].get("range", {}).get("start")


# -----------------------------------------------------------------------------
# Checks
# -----------------------------------------------------------------------------

def check_capabilities(report, response):
    result = response.get("result")
    caps = result.get("capabilities", {}) if isinstance(result, dict) else {}
    report.check("initialize: full text sync", caps.get("textDocumentSync") == 1)
    completion = caps.get("completionProvider")
    report.check(
        "initialize: completion triggers ., \" and :",
        isinstance(completion, dict)
        and completion.get("triggerCharacters") == [".", '"', ":"],
        completion,
    )
    signature = caps.get("signatureHelpProvider")
    report.check(
        "initialize: signature triggers ( and ,",
        isinstance(signature, dict)
        and signature.get("triggerCharacters") == ["(", ","],
        signature,
    )
    for key in ("hoverProvider", "definitionProvider", "documentSymbolProvider",
                "workspaceSymbolProvider", "documentFormattingProvider"):
        report.check("initialize: " + key, caps.get(key) is True)


def check_hover(report, session, uri):
    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(MAIN, "const s", 6),
    })
    value = hover_value(response)
    report.check("hover: const s = \"x\" is string", "s: string" in value, value)

    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(MAIN, "function add", 9),
    })
    value = hover_value(response)
    report.check(
        "hover: function declaration is its function type",
        "add:" in value and "=> number" in value,
        value,
    )

    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(MAIN, "Math.abs", 5),
    })
    value = hover_value(response)
    report.check(
        "hover: std member Math.abs is a function",
        "abs:" in value and "=>" in value,
        value,
    )

    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": {"line": 0, "character": 0},
    })
    report.check("hover: keyword answers null", response.get("result") is None,
                 response.get("result"))


def check_language_diagnostics(report, session, shadow_uri, reserved_uri,
                               assignment_uri, callback_uri):
    shadow = diagnostics_for(session, shadow_uri)
    report.check(
        "diagnostics: shadowing the JS built-in Object is valid",
        shadow is not None and not any(item.get("severity") == 1 for item in shadow),
        shadow,
    )
    reserved = diagnostics_for(session, reserved_uri)
    report.check(
        "diagnostics: a reserved keyword cannot be a binding name",
        reserved is not None and any(
            item.get("severity") == 1 and "syntax error" in item.get("message", "")
            for item in reserved
        ),
        reserved,
    )
    assignments = diagnostics_for(session, assignment_uri)
    assignment_codes = {
        str(item.get("code", "")) for item in (assignments or [])
        if item.get("severity") == 1
    }
    report.check(
        "diagnostics: incompatible object-property assignment is underlined",
        "CP3401" in assignment_codes,
        assignments,
    )
    report.check(
        "diagnostics: const rebinding is underlined",
        "CP3408" in assignment_codes,
        assignments,
    )
    report.check(
        "diagnostics: assigning one string property value to another is valid",
        assignments is not None and not any(
            item.get("severity") == 1 and
            item.get("range", {}).get("start", {}).get("line") == 1
            for item in assignments
        ),
        assignments,
    )
    callback = diagnostics_for(session, callback_uri)
    callback_errors = [item for item in (callback or [])
                       if item.get("severity") == 1]
    callback_codes = {str(item.get("code", "")) for item in callback_errors}
    report.check(
        "diagnostics: unsupported EventEmitter type arguments are rejected",
        "CP3417" in callback_codes,
        callback,
    )
    extra_param = next((item for item in callback_errors
                        if "callback parameter 'aa'" in item.get("message", "")), None)
    report.check(
        "diagnostics: an event callback's extra required parameter is underlined",
        extra_param is not None and "CP3401" in callback_codes,
        callback,
    )


def check_completion(report, session, member_uri, object_uri,
                     object_partial_uri, server_uri, request_uri, response_uri,
                     broken_dot_uri, broken_string_uri, spec_uri, local_uri,
                     main_uri, stdlib_partial_uri, stdlib_type_partial_uri):
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": member_uri},
        "position": position(MEMBER, "const probeMath = Math.", 23),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: Math. offers std/math members",
        items is not None and "abs" in found and "ceil" in found
        and "PI" in found,
        found,
    )
    report.check(
        "completion: Math. offers nothing from other namespaces",
        items is not None and "join" not in found and "toUpperCase" not in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": member_uri},
        "position": position(MEMBER, "const probePath = path.", 23),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: path. offers std/path members",
        items is not None and "join" in found and "dirname" in found,
        found,
    )
    report.check(
        "completion: path. filters invalid members",
        items is not None and "abs" not in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": member_uri},
        "position": position(MEMBER, "const probeText = text.", 23),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: string receiver offers string members",
        items is not None and "toUpperCase" in found and "length" in found,
        found,
    )
    report.check(
        "completion: string receiver filters array members",
        items is not None and "push" not in found and "abs" not in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": object_uri},
        "position": position(OBJECT_MEMBER, "Obj.\n", 4),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: object literal receiver offers its declared properties",
        items is not None and "name" in found and "toUpperCase" not in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": object_partial_uri},
        "position": position(OBJECT_PARTIAL_MEMBER, "Obj.na\n", 6),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: object property completion works during partial identifier typing",
        items is not None and "name" in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": server_uri},
        "position": position(NETWORK_SERVER_MEMBER, "app.\n", 4),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: typed network server offers every supported HTTP route method",
        items is not None and all(name in found for name in
                                  ("get", "post", "put", "delete", "patch",
                                   "head", "options", "any", "use", "listen",
                                   "close")),
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": request_uri},
        "position": position(NETWORK_REQUEST_MEMBER, "  req.\n", 6),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: HTTP request callback has a typed request surface",
        items is not None and all(name in found for name in
                                  ("method", "path", "params", "query",
                                   "headers", "text", "json")),
        found,
    )
    request_text = next((item for item in items or []
                         if item.get("label") == "text"), {})
    report.check(
        "completion: HTTP request method explains its behavior",
        "buffered request body as text" in item_documentation(request_text),
        request_text,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": response_uri},
        "position": position(NETWORK_RESPONSE_MEMBER, "  res.\n", 6),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: HTTP response callback has a typed response surface",
        items is not None and all(name in found for name in
                                  ("status", "header", "text", "json", "send",
                                   "write", "end")),
        found,
    )
    response_write = next((item for item in items or []
                           if item.get("label") == "write"), {})
    report.check(
        "completion: HTTP response write documents accepted byte/text chunks",
        "raw Buffer bytes" in item_documentation(response_write),
        response_write,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": broken_dot_uri},
        "position": {"line": 0, "character": len("const x = 1.")},
    })
    items = completion_items(response)
    report.check("completion: stray dot is a well-formed empty list",
                 items == [], items)

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": broken_string_uri},
        "position": {"line": 0, "character": len(BROKEN_STRING.rstrip("\n"))},
    })
    items = completion_items(response)
    report.check(
        "completion: unterminated string is a well-formed empty list",
        items == [], items)

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": spec_uri},
        "position": {"line": 0, "character": len(IMPORT_SPEC)},
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: import specifier offers std modules",
        items is not None and "std/math" in found and "std/path" in found
        and "std/console" in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": local_uri},
        "position": {"line": 0, "character": len(LOCAL_SPEC)},
    })
    items = completion_items(response)
    report.check("completion: relative specifier answers nothing (no disk scan)",
                 items == [], items)

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": main_uri},
        "position": {"line": 0, "character": 0},
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: top level offers declarations and keywords",
        items is not None and all(name in found for name in
                                  ("add", "s", "function", "class", "import",
                                   "const")),
        found,
    )


    response = session.request("textDocument/completion", {
        "textDocument": {"uri": stdlib_partial_uri},
        "position": {"line": 0, "character": len("consol")},
    })
    items = completion_items(response)
    found = labels(items)
    console_entry = next((it for it in (items or []) if it.get("label") == "console"),
                          None)
    has_console_import_edit = (
        console_entry is not None and
        any("import console from \"std/console\"" in e.get("newText", "")
            for e in console_entry.get("additionalTextEdits", []))
    )
    report.check(
        "completion: bare stdlib 'console' carries an auto-import additionalTextEdits",
        items is not None and "console" in found and has_console_import_edit,
        items,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": stdlib_type_partial_uri},
        "position": {"line": 0, "character": len("const x: ")},
    })
    items = completion_items(response)
    found = labels(items)
    map_entry = next((it for it in (items or []) if it.get("label") == "Map"),
                      None)
    has_map_import_edit = (
        map_entry is not None and
        any("import { Map } from \"std/collections/types\"" in e.get("newText", "")
            for e in map_entry.get("additionalTextEdits", []))
    )
    report.check(
        "completion: type position offers std/collections/types with auto-import edits",
        items is not None and "Map" in found and has_map_import_edit,
        items,
    )

def check_semantic_completion(report, session, event_uri, string_uri,
                              emitter_uri):
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": event_uri},
        "position": position(CONSOLE_EVENT_STRING, '"keyp', len('"keyp')),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: console.on string argument offers only documented events",
        items is not None and set(found) == {"keypress", "line", "close"},
        found,
    )
    keypress = next((item for item in items or []
                     if item.get("label") == "keypress"), {})
    report.check(
        "completion: event literal explains when it fires",
        "each key" in item_documentation(keypress),
        keypress,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": string_uri},
        "position": position(EMITTER_STRING_ARGUMENT, "Strin", len("Strin")),
    })
    items = completion_items(response)
    string_item = next((item for item in items or []
                        if item.get("label") == "String"), {})
    report.check(
        "completion: string argument offers String(value) with a usable call snippet",
        bool(string_item) and string_item.get("insertText") == "String(${1:value})"
        and string_item.get("insertTextFormat") == 2,
        string_item,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": emitter_uri},
        "position": position(EMITTER_MEMBER, "ev.\n", len("ev.")),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: EventEmitter instance offers its event methods",
        items is not None and all(name in found for name in
                                  ("on", "once", "off", "emit")),
        found,
    )


def check_context_completion(report, session, scope_uri, try_uri, notry_uri,
                             class_uri, param_uri, type_uri, nested_uri,
                             partial_uri):
    # The reported screenshot: the cursor is in an expression position.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": scope_uri},
        "position": position(SCOPE, "console.log(", 12),
    })
    items = completion_items(response)
    found = labels(items)
    illegal = ("finally", "catch", "abstract", "private", "implements",
               "public", "protected", "static", "get", "set", "interface",
               "enum", "as", "of", "in", "let", "const", "var", "class")
    report.check(
        "completion: an expression position offers no illegal keyword",
        items is not None and not any(name in found for name in illegal),
        found,
    )
    report.check(
        "completion: an expression position offers the local declared above",
        items is not None and "a" in found and "b" in found,
        found,
    )
    report.check(
        "completion: an expression position offers the imported name",
        items is not None and "console" in found,
        found,
    )
    report.check(
        "completion: the local scope leads the popup",
        items is not None and bool(found) and found[0] in ("a", "b"),
        found,
    )
    details = {}
    for item in items or []:
        details[item.get("label")] = item.get("detail")
    report.check("completion: a local carries the type the compiler inferred",
                 details.get("a") == "number", details)
    report.check("completion: the import carries its module",
                 details.get("console") == 'imported from "std/console"',
                 details)

    # Inside a try block, `catch` and `finally` are the keywords that belong.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": try_uri},
        "position": position(TRY_BLOCK, "    \n", 4),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: inside a try block offers catch and finally",
        items is not None and "catch" in found and "finally" in found,
        found,
    )
    report.check(
        "completion: inside a try block offers no class-member modifier",
        items is not None and not any(name in found for name in
                                      ("private", "public", "protected",
                                       "static", "get", "set")),
        found,
    )

    # Outside a try block they cannot appear.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": notry_uri},
        "position": position(NO_TRY, "  \n", 2),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: outside a try block offers no catch or finally",
        items is not None and "catch" not in found and "finally" not in found,
        found,
    )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": partial_uri},
        "position": position(PARTIAL_STATEMENT, "re\n", 2),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: a partial statement keeps return and const suggestions",
        items is not None and "return" in found and "const" in found,
        found,
    )

    # A class body offers the member modifiers, not the statement keywords.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": class_uri},
        "position": position(CLASS_BODY, "  \n", 2),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: a class body offers the member modifiers",
        items is not None and "private" in found and "static" in found,
        found,
    )
    report.check(
        "completion: a class body offers no statement keyword",
        items is not None and not any(name in found for name in
                                      ("let", "const", "var", "if", "while",
                                       "return", "import")),
        found,
    )

    # A parameter list offers only `ref`.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": param_uri},
        "position": position(PARAM_LIST, "add(", 4),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: a parameter list offers ref and no statement keyword",
        items is not None and "ref" in found and not any(
            name in found for name in ("let", "const", "if", "return", "class")),
        found,
    )

    # A type position offers the names that name a type, and nothing else.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": type_uri},
        "position": position(TYPE_POS, "use(c:", 6),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: a type position offers declarations and primitive types",
        items is not None and all(name in found for name in
                                  ("Counter", "Shape", "number", "string",
                                   "boolean", "void", "unknown")),
        found,
    )
    report.check(
        "completion: a type position offers no value or statement keyword",
        items is not None and not any(name in found for name in
                                      ("count", "use", "let", "const")),
        found,
    )
    number_item = next((item for item in items or []
                        if item.get("label") == "number"), {})
    report.check(
        "completion: built-in type explains itself",
        "numeric type" in item_documentation(number_item), number_item,
    )

    # The nearest scope leads: the inner local before the outer one.
    response = session.request("textDocument/completion", {
        "textDocument": {"uri": nested_uri},
        "position": position(NESTED, "    \n", 4),
    })
    items = completion_items(response)
    found = labels(items)
    report.check(
        "completion: the nearest scope leads the popup",
        items is not None and len(found) > 1 and found[0] == "inner"
        and found[1] == "outer",
        found,
    )


def check_documentation(report, session, uri, std_uri):
    """The documentation the editor shows: hover, completion, signature help."""
    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(DOCUMENTED, "function add", 9),
    })
    value = hover_value(response)
    report.check(
        "hover: a documented function shows its description",
        "Adds two numbers." in value,
        value,
    )
    report.check(
        "hover: a documented function shows its @param list",
        "The first number." in value and "The second number." in value,
        value,
    )
    report.check(
        "hover: a documented function shows its @returns text",
        "The sum of a and b." in value,
        value,
    )

    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(DOCUMENTED, "add(1, 2)", 1),
    })
    value = hover_value(response)
    report.check(
        "hover: the call site shows the documentation too",
        "Adds two numbers." in value and "The first number." in value,
        value,
    )

    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(DOCUMENTED, "function greet", 9),
    })
    value = hover_value(response)
    report.check(
        "hover: a `///` documented function shows its description",
        "Greets the world." in value,
        value,
    )

    response = session.request("textDocument/hover", {
        "textDocument": {"uri": uri},
        "position": position(DOCUMENTED, "function speak", 9),
    })
    value = hover_value(response)
    report.check(
        "hover: a documented method shows its description",
        "Greets the given name." in value,
        value,
    )

    # The std surface: the documented signature and the description.
    response = session.request("textDocument/hover", {
        "textDocument": {"uri": std_uri},
        "position": position(DOC_STD, "console.log", 8),
    })
    value = hover_value(response)
    report.check(
        "hover: console.log shows the std documentation",
        "console.log(" in value and "stdout" in value,
        value,
    )
    response = session.request("textDocument/hover", {
        "textDocument": {"uri": std_uri},
        "position": position(DOC_STD, "Math.abs", 5),
    })
    value = hover_value(response)
    report.check(
        "hover: Math.abs shows the documented signature",
        "Math.abs(x: number): number" in value,
        value,
    )

    # The completion popup: every std member carries its documentation.
    for probe, namespace in (("const probeConsole = console.", "console"),
                             ("const probeMath = Math.", "Math"),
                             ("const probePath = path.", "path")):
        response = session.request("textDocument/completion", {
            "textDocument": {"uri": std_uri},
            "position": position(DOC_STD, probe, len(probe)),
        })
        items = completion_items(response)
        found = labels(items)
        report.check(
            "completion: %s. offers its members" % namespace,
            items is not None and len(found) > 0,
            found,
        )
        missing = [name for name, item in zip(found, items or [])
                   if not item_documentation(item)]
        report.check(
            "completion: every %s member carries documentation" % namespace,
            items is not None and not missing,
            missing,
        )

    response = session.request("textDocument/completion", {
        "textDocument": {"uri": std_uri},
        "position": position(DOC_STD, "const probeConsole = console.",
                             len("const probeConsole = console.")),
    })
    items = completion_items(response) or []
    log_doc = ""
    for item in items:
        if item.get("label") == "log":
            log_doc = item_documentation(item)
    report.check(
        "completion: the log member documents what it prints",
        "stdout" in log_doc,
        log_doc,
    )

    # The signature help: the callee's documentation and its parameters.
    response = session.request("textDocument/signatureHelp", {
        "textDocument": {"uri": uri},
        "position": position(DOCUMENTED, "add(1, 2)", 4),
    })
    documentation = signature_documentation(response)
    report.check(
        "signatureHelp: the callee's documentation is shown",
        "Adds two numbers." in documentation,
        documentation,
    )
    result = response.get("result")
    parameters = []
    if isinstance(result, dict) and isinstance(result.get("signatures"), list):
        if result["signatures"]:
            parameters = result["signatures"][0].get("parameters") or []
    documented = [
        item for item in parameters
        if isinstance(item, dict) and item.get("documentation")
    ]
    report.check(
        "signatureHelp: the parameters carry their documentation",
        len(documented) == 2,
        parameters,
    )


def check_doc_comment_output(report, cp3c, work):
    """A doc comment never changes what a program does."""
    documented = write_fixture(work, "doc_documented.cp3", DOCUMENTED)
    undocumented = write_fixture(work, "doc_undocumented.cp3", UNDOCUMENTED)
    with_run = subprocess.run([cp3c, "run", documented], capture_output=True,
                              timeout=300)
    without_run = subprocess.run([cp3c, "run", undocumented], capture_output=True,
                                 timeout=300)
    with_out = with_run.stdout.replace(b"\r\n", b"\n")
    without_out = without_run.stdout.replace(b"\r\n", b"\n")
    report.check(
        "doc comment: the program's output is unchanged",
        with_out == without_out and with_run.returncode == without_run.returncode,
        "documented=%r undocumented=%r" % (with_out[:200], without_out[:200]),
    )
    report.check(
        "doc comment: the documented program still runs",
        with_run.returncode == 0 and b"3" in with_out and b"hello" in with_out,
        "%r / %r" % (with_out[:200], with_run.stderr[:200]),
    )


def check_definition(report, session, main_uri, import_uri, util_path):
    response = session.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": position(MAIN, "total = add", 8),
    })
    found = locations(response)
    report.check(
        "definition: call jumps to same-file function",
        len(found) == 1
        and found[0].get("uri", "").lower().endswith("intel_main.cp3"),
        found,
    )
    report.check(
        "definition: same-file range is the function name",
        start_line(response) == position(MAIN, "function add", 9),
        start_line(response),
    )

    response = session.request("textDocument/definition", {
        "textDocument": {"uri": import_uri},
        "position": position(IMPORT_MAIN, "combine", 0, 1),
    })
    found = locations(response)
    report.check(
        "definition: local import jumps to the exported declaration",
        len(found) == 1
        and found[0].get("uri", "").lower().endswith("intel_util.cp3"),
        found,
    )
    report.check(
        "definition: imported range is the exported name",
        start_line(response) == position(UTIL, "export function combine", 16),
        start_line(response),
    )

    response = session.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": position(MAIN, "Math.abs", 2),
    })
    report.check("definition: std import answers no location",
                 locations(response) == [], response.get("result"))

    response = session.request("textDocument/definition", {
        "textDocument": {"uri": main_uri},
        "position": {"line": 0, "character": 0},
    })
    report.check("definition: keyword answers no location",
                 locations(response) == [], response.get("result"))

    # The definition target is not open: the server must read it from disk to
    # render the range (the hook's readFile path).
    report.check("definition: target file exists on disk",
                 os.path.isfile(util_path), util_path)


def check_symbols(report, session, main_uri, util_uri, util_path):
    response = session.request("textDocument/documentSymbol", {
        "textDocument": {"uri": main_uri},
    })
    symbols = response.get("result")
    if not isinstance(symbols, list):
        report.check("documentSymbol: returns a list", False, symbols)
        return
    by_name = {entry.get("name"): entry for entry in symbols
               if isinstance(entry, dict)}
    report.check(
        "documentSymbol: declarations listed",
        all(name in by_name for name in
            ("s", "n", "add", "total", "m", "p", "text", "up")),
        sorted(by_name),
    )
    report.check(
        "documentSymbol: kinds match (function 12, const 14, let 13)",
        by_name.get("add", {}).get("kind") == 12
        and by_name.get("s", {}).get("kind") == 14
        and by_name.get("n", {}).get("kind") == 13,
        {name: by_name.get(name, {}).get("kind") for name in ("add", "s", "n")},
    )

    session.notify("textDocument/didOpen", {
        "textDocument": {
            "uri": util_uri,
            "languageId": "cp3",
            "version": 1,
            "text": UTIL,
        },
    })
    response = session.request("workspace/symbol", {"query": "combine"})
    symbols = response.get("result")
    found = [entry for entry in symbols if isinstance(entry, dict)] \
        if isinstance(symbols, list) else []
    report.check(
        "workspaceSymbol: open-file declaration found",
        len(found) == 1
        and found[0].get("name") == "combine"
        and found[0].get("location", {}).get("uri", "").lower().endswith(
            "intel_util.cp3"),
        found,
    )
    response = session.request("workspace/symbol", {"query": "zzzz-no-symbol"})
    report.check("workspaceSymbol: unknown query answers an empty list",
                 response.get("result") == [], response.get("result"))
    report.check("workspaceSymbol: fixture on disk", os.path.isfile(util_path),
                 util_path)


def check_signature(report, session, main_uri):
    response = session.request("textDocument/signatureHelp", {
        "textDocument": {"uri": main_uri},
        "position": position(MAIN, "add(n, 3)", 4),
    })
    result = response.get("result")
    signature = {}
    if isinstance(result, dict) and isinstance(result.get("signatures"), list):
        if result["signatures"]:
            signature = result["signatures"][0]
    report.check(
        "signatureHelp: function label is the callee type",
        "(number, number) => number" in signature.get("label", ""),
        signature,
    )
    parameters = signature.get("parameters")
    report.check(
        "signatureHelp: two number parameters",
        isinstance(parameters, list) and len(parameters) == 2
        and parameters[0].get("label") == "number"
        and parameters[1].get("label") == "number",
        parameters,
    )
    report.check("signatureHelp: first parameter active",
                 result.get("activeParameter") == 0 if isinstance(result, dict)
                 else False, result)

    response = session.request("textDocument/signatureHelp", {
        "textDocument": {"uri": main_uri},
        "position": position(MAIN, "add(n, 3)", 7),
    })
    result = response.get("result")
    report.check(
        "signatureHelp: second parameter active after the comma",
        isinstance(result, dict) and result.get("activeParameter") == 1,
        result,
    )

    response = session.request("textDocument/signatureHelp", {
        "textDocument": {"uri": main_uri},
        "position": position(MAIN, "const s", 6),
    })
    report.check("signatureHelp: outside a call answers null",
                 response.get("result") is None, response.get("result"))


def check_formatting(report, session, cp3c, work, evidence):
    path = write_fixture(work, "intel_format.cp3", MESSY)
    uri = file_uri(path)
    session.notify("textDocument/didOpen", {
        "textDocument": {
            "uri": uri,
            "languageId": "cp3",
            "version": 1,
            "text": MESSY,
        },
    })
    response = session.request("textDocument/formatting", {
        "textDocument": {"uri": uri},
        "options": {"tabSize": 2, "insertSpaces": True},
    })
    edits = response.get("result")
    report.check(
        "formatting: one whole-document edit",
        isinstance(edits, list) and len(edits) == 1
        and isinstance(edits[0], dict)
        and isinstance(edits[0].get("newText"), str),
        edits,
    )
    new_text = edits[0].get("newText", "") if isinstance(edits, list) and edits \
        else ""

    fmt = subprocess.run([cp3c, "fmt", path], capture_output=True, timeout=120)
    with open(path, "rb") as handle:
        on_disk = handle.read()
    report.check("formatting: cp3c fmt succeeded", fmt.returncode == 0,
                 fmt.stderr.decode("utf-8", "replace"))
    report.check(
        "formatting: LSP bytes equal cp3c fmt bytes",
        on_disk == new_text.encode("utf-8"),
        "lsp=%r disk=%r" % (new_text, on_disk),
    )

    session.notify("textDocument/didChange", {
        "textDocument": {"uri": uri, "version": 2},
        "contentChanges": [{"text": on_disk.decode("utf-8")}],
    })
    response = session.request("textDocument/formatting", {
        "textDocument": {"uri": uri},
        "options": {"tabSize": 2, "insertSpaces": True},
    })
    report.check("formatting: formatted document yields no edits",
                 response.get("result") == [], response.get("result"))

    broken = os.path.join(work, "intel_broken_string.cp3")
    fmt = subprocess.run([cp3c, "fmt", broken], capture_output=True,
                         timeout=120)
    report.check("formatting: cp3c fmt refuses lexical errors",
                 fmt.returncode != 0, fmt.returncode)
    session.notify("textDocument/didOpen", {
        "textDocument": {
            "uri": file_uri(broken),
            "languageId": "cp3",
            "version": 1,
            "text": BROKEN_STRING,
        },
    })
    response = session.request("textDocument/formatting", {
        "textDocument": {"uri": file_uri(broken)},
        "options": {"tabSize": 2, "insertSpaces": True},
    })
    report.check("formatting: lexical-error document yields no edits",
                 response.get("result") == [], response.get("result"))
    with open(os.path.join(evidence, "formatted.cp3"), "wb") as handle:
        handle.write(on_disk)


def hover_ok(result):
    return result is None or (
        isinstance(result, dict) and isinstance(result.get("contents"), dict))


def completion_ok(result):
    return isinstance(result, dict) and isinstance(result.get("items"), list)


def definition_ok(result):
    return result is None or isinstance(result, list)


def check_sweep(report, session, main_uri, text):
    lines = text.split("\n")
    line_count = text.count("\n") + (0 if text.endswith("\n") else 1)
    positions = []
    for line in range(line_count):
        content = lines[line] if line < len(lines) else ""
        positions.append({"line": line, "character": 0})
        positions.append({"line": line, "character": len(content)})
    methods = (
        ("textDocument/hover", hover_ok),
        ("textDocument/completion", completion_ok),
        ("textDocument/definition", definition_ok),
    )
    for method, accept in methods:
        failures = []
        for spot in positions:
            response = session.request(method, {
                "textDocument": {"uri": main_uri},
                "position": spot,
            })
            if "error" in response or not accept(response.get("result")):
                failures.append((spot, response.get("error"),
                                 response.get("result")))
        report.check(
            "sweep: %s well-formed at %d line boundaries"
            % (method.rsplit("/", 1)[-1], len(positions)),
            not failures,
            failures[:3],
        )
    return len(positions)


# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp3c", required=True, help="path to cp3c.exe")
    parser.add_argument("--evidence", default=None,
                        help="evidence directory (default: build/scratch/"
                             "rounds/evidence/lsp-intel)")
    args = parser.parse_args()

    cp3c = os.path.abspath(args.cp3c)
    evidence = os.path.abspath(
        args.evidence
        or os.path.join(REPO, "build", "scratch", "rounds", "evidence",
                        "lsp-intel"))
    work = os.path.join(evidence, "work")
    os.makedirs(evidence, exist_ok=True)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    report = Report(os.path.join(evidence, "intel-output.txt"))

    paths = {
        "main": write_fixture(work, "intel_main.cp3", MAIN),
        "member": write_fixture(work, "intel_member.cp3", MEMBER),
        "object_member": write_fixture(work, "intel_object_member.cp3", OBJECT_MEMBER),
        "object_partial": write_fixture(work, "intel_object_partial.cp3",
                                         OBJECT_PARTIAL_MEMBER),
        "object_shadow": write_fixture(work, "intel_object_shadow.cp3",
                                        OBJECT_SHADOW),
        "reserved_identifier": write_fixture(work, "intel_reserved_identifier.cp3",
                                              RESERVED_IDENTIFIER),
        "assignment_diagnostics": write_fixture(
            work, "intel_assignment_diagnostics.cp3", ASSIGNMENT_DIAGNOSTICS),
        "callback_diagnostics": write_fixture(
            work, "intel_callback_diagnostics.cp3", CALLBACK_DIAGNOSTICS),
        "network_server": write_fixture(work, "intel_network_server.cp3",
                                         NETWORK_SERVER_MEMBER),
        "network_request": write_fixture(work, "intel_network_request.cp3",
                                          NETWORK_REQUEST_MEMBER),
        "network_response": write_fixture(work, "intel_network_response.cp3",
                                           NETWORK_RESPONSE_MEMBER),
        "util": write_fixture(work, "intel_util.cp3", UTIL),
        "import": write_fixture(work, "intel_import.cp3", IMPORT_MAIN),
        "broken_dot": write_fixture(work, "intel_broken_dot.cp3", BROKEN_DOT),
        "broken_string": write_fixture(work, "intel_broken_string.cp3",
                                       BROKEN_STRING),
        "stdlib_partial": write_fixture(work, "intel_stdlib_partial.cp3",
                                        STDLIB_PARTIAL),
        "stdlib_type_partial": write_fixture(work,
                                             "intel_stdlib_type_partial.cp3",
                                             STDLIB_TYPE_PARTIAL),
        "console_event_string": write_fixture(
            work, "intel_console_event_string.cp3", CONSOLE_EVENT_STRING),
        "emitter_string_argument": write_fixture(
            work, "intel_emitter_string_argument.cp3", EMITTER_STRING_ARGUMENT),
        "emitter_member": write_fixture(
            work, "intel_emitter_member.cp3", EMITTER_MEMBER),
        "spec": write_fixture(work, "intel_import_spec.cp3", IMPORT_SPEC),
        "local": write_fixture(work, "intel_local_spec.cp3", LOCAL_SPEC),
        "scope": write_fixture(work, "intel_scope.cp3", SCOPE),
        "try": write_fixture(work, "intel_try.cp3", TRY_BLOCK),
        "notry": write_fixture(work, "intel_notry.cp3", NO_TRY),
        "partial": write_fixture(work, "intel_partial.cp3", PARTIAL_STATEMENT),
        "class": write_fixture(work, "intel_class.cp3", CLASS_BODY),
        "param": write_fixture(work, "intel_param.cp3", PARAM_LIST),
        "type": write_fixture(work, "intel_type.cp3", TYPE_POS),
        "nested": write_fixture(work, "intel_nested.cp3", NESTED),
        "documented": write_fixture(work, "intel_documented.cp3", DOCUMENTED),
        "std": write_fixture(work, "intel_std_docs.cp3", DOC_STD),
    }
    uris = {key: file_uri(path) for key, path in paths.items()}

    print("cp3c: %s" % cp3c)
    print("evidence: %s" % evidence)
    session = Session(cp3c, work)
    try:
        init = session.request("initialize", {
            "processId": None,
            "rootUri": file_uri(work),
            "capabilities": {},
        })
        check_capabilities(report, init)

        for key, text in (("main", MAIN), ("member", MEMBER),
                          ("object_member", OBJECT_MEMBER),
                          ("object_partial", OBJECT_PARTIAL_MEMBER),
                          ("object_shadow", OBJECT_SHADOW),
                          ("reserved_identifier", RESERVED_IDENTIFIER),
                          ("assignment_diagnostics", ASSIGNMENT_DIAGNOSTICS),
                          ("callback_diagnostics", CALLBACK_DIAGNOSTICS),
                          ("network_server", NETWORK_SERVER_MEMBER),
                          ("network_request", NETWORK_REQUEST_MEMBER),
                          ("network_response", NETWORK_RESPONSE_MEMBER),
                          ("import", IMPORT_MAIN), ("broken_dot", BROKEN_DOT),
                          ("broken_string", BROKEN_STRING), ("spec", IMPORT_SPEC),
        ("stdlib_partial", STDLIB_PARTIAL),
        ("stdlib_type_partial", STDLIB_TYPE_PARTIAL),
                          ("console_event_string", CONSOLE_EVENT_STRING),
                          ("emitter_string_argument", EMITTER_STRING_ARGUMENT),
                          ("emitter_member", EMITTER_MEMBER),
                          ("local", LOCAL_SPEC), ("scope", SCOPE),
                          ("try", TRY_BLOCK), ("notry", NO_TRY),
                          ("partial", PARTIAL_STATEMENT),
                          ("class", CLASS_BODY), ("param", PARAM_LIST),
                          ("type", TYPE_POS), ("nested", NESTED),
                          ("documented", DOCUMENTED), ("std", DOC_STD)):
            session.notify("textDocument/didOpen", {
                "textDocument": {
                    "uri": uris[key],
                    "languageId": "cp3",
                    "version": 1,
                    "text": text,
                },
            })
        # Barrier: lets the coalesced diagnostics drain before the checks.
        session.request("textDocument/hover", {
            "textDocument": {"uri": uris["main"]},
            "position": {"line": 0, "character": 0},
        })

        check_hover(report, session, uris["main"])
        check_language_diagnostics(report, session, uris["object_shadow"],
                                   uris["reserved_identifier"],
                                   uris["assignment_diagnostics"],
                                   uris["callback_diagnostics"])
        check_completion(report, session, uris["member"],
                         uris["object_member"], uris["object_partial"],
                         uris["network_server"], uris["network_request"],
                         uris["network_response"], uris["broken_dot"],
                         uris["broken_string"], uris["spec"], uris["local"],
                         uris["main"], uris["stdlib_partial"],
                         uris["stdlib_type_partial"])
        check_semantic_completion(report, session,
                                  uris["console_event_string"],
                                  uris["emitter_string_argument"],
                                  uris["emitter_member"])
        check_context_completion(report, session, uris["scope"], uris["try"],
                                 uris["notry"], uris["class"], uris["param"],
                                 uris["type"], uris["nested"], uris["partial"])
        check_definition(report, session, uris["main"], uris["import"],
                         paths["util"])
        check_symbols(report, session, uris["main"], uris["util"],
                      paths["util"])
        check_signature(report, session, uris["main"])
        check_documentation(report, session, uris["documented"], uris["std"])
        check_formatting(report, session, cp3c, work, evidence)
        check_sweep(report, session, uris["main"], MAIN)
        check_doc_comment_output(report, cp3c, work)

        # Liveness after the broken documents and the sweep.
        response = session.request("textDocument/hover", {
            "textDocument": {"uri": uris["main"]},
            "position": position(MAIN, "const s", 6),
        })
        report.check("server: still answers after broken files and sweep",
                     "s: string" in hover_value(response), response.get("result"))

        shutdown, exit_code = session.shutdown()
        report.check("shutdown: null result", shutdown.get("result") is None,
                     shutdown.get("result"))
        report.check("exit: clean process exit", exit_code == 0, exit_code)
    except Exception as exc:
        report.check("driver: no exception", False, repr(exc))
        try:
            session.proc.kill()
        except Exception:
            pass
    finally:
        transcript_path = os.path.join(evidence, "intel-transcript.txt")
        with open(transcript_path, "wb") as handle:
            for direction, payload in list(session.transcript):
                marker = b">>> SENT " if direction == "sent" else b"<<< RECV "
                handle.write(marker + str(len(payload)).encode("ascii")
                             + b" bytes\n")
                handle.write(payload)
                handle.write(b"\n---\n")
        with open(os.path.join(evidence, "intel-stderr.txt"), "w",
                  encoding="utf-8", newline="\n") as handle:
            handle.write("".join(session.stderr_lines))
        ok = report.finish()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
