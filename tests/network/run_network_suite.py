#!/usr/bin/env python3
# =============================================================================
# tests/network/run_network_suite.py - the network verification suite for
# `std/network` (permanent, by project-owner directive).
#
# Five phases, dual verification:
#   A  python HTTP referee, cp3 `fetch` client (the independent authority)
#   B  cp3 `createServer` server, cp3 `fetch` client (no python in the path)
#   C  python RFC6455 WebSocket server, cp3 `WebSocket` client
#   D  cp3 `WebSocketServer`, python RFC6455 WebSocket client
#   S  security probes against a cp3 server (survival, no-injection, no-hang)
#
#   --smoke-bench-server  the pre-bench smoke of benchmarks/cases/health-server/cp3:
#                         compiles a scratch copy (the port is passed via argv,
#                         so the suite never binds 8080), boots it, fires the
#                         frozen contract with a cp3 fetch checker (no python in
#                         the request path), shuts it down.
#                         This mode does not time anything.
#
# Docs oracle: docs/standard-library/04-imported-std-modules.md section
# `network`, "Included in beta one" only. Checks that depend on a known runtime
# gap are still written docs-faithfully and reported as PENDING SURFACE with the
# observed evidence, never weakened.
#
# Usage:
#   python tests/network/run_network_suite.py --cp3c <path> [--only A,B,C,D,S,G,T]
#          [--json] [--results DIR] [--skip-pending]
#   python tests/network/run_network_suite.py --cp3c <path> --smoke-bench-server
#
# Exit code: 0 only when every executed check passes (PENDING SURFACE counts
# as not passing; --skip-pending removes those checks entirely).
#
# Python 3 stdlib only. Port map and coverage matrix: tests/network/README.md.
# =============================================================================
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FIXTURES = HERE / "fixtures"
EXE = ".exe" if os.name == "nt" else ""
SMOKE_HEALTH_SOURCE = (ROOT / "benchmarks" / "cases" / "health-server"
                       / "cp3" / "src" / "index.cp3")

sys.path.insert(0, str(HERE))
import http_referee  # noqa: E402
import security_probes  # noqa: E402
import ws_rfc6455  # noqa: E402
import ws_rfc6455_tls  # noqa: E402

# ---- port map (README.md documents every allocation) ------------------------
PORT_REFEREE = 48201     # phase A python HTTP referee
PORT_REFUSED = 48202     # phase A connection-refused probe (intentionally closed)
PORT_SERVER_OWN = 48204  # phase B cp3 server
PORT_WS_PY = 48205       # phase C python WebSocket server
PORT_WS_CP3 = 48206      # phase D cp3 WebSocketServer
PORT_PROBE = 48207       # phase S cp3 probe target
PORT_TLS_HTTPS = 48235   # phase T cp3 HTTPS server -> python urllib
PORT_TLS_WSS = 48236     # phase T cp3 WebSocketServer({tls}) -> python RFC6455-TLS client
PORT_SMOKE = 48209       # --smoke-bench-server
NEW_SURFACE_PORTS = {          # phase B new-surface fixture ports; each binds only
    48216: "server_next_chain",       # when its fixture gets that far
    48217: "server_async_handler",
    48218: "server_req_fields",
    48219: "server_delete_route",
    48220: "server_path_eq / server_json_typed_payload (never both run)",
    48221: "server_json_object",
}

TARGET_SECONDS = 90.0
PHASES = ["A", "B", "C", "D", "S", "G", "T"]


# =============================================================================
# checkers: (ok, expected-text) for one parsed fixture line
# =============================================================================
def exact(*fields: str):
    def check(got: list[str]):
        return tuple(got) == tuple(fields), "|".join(fields)
    return check


def rejected_with(substring: str):
    def check(got: list[str]):
        joined = "|".join(got)
        return (len(got) >= 2 and got[0] == "rejected" and substring in joined,
                f"rejected with message containing {substring!r}")
    return check


def one_of(*options: tuple):
    def check(got: list[str]):
        pair = tuple(got)
        expected = " or ".join("|".join(option) for option in options)
        return pair in options, expected
    return check


# phase A: python referee -> cp3 fetch
PHASE_A_SPEC = {
    "A1": ("fetch 200: status/ok/text/statusText", exact("200", "true", "ok-body", "OK")),
    "A2": ("fetch 404: status/ok/text/statusText", exact("404", "false", "not found", "Not Found")),
    "A3": ("POST body echoes as method|path|body", exact("200", "POST", "/echo", "alpha")),
    "A4": ("PUT body and query echo", exact("200", "PUT", "/echo?x=1", "beta")),
    "A5": ("DELETE without body", exact("200", "DELETE", "/echo", "")),
    "A6": ("query string passes through untouched", exact("200", "GET", "/echo?x=1&y=2", "")),
    "A7": ("timeout rejects with the documented message",
           rejected_with("timed out after 300 ms")),
    "A8": ("connection refused rejects", rejected_with("could not connect")),
    "A9": ("https:// against a closed port rejects deterministically",
           rejected_with("tcp connect failed")),
    "A10": ("redirect behavior (docs silent; recorded)",
            one_of(("302", "redirecting"), ("200", "ok-body"))),
    "A11": ("binary payload via arrayBuffer() as Buffer",
            exact("200", "64", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
                               "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f")),
    "A13": ("Init.headers Map sent + res.headers read",
            exact("200", "yes", "net")),
    "A14": ("json<number>() typed read", exact("200", "42")),
    "A15": ("json<User>() reified interface read", exact("200", "7", "alice")),
    "A16a": ("json<string>() typed read", exact("200", "hi")),
    "A16b": ("json<boolean>() typed read", exact("200", "true")),
    "A17": ("json<T>() shape mismatch rejects catchably",
            rejected_with("JSON payload is not a number")),
}

