#!/usr/bin/env python3
"""End-to-end check for `cp3c dap` (tests/dap/dap_check.py).

Drives the debug adapter the way an editor does: Content-Length framed JSON
over stdio, one gdb session, .cp3 breakpoints/frames/locals, stepping, pause,
continue-to-exit, and process cleanup. Standard library only.

Exit codes: 0 pass, 1 failure, 3 skipped (gdb is not on PATH).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FIXTURE = os.path.join(HERE, "dap_fixture.cp3")
EXE_NAME = "dap_fixture.exe"


def fail(message):
    print("FAIL: " + message)
    raise SystemExit(1)


def check(condition, what, detail=""):
    if condition:
        print("ok   " + what)
        return
    fail(what + (": " + detail if detail else ""))


def task_rows(filters):
    if os.name != "nt":
        return []
    result = subprocess.run(["tasklist"] + filters + ["/FO", "CSV", "/NH"],
                            capture_output=True, text=True, errors="replace")
    return [line for line in (result.stdout or "").splitlines()
            if line.startswith('"')]


def process_running(image):
    if os.name == "nt":
        return bool(task_rows(["/FI", "IMAGENAME eq " + image]))
    result = subprocess.run(["pgrep", "-f", image], capture_output=True,
                            text=True, errors="replace")
    return (result.stdout or "").strip() != ""


def pid_running(pid):
    if os.name == "nt":
        needle = '"%d"' % pid
        return any(row.split(",")[1] == needle for row in task_rows(["/FI", "PID eq %d" % pid])
                   if "," in row)
    result = subprocess.run(["ps", "-p", str(pid)], capture_output=True,
                            text=True, errors="replace")
    return result.returncode == 0


class Adapter:
    """The `cp3c dap --stdio` child plus its framed-message plumbing."""

    def __init__(self, cp3c, cwd):
        self.proc = subprocess.Popen(
            [cp3c, "dap", "--stdio"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, cwd=cwd)
        self.seq = 0
        self.messages = []      # everything, in arrival order
        self.cursor = 0         # how far wait() has consumed
        self.lock = threading.Condition()
        self.stderr_lines = []
        self.out_reader = threading.Thread(target=self._read_stdout, daemon=True)
        self.out_reader.start()
        self.err_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self.err_reader.start()

    def _read_stdout(self):
        stream = self.proc.stdout
        try:
            while True:
                headers = {}
                while True:
                    line = stream.readline()
                    if not line:
                        return
                    text = line.decode("utf-8", "replace").strip()
                    if not text:
                        break
                    if ":" in text:
                        key, value = text.split(":", 1)
                        headers[key.strip().lower()] = value.strip()
                length = int(headers.get("content-length", "0"))
                body = stream.read(length)
                if not body or len(body) != length:
                    return
                self._append(json.loads(body.decode("utf-8")))
        except Exception:
            return
        finally:
            self._append({"type": "__closed__"})

    def _append(self, message):
        with self.lock:
            self.messages.append(message)
            self.lock.notify_all()

    def _read_stderr(self):
        for raw in iter(self.proc.stderr.readline, b""):
            self.stderr_lines.append(raw.decode("utf-8", "replace").rstrip())

    def send(self, command, arguments=None):
        self.seq += 1
        message = {"seq": self.seq, "type": "request", "command": command}
        if arguments is not None:
            message["arguments"] = arguments
        body = json.dumps(message).encode("utf-8")
        self.proc.stdin.write(
            ("Content-Length: %d\r\n\r\n" % len(body)).encode("ascii") + body)
        self.proc.stdin.flush()
        return self.seq

    def wait(self, predicate, what, timeout):
        deadline = time.time() + timeout
        with self.lock:
            while True:
                while self.cursor < len(self.messages):
                    message = self.messages[self.cursor]
                    self.cursor += 1
                    if message.get("type") == "__closed__":
                        fail("adapter closed its stdout while waiting for " + what)
                    if predicate(message):
                        return message
                remaining = deadline - time.time()
                if remaining <= 0:
                    fail("timeout waiting for " + what)
                self.lock.wait(remaining)

    def request(self, command, arguments=None, timeout=30.0):
        seq = self.send(command, arguments)
        return self.wait(lambda m: m.get("type") == "response" and
                         m.get("request_seq") == seq,
                         "response to " + command, timeout)

    def wait_event(self, name, timeout):
        return self.wait(lambda m: m.get("type") == "event" and
                         m.get("event") == name, "event " + name, timeout)

    def gdb_pid(self):
        for line in self.stderr_lines:
            match = re.search(r"gdb pid (\d+)", line)
            if match:
                return int(match.group(1))
        return None

    def close(self):
        if self.proc.poll() is None:
            try:
                self.proc.stdin.close()
            except OSError:
                pass
            try:
                self.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=15)
        self.out_reader.join(timeout=5)
        self.err_reader.join(timeout=5)


def fixture_frame(frames):
    for frame in frames:
        path = frame.get("source", {}).get("path", "")
        if path.replace("\\", "/").endswith("dap_fixture.cp3"):
            return frame
    return None


def scope_reference(response, name):
    for scope in response.get("body", {}).get("scopes", []):
        if scope.get("name") == name:
            return scope.get("variablesReference")
    return None


def variables_of(adapter, reference):
    response = adapter.request("variables", {"variablesReference": reference}, 60)
    if response.get("success") is not True:
        fail("variables failed: " + str(response))
    return {variable["name"]: variable["value"]
            for variable in response["body"]["variables"]}


def run_session(adapter, timeout):
    # --- handshake -------------------------------------------------------
    response = adapter.request("initialize", {
        "adapterID": "cp3", "linesStartAt1": True, "pathFormat": "path",
        "clientID": "dap_check"}, 30)
    check(response.get("success") is True, "initialize succeeds", str(response))
    check(response.get("body", {}).get("supportsConfigurationDoneRequest") is True,
          "configurationDone capability is advertised")
    adapter.wait_event("initialized", 15)
    check(True, "initialized event arrives")

    # --- negative: a breakpoint before any session is a clear refusal ---
    response = adapter.request("setBreakpoints", {
        "source": {"path": FIXTURE}, "breakpoints": [{"line": 5}]}, 30)
    entry = response.get("body", {}).get("breakpoints", [{}])[0]
    check(response.get("success") is True and entry.get("verified") is False and
          entry.get("message"), "breakpoint before launch is unverified", str(entry))

    # --- negative: unknown requests are answered, not ignored ------------
    response = adapter.request("evaluate", {"expression": "1 + 1"}, 30)
    check(response.get("success") is False, "unsupported request is rejected",
          str(response))

    # --- negative: launching a missing program fails with a message ------
    missing = os.path.join(HERE, "no_such_program.cp3")
    response = adapter.request("launch", {"program": missing, "cwd": REPO}, 120)
    message = response.get("message", "")
    check(response.get("success") is False and "no_such_program" in message,
          "launch of a missing program fails with a message", str(response))

    # --- negative: a program that does not compile fails cleanly ---------
    # (the adapter forwards the compiler diagnostics as `output` events and
    # answers the launch with an error; it never starts gdb for this file)
    broken = os.path.join(HERE, "dap_broken.cp3")
    response = adapter.request("launch", {"program": broken, "cwd": REPO}, 120)
    message = response.get("message", "")
    check(response.get("success") is False and "build failed:" in message,
          "launch of a broken program reports the compiler error",
          str(response))

    # --- the real launch: the adapter builds with the Debug profile ------
    response = adapter.request("launch", {"program": FIXTURE, "cwd": REPO},
                               timeout)
    check(response.get("success") is True, "launch builds and starts a session",
          str(response))

    response = adapter.request("setBreakpoints", {
        "source": {"path": FIXTURE},
        "breakpoints": [{"line": 5}, {"line": 99999}]}, 60)
    entries = response.get("body", {}).get("breakpoints", [])
    check(len(entries) == 2 and entries[0].get("verified") is True,
          "breakpoint on line 5 resolves in the .cp3 file", str(entries))
    check(entries[1].get("verified") is False and entries[1].get("message"),
          "breakpoint on line 99999 is reported unverified", str(entries))

    response = adapter.request("configurationDone", {}, 30)
    check(response.get("success") is True, "configurationDone succeeds",
          str(response))

    # --- pause while the fixture's busy loop runs ------------------------
    time.sleep(1.0)
    response = adapter.request("pause", {}, 30)
    check(response.get("success") is True, "pause is accepted while running",
          str(response))
    stopped = adapter.wait_event("stopped", 60)
    body = stopped.get("body", {})
    check(body.get("reason") == "pause", "pause produces a pause stop", str(body))
    thread_id = body.get("threadId")
    check(isinstance(thread_id, int), "stop names a thread", str(body))

    response = adapter.request("threads", {}, 30)
    threads = response.get("body", {}).get("threads", [])
    check(any(thread.get("id") == thread_id for thread in threads),
          "threads lists the stopped thread", str(threads))

    response = adapter.request("stackTrace", {"threadId": thread_id}, 60)
    frames = response.get("body", {}).get("stackFrames", [])
    paused = fixture_frame(frames)
    check(paused is not None, "pause stack contains a .cp3 frame",
          str(frames[:3]))

    # --- continue: the loop ends and the line-5 breakpoint hits ----------
    response = adapter.request("continue", {"threadId": thread_id}, 30)
    check(response.get("success") is True, "continue after pause succeeds",
          str(response))
    adapter.wait_event("continued", 30)
    check(True, "continued event arrives")
    stopped = adapter.wait_event("stopped", timeout)
    body = stopped.get("body", {})
    check(body.get("reason") == "breakpoint", "breakpoint stop after the loop",
          str(body))
    hit = body.get("hitBreakpointIds", [])
    check(bool(hit), "breakpoint stop carries its id", str(body))
    thread_id = body.get("threadId")

    response = adapter.request("stackTrace", {"threadId": thread_id}, 60)
    frames = response.get("body", {}).get("stackFrames", [])
    check(bool(frames), "breakpoint stack is not empty", str(response))
    top = frames[0]
    check("add" in top.get("name", ""), "top frame is the add fast path",
          str(top))
    check(top.get("line") == 5, "top frame is at line 5", str(top))
    check(fixture_frame(frames) is not None, "stack is .cp3-native", str(frames[:3]))

    response = adapter.request("scopes", {"frameId": top["id"]}, 60)
    locals_ref = scope_reference(response, "Locals")
    arguments_ref = scope_reference(response, "Arguments")
    check(locals_ref and arguments_ref, "scopes expose Locals and Arguments",
          str(response.get("body", {})))

    locals_now = variables_of(adapter, locals_ref)
    check(locals_now.get("x") == "42", "locals show x = 42", str(locals_now))
    arguments_now = variables_of(adapter, arguments_ref)
    check(arguments_now.get("a") == "1" and arguments_now.get("b") == "2",
          "arguments show a = 1, b = 2", str(arguments_now))

    # --- step one line: y becomes visible --------------------------------
    response = adapter.request("next", {"threadId": thread_id}, 30)
    check(response.get("success") is True, "next succeeds", str(response))
    stopped = adapter.wait_event("stopped", 60)
    check(stopped.get("body", {}).get("reason") == "step",
          "next produces a step stop", str(stopped.get("body", {})))
    response = adapter.request("stackTrace", {"threadId": thread_id}, 60)
    frames = response.get("body", {}).get("stackFrames", [])
    check(bool(frames), "step stack is not empty", str(response))
    top = frames[0]
    check(top.get("line") == 6, "next moved to line 6", str(top))
    response = adapter.request("scopes", {"frameId": top["id"]}, 60)
    locals_now = variables_of(adapter, scope_reference(response, "Locals"))
    check(locals_now.get("y") == "3", "locals show y = 3 after the step",
          str(locals_now))

    # --- run to the end --------------------------------------------------
    response = adapter.request("continue", {"threadId": thread_id}, 30)
    check(response.get("success") is True, "continue to exit succeeds",
          str(response))
    exited = adapter.wait_event("exited", 120)
    check(exited.get("body", {}).get("exitCode") == 0, "program exits with 0",
          str(exited))
    adapter.wait_event("terminated", 30)
    check(True, "terminated event arrives")
    outputs = [message.get("body", {}).get("output", "")
               for message in adapter.messages
               if message.get("event") == "output"]
    check(any("45" in text for text in outputs),
          "console.log output is forwarded", "".join(outputs))

    # --- clean shutdown --------------------------------------------------
    response = adapter.request("disconnect", {}, 30)
    check(response.get("success") is True, "disconnect succeeds", str(response))
    code = adapter.proc.wait(timeout=30)
    check(code == 0, "adapter exits cleanly", str(code))


def run_checks(cp3c, timeout):
    print("cp3c    : " + cp3c)
    print("gdb     : " + str(shutil.which("gdb")))
    print("fixture : " + FIXTURE)
    adapter = Adapter(cp3c, REPO)
    try:
        run_session(adapter, timeout)
        adapter.out_reader.join(timeout=5)
        adapter.err_reader.join(timeout=5)
        gdb_pid = adapter.gdb_pid()
        check(gdb_pid is not None, "adapter logged the gdb pid to stderr",
              "\n".join(adapter.stderr_lines))
        check(not process_running(EXE_NAME), "no " + EXE_NAME + " left behind")
        check(not pid_running(gdb_pid), "gdb process " + str(gdb_pid) + " is gone")
    finally:
        adapter.close()


def main():
    parser = argparse.ArgumentParser(description="cp3c dap end-to-end check")
    parser.add_argument("--cp3c", default=os.path.join(
        REPO, "build", "scratch", "rounds", "build", "bin", "cp3c.exe"),
        help="path to the cp3c executable under test")
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="seconds allowed for the launch build and each wait")
    args = parser.parse_args()

    if shutil.which("gdb") is None:
        print("SKIP: gdb is not on PATH; install MinGW-w64 (WinLibs) to run "
              "tests/dap/dap_check.py")
        return 3
    if not os.path.isfile(args.cp3c):
        fail("cp3c not found at " + args.cp3c + " (pass --cp3c)")
    if not os.path.isfile(FIXTURE):
        fail("fixture not found at " + FIXTURE)

    run_checks(os.path.abspath(args.cp3c), args.timeout)
    print("PASS: cp3c dap end-to-end check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
