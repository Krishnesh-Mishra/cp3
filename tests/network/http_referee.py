#!/usr/bin/env python3
# =============================================================================
# tests/network/http_referee.py - the independent HTTP referee of the network
# verification suite (phase A: python server, cp3 `fetch` client).
#
# A small ThreadingHTTPServer whose endpoints exercise the documented `fetch`
# surface: fixed statuses, method/body/query echo, JSON scalars, a redirect, a
# binary payload, and a deliberately slow endpoint for the timeout check. Every
# request is recorded (method, raw path with query, headers, body) so the
# orchestrator can assert not only what cp3 saw but also what the referee
# actually received.
#
# Python 3 stdlib only, no pip, no downloads.
# =============================================================================
from __future__ import annotations

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SLOW_SECONDS = 2.0
BINARY_PAYLOAD = bytes(range(64))


class RefereeState:
    """Thread-safe request log shared by every handler thread."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.requests: list[dict] = []

    def record(self, method: str, path: str, headers: dict, body: str) -> None:
        with self._lock:
            self.requests.append(
                {"method": method, "path": path, "headers": headers, "body": body}
            )

    def snapshot(self) -> list[dict]:
        with self._lock:
            return [dict(entry) for entry in self.requests]

    def saw(self, method: str, path: str) -> bool:
        return any(
            entry["method"] == method and entry["path"] == path
            for entry in self.snapshot()
        )


class RefereeHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "cp3-net-referee/1.0"

    # Silence the default stderr logging; the orchestrator records everything.
    def log_message(self, fmt: str, *args) -> None:  # noqa: A003 - stdlib name
        pass

    # ------------------------------------------------------------------ verbs
    def do_GET(self) -> None:  # noqa: N802 - stdlib name
        self._handle()

    def do_POST(self) -> None:  # noqa: N802 - stdlib name
        self._handle()

    def do_PUT(self) -> None:  # noqa: N802 - stdlib name
        self._handle()

    def do_DELETE(self) -> None:  # noqa: N802 - stdlib name
        self._handle()

    # -------------------------------------------------------------- plumbing
    def _read_body(self) -> bytes:
        try:
            length = int(self.headers.get("Content-Length", "0") or "0")
        except ValueError:
            length = 0
        return self.rfile.read(length) if length > 0 else b""

    def _respond(self, code: int, body: bytes, content_type: str,
                 extra_headers: dict | None = None) -> None:
        try:
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            for key, value in (extra_headers or {}).items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            # The cp3 timeout check abandons /slow on purpose.
            pass

    def _handle(self) -> None:
        raw_path = self.path
        body = self._read_body()
        headers = {key.lower(): value for key, value in self.headers.items()}
        state: RefereeState = self.server.referee_state  # type: ignore[attr-defined]
        state.record(self.command, raw_path, headers, body.decode("utf-8", "replace"))

        path = raw_path.split("?", 1)[0]
        if path == "/echo":
            text = f"{self.command}|{raw_path}|{body.decode('utf-8', 'replace')}"
            self._respond(200, text.encode("utf-8"), "text/plain; charset=utf-8")
            return
        if path == "/json/echo":
            # Deterministic compact JSON echo: method, raw path and body. The
            # headers the client sent stay in the referee log (A12 asserts
            # them); keeping them out of the body keeps the expected bytes
            # stable across runtimes.
            echo = json.dumps(
                {"method": self.command,
                 "path": raw_path,
                 "body": body.decode("utf-8", "replace")},
                separators=(",", ":"))
            self._respond(200, echo.encode("utf-8"), "application/json")
            return
        if path == "/headers":
            probe = headers.get("x-probe", "-")
            self._respond(200, probe.encode("utf-8"), "text/plain; charset=utf-8",
                          {"X-Referee": "net"})
            return
        if path == "/json/number":
            self._respond(200, b"42", "application/json")
            return
        if path == "/json/bool":
            self._respond(200, b"true", "application/json")
            return
        if path == "/json/string":
            self._respond(200, b'"hi"', "application/json")
            return
        if path == "/json/user":
            self._respond(200, json.dumps({"id": 7, "name": "alice"}).encode(),
                          "application/json")
            return
        if path == "/redirect":
            self._respond(302, b"redirecting", "text/plain; charset=utf-8",
                          {"Location": "/fixed/200"})
            return
        if path == "/slow":
            time.sleep(SLOW_SECONDS)
            self._respond(200, b"slow-ok", "text/plain; charset=utf-8")
            return
        if path == "/binary":
            self._respond(200, BINARY_PAYLOAD, "application/octet-stream")
            return
        if path.startswith("/fixed/"):
            try:
                code = int(path.rsplit("/", 1)[1])
            except ValueError:
                code = 404
            text = {200: "ok-body", 404: "not found"}.get(code, f"status-{code}")
            self._respond(code, text.encode("utf-8"), "text/plain; charset=utf-8")
            return
        self._respond(404, b"not found", "text/plain; charset=utf-8")


class RefereeServer:
    """Owns the ThreadingHTTPServer and its serving thread."""

    def __init__(self, port: int) -> None:
        self.state = RefereeState()
        self._server = ThreadingHTTPServer(("127.0.0.1", port), RefereeHandler)
        self._server.referee_state = self.state  # type: ignore[attr-defined]
        self._server.daemon_threads = True
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._server.serve_forever,
                                        name="referee", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=5)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="standalone cp3 HTTP referee")
    parser.add_argument("--port", type=int, default=48201)
    parser.add_argument("--seconds", type=float, default=30.0,
                        help="serve for this long, then exit")
    args = parser.parse_args()
    server = RefereeServer(args.port)
    server.start()
    print(f"referee listening on http://127.0.0.1:{args.port}", flush=True)
    try:
        time.sleep(args.seconds)
    finally:
        server.stop()