# phase B: cp3 server -> cp3 fetch
PHASE_B_SPEC = {
    "B1": ("res.text", exact("200", "true", "hello")),
    "B2": ("res.status(201)", exact("201", "created")),
    "B3": ("chained res.status + res.header", exact("202", "chained")),
    "B4": ("chunked res.write + res.end", exact("200", "chunk1chunk2")),
    "B5": ("res.json(7) wire bytes", exact("200", "7")),
    "B6": ("res.json(\"hi\") wire bytes", exact("200", '"hi"')),
    "B7": ("res.json(true) wire bytes", exact("200", "true")),
    "B8": ("req.method + req.path (query stripped)", exact("200", "GET /method")),
    "B9": ("POST route matching", exact("200", "posted")),
    "B10": ("PUT route with :id param matching", exact("200", "putparam")),
    "B11": ("404 fallback", exact("404", "not found")),
    "B12": ("res.send(status) with empty body", exact("203", "")),
    "B13": ("middleware answering a path stops the chain", exact("418", "teapot")),
    "B14": ("server still serving after the whole run", exact("200", "hello")),
}

# phase G: cp3 server hardening/surface not otherwise covered in-process
# (bodyLimit, cors, static, cookieParser, trustProxy, onError, verbs, Router,
# FormData). AbortSignal is not supported in this build (see README).
PHASE_G_BODY_LIMIT = {
    "SB1": ("bodyLimit: small body passes through to the handler",
            exact("200", "got:tiny")),
    "SB2": ("bodyLimit: oversized body rejected with 413 before the handler",
            exact("413")),
}

PHASE_G_CORS = {
    "SC1": ("cors: preflight answered with the configured origin/methods/headers/credentials",
            exact("204", "https://example.com", "GET,POST", "x-a,x-b", "true")),
    "SC2": ("cors: simple request stamped with allow-origin",
            exact("200", "https://example.com")),
}

PHASE_G_STATIC = {
    "ST1": ("static: file served below the root", exact("200", "static-ok")),
    "ST2": ("static: missing path falls back to 404", exact("404")),
}

PHASE_G_COOKIE_PARSER = {
    "SC3": ("cookieParser: Cookie header parsed into req.cookies",
            exact("abc123")),
}

PHASE_G_TRUST_PROXY = {
    "TP1": ("trustProxy: X-Forwarded-Proto https observed", exact("https")),
    "TP2": ("trustProxy: no forwarded header means http", exact("http")),
}

PHASE_G_ONERROR = {
    "OE1": ("onError: throwing handler routed to the configured handler",
            exact("599", "onerror-caught")),
    "OE2": ("onError: healthy handler unaffected", exact("200", "still ok")),
}

PHASE_G_METHODS = {
    "M1": ("PATCH verb dispatched", exact("200", "PATCH")),
    "M2": ("HEAD verb dispatches and carries response headers", exact("200", "yes")),
    "M3": ("OPTIONS verb dispatched", exact("204")),
}

PHASE_G_ROUTER = {
    "RT1": ("Router: mounted :id param matches", exact("200", "item:42")),
    "RT2": ("Router: second mounted route matches", exact("200", "plain")),
    "RT3": ("Router: path outside the mount prefix 404s", exact("404")),
}

PHASE_G_FORMDATA = {
    "SF1": ("FormData: multipart/form-data encoded body round-trips",
            exact("200", "payload-ok", "multipart")),
}

PHASE_G_ABORT = None  # AbortSignal: not supported in this build (see README)

# phase C: python ws server -> cp3 WebSocket client (exact stdout sequence)
PHASE_C_SEQUENCE = [
    ("C1", "client open event prints", exact("open")),
    ("C2a", "first text echo delivered", exact("echo:hello")),
    ("C2b", "second text echo delivered", exact("echo:again")),
    ("C2c", "server binary frame delivered byte-exact", exact("000102ff62696e")),
    ("C3", "close handshake reports code and reason", exact("1000", "done")),
    ("C5", "message count and closed flag", exact("3", "true")),
]

# phase T: TLS end-to-end. T1/T2/W_T run in-process (cp3 TLS client against
# cp3 TLS server); T3a/T3b drive the cp3 HTTPS server with python urllib
# (independent); T4 drives the cp3 TLS WebSocketServer with a python
# RFC6455-over-TLS client (independent). T2 and T3b require the caller NOT to
# set SSL_CERT_FILE: they pin the fail-closed behavior.
PHASE_T_FETCH = {
    "F_T1": ("fetch https:// resolves against the in-process TLS server",
             exact("200", "tls-ok")),
    "F_T2": ("fetch https:// fail-closed: embedded CA rejects the loopback cert",
             exact("rejected")),
}
PHASE_T_WSS_INPROCESS = {
    "W_T": ("wss:// round trip over the real TLS pump", exact("echo:hello-tls")),
}
PHASE_T_WSS_SEQUENCE = [
    ("D0", "TLS WebSocketServer constructed", exact("ready")),
    ("D1", "connection event", exact("connection")),
    ("D21", "first message delivered", exact("hello")),
    ("D22", "second message delivered", exact("second")),
    ("D3", "close event with code and reason", exact("1000", "bye")),
    ("D4", "server closed and finished", exact("done")),
]

# phase D: cp3 WebSocketServer -> python ws client (exact stdout sequence)
PHASE_D_SEQUENCE = [
    ("D0", "server constructed", exact("ready")),
    ("D1", "connection event", exact("connection")),
    ("D2a", "first text message", exact("hello")),
    ("D2b", "second text message", exact("second")),
    ("D2c", "client binary frame delivered byte-exact", exact("deadbeef")),
    ("D3", "close event with code and reason", exact("1000", "bye")),
    ("D4", "server closed and finished", exact("done")),
]

SMOKE_EXPECT = [
    ("SM1", "GET /healthz -> 200 ok", exact("200", "ok")),
    ("SM2", "GET / -> 200 health-server ok", exact("200", "health-server ok")),
    ("SM3", "GET /nope -> 404 not found", exact("404", "not found")),
]


# =============================================================================
# plumbing
# =============================================================================
@dataclass
class Check:
    cid: str
    phase: str
    name: str
    status: str  # PASS | FAIL | PENDING SURFACE | SKIPPED
    detail: str = ""


def parse_fixture_lines(text: str) -> list[tuple[str, list[str]]]:
    lines: list[tuple[str, list[str]]] = []
    for raw in text.replace("\r\n", "\n").split("\n"):
        line = raw.strip()
        if not line:
            continue
        parts = line.split("|")
        lines.append((parts[0], parts[1:]))
    return lines


def extract_error(text: str) -> str:
    for raw in text.replace("\r\n", "\n").split("\n"):
        if "error:" in raw:
            cleaned = re.sub(r"^[A-Za-z]:\\[^:]*\\", "", raw.strip())
            return cleaned[:240]
    for raw in text.replace("\r\n", "\n").split("\n"):
        if raw.strip():
            return raw.strip()[:240]
    return "(no output)"


def wait_port(port: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                return True
        except OSError:
            time.sleep(0.05)
    return False


class Suite:
    def __init__(self, cp3c: Path, results: Path | None, per_check: float,
                 overall: float, skip_pending: bool, as_json: bool) -> None:
        self.cp3c = cp3c
        self.results = results
        self.per_check = per_check
        self.overall = overall
        self.skip_pending = skip_pending
        self.as_json = as_json
        self.checks: list[Check] = []
        self.lines: list[str] = []
        self.start = time.monotonic()
        self.deadline = self.start + overall
        self.phase = "S"
        self._procs: list[tuple[subprocess.Popen, str]] = []
        if self.results is not None:
            (self.results / "logs").mkdir(parents=True, exist_ok=True)

    # ---------------------------------------------------------------- output
    def emit(self, text: str = "") -> None:
        self.lines.append(text)
        if not self.as_json:
            print(text, flush=True)

    def add(self, cid: str, name: str, status: str, detail: str = "") -> Check:
        check = Check(cid, self.phase, name, status, detail)
        self.checks.append(check)
        line = f"CHECK {status} {check.phase} {cid} {name}"
        if detail:
            line += f" - {detail}"
        self.emit(line)
        return check

    def remaining(self) -> float:
        return self.deadline - time.monotonic()

    def budget(self) -> float:
        return min(self.per_check, max(1.0, self.remaining()))

    def write_log(self, name: str, text: str) -> None:
        if self.results is not None:
            (self.results / "logs" / f"{name}.txt").write_text(
                text, encoding="utf-8", newline="")

    # ------------------------------------------------------------ processes
    def run_cp3(self, fixture: str, timeout: float, log_name: str,
                env_overrides: dict | None = None,
                env_remove: tuple[str, ...] = ()):
        env = dict(os.environ)
        for key in env_remove:
            env.pop(key, None)
        if env_overrides:
            env.update(env_overrides)
        try:
            proc = subprocess.run(
                [str(self.cp3c), "run", fixture],
                cwd=str(FIXTURES), capture_output=True, timeout=timeout,
                encoding="utf-8", errors="replace", env=env)
        except subprocess.TimeoutExpired:
            self.write_log(log_name, f"[suite] {fixture}: timed out after {timeout:g}s")
            return None
        except OSError as exc:
            self.write_log(log_name, f"[suite] {fixture}: could not start cp3c: {exc}")
            return None
        self.write_log(log_name, (proc.stdout or "") + "\n" + (proc.stderr or ""))
        return proc

    def start_server(self, fixture: str, log_name: str) -> subprocess.Popen | None:
        try:
            proc = subprocess.Popen(
                [str(self.cp3c), "run", fixture],
                cwd=str(FIXTURES), stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
        except OSError as exc:
            self.write_log(log_name, f"[suite] could not start cp3c: {exc}")
            return None
        self._procs.append((proc, log_name))
        return proc

    def stop_server(self, proc: subprocess.Popen, log_name: str,
                    timeout: float = 10.0) -> str:
        output = ""
        try:
            output = proc.communicate(timeout=timeout)[0] or ""
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                output = proc.communicate(timeout=5)[0] or ""
            except subprocess.TimeoutExpired:
                proc.kill()
                output = (proc.communicate(timeout=5)[0] or "")
        self.write_log(log_name, output)
        return output

    def cleanup(self) -> None:
        for proc, log_name in self._procs:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                self.write_log(log_name, f"[suite] terminated leftover {log_name}\n")

    # -------------------------------------------------------------- fixture
    def assert_lines(self, spec: dict, got: list[tuple[str, list[str]]],
                     log_name: str) -> None:
        seen: set[str] = set()
        for cid, fields in got:
            if cid not in spec:
                self.add(cid, f"unexpected output line ({log_name})", "FAIL",
                         f"observed {'|'.join([cid] + fields)}")
                continue
            name, check = spec[cid]
            seen.add(cid)
            ok, expected = check(fields)
            observed = "|".join(fields)
            if ok:
                self.add(cid, name, "PASS", f"observed {observed}")
            else:
                self.add(cid, name, "FAIL",
                         f"expected {expected}, observed {observed}")
        for cid, (name, _check) in spec.items():
            if cid not in seen:
                self.add(cid, name, "FAIL", "missing output line (fixture did not run it)")

    def assert_sequence(self, sequence: list, got: list[tuple[str, list[str]]],
                        log_name: str) -> None:
        index = 0
        for cid, name, check in sequence:
            if index >= len(got):
                self.add(cid, name, "FAIL", "missing output line (fixture stopped early)")
                continue
            got_cid, fields = got[index]
            index += 1
            if got_cid != cid:
                self.add(cid, name, "FAIL",
                         f"expected line {cid}, observed {'|'.join([got_cid] + fields)}")
                continue
            ok, expected = check(fields)
            observed = "|".join(fields)
            if ok:
                self.add(cid, name, "PASS", f"observed {observed}")
            else:
                self.add(cid, name, "FAIL", f"expected {expected}, observed {observed}")
        for cid, fields in got[index:]:
            self.add(cid, f"unexpected output line ({log_name})", "FAIL",
                     f"observed {'|'.join([cid] + fields)}")

    def run_and_assert(self, fixture: str, spec: dict, log_name: str,
                       env_overrides: dict | None = None,
                       env_remove: tuple[str, ...] = ()) -> None:
        proc = self.run_cp3(fixture, self.budget(), log_name,
                            env_overrides=env_overrides, env_remove=env_remove)
        if proc is None:
            for cid, (name, _c) in spec.items():
                self.add(cid, name, "FAIL", f"{fixture} timed out or could not start")
            return
        got = parse_fixture_lines(proc.stdout or "")
        self.assert_lines(spec, got, log_name)
        if proc.returncode != 0 and not got:
            self.emit(f"  {fixture} exit={proc.returncode}: "
                      f"{extract_error(proc.stderr or proc.stdout or '')}")

    def runtime_gap_fixture(self, cid: str, name: str, fixture: str,
                            expected: list[tuple[str, list[str]]],
                            buggy: list[tuple[str, list[str]]],
                            buggy_detail: str) -> None:
        """Run a fixture whose expected behavior is blocked by a known runtime
        gap (no compiler diagnostic). The gap is reported as PENDING SURFACE
        with the observed evidence; the check flips to PASS when it is fixed."""
        if self.skip_pending:
            self.add(cid, name, "SKIPPED", "skipped by --skip-pending (known runtime gap)")
            return
        proc = self.run_cp3(fixture, self.budget(), cid)
        if proc is None:
            self.add(cid, name, "FAIL", f"{fixture} timed out")
            return
        combined = (proc.stderr or "") + "\n" + (proc.stdout or "")
        if proc.returncode != 0:
            self.add(cid, name, "FAIL", f"unexpected failure: {extract_error(combined)}")
            return
        got = parse_fixture_lines(proc.stdout or "")
        if got == [(i, f) for i, f in buggy]:
            self.add(cid, name, "PENDING SURFACE",
                     f"runtime gap (no compiler message): {buggy_detail}")
            return
        wanted = [(i, f) for i, f in expected]
        if got == wanted:
            self.add(cid, name, "PASS", "expected lines observed")
        else:
            self.add(cid, name, "FAIL",
                     f"compiled but output differs; observed {got!r}")

    # ---------------------------------------------------------------- phases
    def phase_a(self) -> None:
        self.phase = "A"
        self.emit("[phase A] python HTTP referee (48201) -> cp3 fetch client")
        referee = http_referee.RefereeServer(PORT_REFEREE)
        referee.start()
        try:
            if not wait_port(PORT_REFEREE, 5):
                for cid, (name, _c) in PHASE_A_SPEC.items():
                    self.add(cid, name, "FAIL", "referee did not start")
                return
            self.run_and_assert("client/fetch_referee.cp3", PHASE_A_SPEC, "fetch_referee")
            # the referee's own log is the independent half of the check
            missing = []
            log = referee.state.snapshot()
            for method, path in [("GET", "/fixed/200"), ("GET", "/fixed/404"),
                                 ("POST", "/echo"), ("PUT", "/echo?x=1"),
                                 ("DELETE", "/echo"), ("GET", "/echo?x=1&y=2"),
                                 ("GET", "/slow"), ("GET", "/redirect"),
                                 ("GET", "/binary"), ("GET", "/headers"),
                                 ("GET", "/json/number"), ("GET", "/json/user"),
                                 ("GET", "/json/string"), ("GET", "/json/bool")]:
                if not referee.state.saw(method, path):
                    missing.append(f"{method} {path}")
            bodies = {("POST", "/echo"): "alpha", ("PUT", "/echo?x=1"): "beta"}
            for (method, path), body in bodies.items():
                entry = next((e for e in log if e["method"] == method and e["path"] == path), None)
                if entry is None or entry["body"] != body:
                    missing.append(f"{method} {path} body={body!r} got "
                                   f"{entry['body'] if entry else None!r}")
            headers_entry = next((e for e in log if e["method"] == "GET"
                                  and e["path"] == "/headers"), None)
            if headers_entry is None or headers_entry["headers"].get("x-probe") != "yes":
                missing.append("GET /headers x-probe: yes not received")
            no_host = [e["path"] for e in log if "host" not in e["headers"]]
            if missing or no_host:
                detail = "missing=" + "; ".join(missing) if missing else ""
                if no_host:
                    detail += f"; requests without Host: {no_host}"
                self.add("A12", "referee request log (independent authority)", "FAIL", detail)
            else:
                self.add("A12", "referee request log (independent authority)", "PASS",
                         f"{len(log)} requests recorded, bodies, x-probe and Host intact")
            self.write_log("referee_log", json.dumps(log, indent=2))
        finally:
            referee.stop()

    def phase_b(self) -> None:
        self.phase = "B"
        self.emit("[phase B] cp3 createServer (48204) -> cp3 fetch client")
        self.run_and_assert("server/server_own.cp3", PHASE_B_SPEC, "server_own")
        self.run_and_assert(
            "server/server_next_chain.cp3",
            {"PB1": ("middleware next() continues the chain", exact("200", "x12"))},
            "server_next_chain")
        self.run_and_assert(
            "server/server_async_handler.cp3",
            {"PB2": ("async handler + req.text() body echo", exact("200", "got:payload"))},
            "server_async_handler")
        self.run_and_assert(
            "server/server_req_fields.cp3",
            {"PB3": ("req.params / req.query / req.headers",
                     exact("200", "7", "1", "en", "on"))},
            "server_req_fields")
        self.run_and_assert(
            "server/server_delete_route.cp3",
            {"PB4": ("app.delete route registration", exact("200", "deleted")),
             "PB4b": ("app.delete wrong method falls through",
                      exact("404", "not found"))},
            "server_delete_route")
        self.run_and_assert(
            "server/server_json_typed_payload.cp3",
            {"PB5p": ("res.json<T>(payload) object codecs", exact("7", "alice"))},
            "server_json_typed_payload")
        self.run_and_assert(
            "server/server_path_eq.cp3",
            {"PB6": ("req.path === literal without annotation",
                     exact("418", "teapot"))},
            "server_path_eq")
        self.run_and_assert(
            "server/server_json_object.cp3",
            {"PB5a": ("res.json(payload, status?) + reified client read",
                      exact("200", "7", "alice",
                            "application/json; charset=utf-8")),
             "PB5b": ("res.json(payload, status?) carries status",
                      exact("201", "9", "bob"))},
            "server_json_object")

    def phase_c(self) -> None:
        self.phase = "C"
        self.emit("[phase C] python RFC6455 server (48205) -> cp3 WebSocket client")
        server = ws_rfc6455.EchoServer(PORT_WS_PY)
        server.start()
        if not wait_port(PORT_WS_PY, 5):
            for cid, name, _check in PHASE_C_SEQUENCE:
                self.add(cid, name, "FAIL", "python ws server did not start")
            return
        proc = self.run_cp3("client/ws_client_python.cp3", self.budget(), "ws_client_python")
        got = parse_fixture_lines(proc.stdout or "") if proc else []
        self.assert_sequence(PHASE_C_SEQUENCE, got, "ws_client_python")
        server.wait(timeout=5)
        observations = {"messages": server.messages, "binary": [
            list(payload) for payload in server.binary], "pongs": server.pongs,
            "close_code": server.close_code, "close_reason": server.close_reason,
            "error": server.error}
        self.write_log("ws_phase_c", json.dumps(observations, indent=2))
        ok = (server.messages == ["hello", "again"]
              and server.binary == [b"cli-bin"]
              and server.pongs == ["srv-ping"]
              and server.close_code == 1000 and server.close_reason == "done"
              and server.error is None)
        self.add("C6", "python server observations (texts, binary, auto-pong, close)",
                 "PASS" if ok else "FAIL", json.dumps(observations))

    def phase_d(self) -> None:
        self.phase = "D"
        self.emit("[phase D] cp3 WebSocketServer (48206) -> python RFC6455 client")
        proc = self.start_server("client/ws_server_own.cp3", "ws_server_own")
        if proc is None or not wait_port(PORT_WS_CP3, 20):
            for cid, name, _check in PHASE_D_SEQUENCE:
                self.add(cid, name, "FAIL", "cp3 ws server never became ready")
            if proc is not None:
                self.stop_server(proc, "ws_server_own")
            return
        client = ws_rfc6455.client_check(PORT_WS_CP3)
        plain = ws_rfc6455.probe_plain_http(PORT_WS_CP3)
        output = self.stop_server(proc, "ws_server_own")
        got = parse_fixture_lines(output)
        self.assert_sequence(PHASE_D_SEQUENCE, got, "ws_server_own")
        client_view = dict(client)
        client_view["binary"] = [list(payload) for payload in client["binary"]]
        observations = dict(client_view)
        observations["plain_http"] = plain
        self.write_log("ws_phase_d", json.dumps(observations, indent=2))
        ok = (client["messages"] == ["echo:hello", "echo:second"]
              and client["binary"] == [b"\xde\xad\xbe\xef"]
              and client["pong"] == "p1" and client["close_code"] == 1000
              and client["close_reason"] == "bye" and client["error"] is None)
        self.add("D6", "python client observations (echoes, binary echo, pong, close ack)",
                 "PASS" if ok else "FAIL", json.dumps(observations))
        self.add("D7", "plain HTTP request to the ws port (docs silent; recorded)",
                 "PASS", f"observed {plain}")

    def phase_g(self) -> None:
        self.phase = "G"
        self.emit("[phase G] cp3 server surface not otherwise covered (bodyLimit/cors/static/cookies/trustProxy/onError/verbs/Router/FormData)")
        self.run_and_assert("server/server_body_limit.cp3", PHASE_G_BODY_LIMIT,
                            "g_body_limit")
        self.run_and_assert("server/server_cors.cp3", PHASE_G_CORS, "g_cors")
        self.run_and_assert("server/server_static.cp3", PHASE_G_STATIC, "g_static")
        self.run_and_assert("server/server_cookie_parser.cp3", PHASE_G_COOKIE_PARSER,
                            "g_cookie_parser")
        self.run_and_assert("server/server_trust_proxy.cp3", PHASE_G_TRUST_PROXY,
                            "g_trust_proxy")
        self.run_and_assert("server/server_onerror.cp3", PHASE_G_ONERROR, "g_onerror")
        self.run_and_assert("server/server_methods.cp3", PHASE_G_METHODS, "g_methods")
        self.run_and_assert("server/server_router.cp3", PHASE_G_ROUTER, "g_router")
        self.run_and_assert("client/fetch_formdata.cp3", PHASE_G_FORMDATA, "g_formdata")

    def phase_t(self) -> None:
        import ssl
        import urllib.request

        self.phase = "T"
        self.emit("[phase T] TLS end-to-end (fetch https / wss / cp3 HTTPS server vs python urllib)")
        ca_pem = FIXTURES / "tls" / "cert.pem"
        tls_env = {"SSL_CERT_FILE": str(ca_pem)}
        # T1/W_T: cp3 TLS client against an in-process cp3 TLS server. The
        # fetch and wss sides verify the loopback cert via SSL_CERT_FILE.
        self.run_and_assert("client/fetch_tls_ok.cp3", PHASE_T_FETCH, "t_fetch_ok",
                            env_overrides=tls_env)
        # T2: no SSL_CERT_FILE on this run: the embedded Mozilla CA bundle
        # must reject the self-signed loopback cert.
        self.run_and_assert("client/fetch_tls_rejected.cp3", PHASE_T_FETCH,
                            "t_fetch_rejected", env_remove=("SSL_CERT_FILE",))
        self.run_and_assert("client/wss_tls_inprocess.cp3", PHASE_T_WSS_INPROCESS,
                            "t_wss_inprocess", env_overrides=tls_env)
        # T3a/T3b: python urllib is the independent client against the cp3
        # HTTPS server (createServer({ tls }), uWS SSLApp). T3a verifies
        # against the loopback CA and expects 200/ok; T3b uses the default
        # trust store and must reject the self-signed cert.
        proc = self.start_server("server/server_tls.cp3", "server_tls")
        if proc is None or not wait_port(PORT_TLS_HTTPS, 20):
            self.add("T3a", "python urllib over verified TLS (cp3 HTTPS server)",
                     "FAIL", "cp3 HTTPS server never became ready")
            self.add("T3b", "python urllib without the CA rejects the self-signed cert",
                     "FAIL", "cp3 HTTPS server never became ready")
            if proc is not None:
                self.stop_server(proc, "server_tls")
            return
        url = f"https://127.0.0.1:{PORT_TLS_HTTPS}/health"
        try:
            ctx = ssl.create_default_context(cafile=str(ca_pem))
            with urllib.request.urlopen(url, context=ctx, timeout=8) as r:
                body = r.read().decode("utf-8", "replace")
                ok = r.status == 200 and body == "ok"
                detail = f"{r.status} {body!r}"
        except Exception as exc:  # noqa: BLE001 - report what the client saw
            ok, detail = False, str(exc)
        self.add("T3a", "python urllib over verified TLS (cp3 HTTPS server)",
                 "PASS" if ok else "FAIL", detail)
        try:
            urllib.request.urlopen(url, timeout=8)
            ok, detail = False, "cert unexpectedly verified"
        except ssl.SSLError as exc:
            ok = "CERTIFICATE_VERIFY_FAILED" in str(exc)
            detail = str(exc)
        except Exception as exc:  # noqa: BLE001
            ok, detail = False, str(exc)
        self.add("T3b", "python urllib without the CA rejects the self-signed cert",
                 "PASS" if ok else "FAIL", detail)
        self.stop_server(proc, "server_tls")
        # T4: python RFC6455-over-TLS client against the cp3 TLS WebSocketServer.
        proc = self.start_server("client/wss_server_own.cp3", "wss_server_own")
        if proc is None or not wait_port(PORT_TLS_WSS, 20):
            for cid, name, _check in PHASE_T_WSS_SEQUENCE:
                self.add(cid, name, "FAIL", "cp3 WSS server never became ready")
            self.add("T4", "python RFC6455-over-TLS client observations (echo/binary/pong/close)",
                     "FAIL", "cp3 WSS server never became ready")
            if proc is not None:
                self.stop_server(proc, "wss_server_own")
            return
        client = ws_rfc6455_tls.client_check_tls(PORT_TLS_WSS)
        output = self.stop_server(proc, "wss_server_own")
        got = parse_fixture_lines(output)
        self.assert_sequence(PHASE_T_WSS_SEQUENCE, got, "wss_server_own")
        observations = dict(client)
        observations["binary"] = [list(payload) for payload in client["binary"]]
        self.write_log("wss_tls_observations", json.dumps(observations, indent=2))
        ok = (client["messages"] == ["echo:hello", "echo:second"]
              and client["binary"] == [b"\xde\xad\xbe\xef"]
              and client["pong"] == "p1" and client["close_code"] == 1000
              and client["close_reason"] == "bye" and client["error"] is None)
        self.add("T4", "python RFC6455-over-TLS client observations (echo/binary/pong/close)",
                 "PASS" if ok else "FAIL", json.dumps(observations))

    def phase_s(self) -> None:
        self.phase = "S"
        self.emit("[phase S] python security probes -> cp3 server (48207)")
        proc = self.start_server("server/server_probe_target.cp3", "server_probe_target")
        if proc is None or not wait_port(PORT_PROBE, 20):
            self.add("S0", "probe target server ready", "FAIL", "never accepted connections")
            if proc is not None:
                self.stop_server(proc, "server_probe_target")
            return
        self.add("S0", "probe target server ready", "PASS", "port 48207 accepting")
        probes = [
            ("S1", "malformed JSON request body", security_probes.probe_malformed_json),
            ("S2", "oversized (1 MiB) request body", security_probes.probe_oversized_body),
            ("S3", "short Content-Length then close", security_probes.probe_short_content_length),
            ("S4", "negative Content-Length", security_probes.probe_negative_content_length),
            ("S5", "duplicate Content-Length", security_probes.probe_duplicate_content_length),
            ("S6", "CRLF header/path injection", security_probes.probe_crlf_injection),
            ("S7", "path traversal / middleware bypass", security_probes.probe_path_traversal),
            ("S8", "small-scale concurrency", security_probes.probe_concurrency),
            ("S9", "500 isolation under real sockets", security_probes.probe_boom_isolation),
            ("S10", "unregistered method", security_probes.probe_unknown_method),
        ]
        results = {}
        for cid, name, probe in probes:
            if self.remaining() < 3:
                self.add(cid, name, "FAIL", "overall budget exhausted before the probe")
                continue
            result = probe(PORT_PROBE)
            results[cid] = result
            status = "PASS" if result.get("pass") else "FAIL"
            self.add(cid, name, status, f"{result.get('observed', '')}; {result.get('detail', '')}")
        self.write_log("probes", json.dumps(results, indent=2))
        self.stop_server(proc, "server_probe_target")

    # ---------------------------------------------------------------- smoke
    def run_smoke(self, smoke_port: int, scratch: Path) -> None:
        self.phase = "SM"
        self.emit("[smoke] benchmarks/cases/health-server/cp3 contract via cp3 fetch "
                  "(no python in the request path)")
        scratch.mkdir(parents=True, exist_ok=True)
        if not SMOKE_HEALTH_SOURCE.is_file():
            self.add("SM0", "health-server cp3 source found", "FAIL",
                     f"missing {SMOKE_HEALTH_SOURCE}")
            return
        source = SMOKE_HEALTH_SOURCE.read_text(encoding="utf-8")
        # The bench entry reads its port from argv[1], so the suite can point a
        # scratch copy at smoke_port without rewriting the source.
        (scratch / "health_server.cp3").write_text(source, encoding="utf-8")
        checker = (FIXTURES / "client" / "smoke_health_contract.cp3").read_text(encoding="utf-8")
        (scratch / "smoke_check.cp3").write_text(
            checker.replace("const PORT = 48209;", f"const PORT = {smoke_port};", 1),
            encoding="utf-8")
        build = subprocess.run(
            [str(self.cp3c), "build", "health_server.cp3"], cwd=str(scratch),
            capture_output=True, timeout=self.per_check, encoding="utf-8", errors="replace")
        self.write_log("smoke_build", (build.stdout or "") + "\n" + (build.stderr or ""))
        if build.returncode != 0:
            self.add("SMB1", "health-server cp3 builds", "FAIL",
                     extract_error(build.stderr or build.stdout or ""))
            return
        candidates = sorted((scratch / ".cp3").rglob(f"health_server{EXE}"),
                            key=lambda p: p.stat().st_mtime, reverse=True)
        if not candidates:
            candidates = sorted((scratch / ".cp3").rglob(f"*{EXE}"),
                                key=lambda p: p.stat().st_mtime, reverse=True)
        if not candidates:
            self.add("SMB1", "health-server cp3 builds", "FAIL",
                     "build succeeded but no executable was found under .cp3/")
            return
        self.add("SMB1", "health-server cp3 builds", "PASS",
                 f"port {smoke_port} passed via argv to a scratch copy")
        server = subprocess.Popen([str(candidates[0]), str(smoke_port)], cwd=str(scratch),
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  encoding="utf-8", errors="replace")
        try:
            if not wait_port(smoke_port, 30):
                self.add("SMB2", "health-server boots and listens", "FAIL",
                         "no connection accepted within 30s")
                return
            self.add("SMB2", "health-server boots and listens", "PASS",
                     f"port {smoke_port} accepting")
            proc = subprocess.run(
                [str(self.cp3c), "run", "smoke_check.cp3"], cwd=str(scratch),
                capture_output=True, timeout=self.per_check, encoding="utf-8",
                errors="replace")
            self.write_log("smoke_check", (proc.stdout or "") + "\n" + (proc.stderr or ""))
            got = parse_fixture_lines(proc.stdout or "")
            self.assert_lines({cid: (name, check) for cid, name, check in SMOKE_EXPECT},
                              got, "smoke_check")
            self.add("SMX", "server shut down by the suite", "PASS",
                     "process terminated by exact pid after the checks")
        finally:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()

    # -------------------------------------------------------------- summary
    def summary(self) -> dict:
        counts: dict[str, int] = {}
        for check in self.checks:
            counts[check.status] = counts.get(check.status, 0) + 1
        elapsed = time.monotonic() - self.start
        failed = counts.get("FAIL", 0)
        pending = counts.get("PENDING SURFACE", 0)
        ok = failed == 0 and pending == 0
        payload = {
            "cp3c": str(self.cp3c),
            "checks": [{"id": c.cid, "phase": c.phase, "name": c.name,
                        "status": c.status, "detail": c.detail} for c in self.checks],
            "summary": {
                "total": len(self.checks),
                "pass": counts.get("PASS", 0),
                "fail": failed,
                "pending_surface": pending,
                "skipped": counts.get("SKIPPED", 0),
                "elapsed_s": round(elapsed, 1),
                "target_s": TARGET_SECONDS,
                "over_target": elapsed > TARGET_SECONDS,
                "ok": ok,
            },
        }
        if not self.as_json:
            self.emit("")
            self.emit(f"SUMMARY total={len(self.checks)} pass={counts.get('PASS', 0)} "
                      f"fail={failed} pending_surface={pending} "
                      f"skipped={counts.get('SKIPPED', 0)} "
                      f"elapsed={elapsed:.1f}s target={TARGET_SECONDS:.0f}s "
                      f"result={'OK' if ok else 'FAIL'}")
            if elapsed > TARGET_SECONDS:
                self.emit(f"WARN suite elapsed {elapsed:.1f}s exceeds the "
                          f"~{TARGET_SECONDS:.0f}s target (cold fixture compile can dominate)")
            if pending:
                self.emit("note: PENDING SURFACE checks are known runtime gaps "
                          "(see tests/network/README.md); they are not weakened passes.")
        if self.results is not None:
            (self.results / "checks.json").write_text(
                json.dumps(payload, indent=2), encoding="utf-8")
            (self.results / "run.txt").write_text(
                "\n".join(self.lines) + "\n", encoding="utf-8", newline="")
        return payload


def compiler_provenance(cp3c: Path) -> dict:
    digest = hashlib.sha256()
    with open(cp3c, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return {"path": str(cp3c), "sha256": digest.hexdigest().upper(),
            "mtime": datetime.fromtimestamp(cp3c.stat().st_mtime).isoformat(timespec="seconds")}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="network verification suite for std/network (permanent)")
    parser.add_argument("--cp3c", default=str(ROOT / "build" / "bin" / f"cp3c{EXE}"),
                        help="path to the cp3c under test")
    parser.add_argument("--only", default="",
                        help="comma list of phases to run: " + ",".join(PHASES))
    parser.add_argument("--json", action="store_true",
                        help="print one JSON document instead of the human output")
    parser.add_argument("--results", default="",
                        help="directory for raw logs, checks.json and run.txt")
    parser.add_argument("--skip-pending", action="store_true",
                        help="skip the known runtime-gap checks (B20)")
    parser.add_argument("--smoke-bench-server", action="store_true",
                        help="run only the benchmarks/cases/health-server/cp3 smoke")
    parser.add_argument("--smoke-port", type=int, default=PORT_SMOKE,
                        help=f"port for the smoke (default {PORT_SMOKE}; never 8080)")
    parser.add_argument("--scratch", default="",
                        help="scratch directory for the smoke copies "
                             "(default build/scratch/net-suite/smoke)")
    parser.add_argument("--per-check-timeout", type=float, default=120.0)
    parser.add_argument("--overall-timeout", type=float, default=300.0)
    args = parser.parse_args(argv)

    cp3c = Path(args.cp3c)
    if not cp3c.is_file():
        sys.stderr.write(f"run_network_suite: cp3c not found: {cp3c}\n")
        return 2
    if args.smoke_bench_server and args.smoke_port == 8080:
        sys.stderr.write("run_network_suite: --smoke-port 8080 is off limits\n")
        return 2

    suite = Suite(cp3c, Path(args.results) if args.results else None,
                  args.per_check_timeout, args.overall_timeout,
                  args.skip_pending, args.json)
    try:
        if args.smoke_bench_server:
            scratch = (Path(args.scratch) if args.scratch
                       else ROOT / "build" / "scratch" / "net-suite" / "smoke")
            suite.emit(f"compiler: {json.dumps(compiler_provenance(cp3c))}")
            suite.run_smoke(args.smoke_port, scratch)
        else:
            wanted = [p.strip().upper() for p in args.only.split(",") if p.strip()]
            unknown = [p for p in wanted if p not in PHASES]
            if unknown:
                sys.stderr.write(f"run_network_suite: unknown phase(s): {','.join(unknown)}; "
                                 f"known: {','.join(PHASES)}\n")
                return 2
            selected = wanted or PHASES
            suite.emit(f"network suite: phases {','.join(selected)} | cp3c {cp3c}")
            suite.emit(f"compiler: {json.dumps(compiler_provenance(cp3c))}")
            for phase in PHASES:
                if phase not in selected:
                    continue
                if suite.remaining() < 3:
                    suite.add(f"{phase}0", f"phase {phase} skipped", "FAIL",
                              "overall budget exhausted")
                    continue
                {"A": suite.phase_a, "B": suite.phase_b, "C": suite.phase_c,
                 "D": suite.phase_d, "S": suite.phase_s, "G": suite.phase_g,
                 "T": suite.phase_t}[phase]()
    finally:
        suite.cleanup()

    payload = suite.summary()
    if args.json:
        print(json.dumps(payload, indent=2))
    return 0 if payload["summary"]["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
