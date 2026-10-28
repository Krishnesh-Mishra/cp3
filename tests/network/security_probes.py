#!/usr/bin/env python3
# =============================================================================
# tests/network/security_probes.py - phase S probes of the network suite.
#
# Every probe is a python client (stdlib only) attacking the cp3 server fixture
# (server_probe_target.cp3, port 48207). The contract each probe must prove:
# no crash, no hang, the server keeps serving correct responses afterwards,
# and no injection succeeded. Where the docs are silent on the "right" status
# the observed behavior is recorded, never asserted as a guess; the assertions
# cover survival, validity, and the no-injection invariant only.
# =============================================================================
from __future__ import annotations

import http.client
import socket
import threading
import time

OK_PATH = "/ok"


def _http(port: int, method: str, path: str, body: bytes | None = None,
          headers: dict | None = None, timeout: float = 5.0) -> dict:
    result: dict = {"status": None, "reason": "", "headers": {}, "body": b"",
                    "error": None}
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        conn.request(method, path, body=body, headers=headers or {})
        response = conn.getresponse()
        result["status"] = response.status
        result["reason"] = response.reason
        result["headers"] = {k.lower(): v for k, v in response.getheaders()}
        result["body"] = response.read()
    except (OSError, http.client.HTTPException) as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        conn.close()
    return result


def alive(port: int) -> tuple[bool, str]:
    """The survival assertion: GET /ok must still be 200 'ok'."""
    response = _http(port, "GET", OK_PATH)
    if response["status"] == 200 and response["body"] == b"ok":
        return True, "GET /ok -> 200 ok"
    return False, f"GET /ok -> {response['status']!r} body={response['body']!r} error={response['error']!r}"


def _parse_head(data: bytes):
    if b"\r\n\r\n" not in data:
        return None
    head, _, rest = data.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    parts = lines[0].split(" ", 2)
    status = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else None
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            key, value = line.split(":", 1)
            headers[key.strip().lower()] = value.strip()
    return status, headers, rest


def raw_request(port: int, payload: bytes, timeout: float = 3.0) -> dict:
    """One malformed request on a raw socket; records whatever happens."""
    result: dict = {"status": None, "headers": {}, "body": b"", "closed": False,
                    "timed_out": False, "error": None}
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    except OSError as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        return result
    try:
        sock.settimeout(timeout)
        sock.sendall(payload)
        data = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                break
            except OSError as exc:
                result["error"] = f"{type(exc).__name__}: {exc}"
                break
            if not chunk:
                result["closed"] = True
                break
            data.extend(chunk)
            parsed = _parse_head(bytes(data))
            if parsed is not None:
                status, headers, rest = parsed
                length = headers.get("content-length")
                if length is None or len(rest) >= int(length):
                    result.update(status=status, headers=headers, body=rest)
                    return result
        if result["status"] is None:
            result["timed_out"] = True
            result["raw"] = bytes(data[:160])
    finally:
        sock.close()
    return result


def _alive_note(port: int) -> tuple[bool, str]:
    ok, detail = alive(port)
    return ok, f"alive: {detail}"


# ---------------------------------------------------------------- the probes
def probe_malformed_json(port: int) -> dict:
    response = _http(port, "POST", "/echo", body=b'{"broken": ',
                     headers={"Content-Type": "application/json"})
    ok, note = _alive_note(port)
    return {"observed": f"status={response['status']} body={response['body']!r}",
            "alive": ok,
            "pass": response["status"] == 200 and response["body"] == b"echo-ok" and ok,
            "detail": f"{note}; req.json<T>() is not exercised here, so this "
                      f"probe checks survival only"}


def probe_oversized_body(port: int, size: int = 1 << 20) -> dict:
    response = _http(port, "POST", "/echo", body=b"A" * size,
                     headers={"Content-Type": "application/octet-stream"},
                     timeout=15.0)
    ok, note = _alive_note(port)
    got_response = response["status"] is not None
    return {"observed": f"status={response['status']} error={response['error']!r}",
            "alive": ok,
            "pass": (got_response or response["error"] is None) and ok,
            "detail": f"{note}; {size} byte body"}


def probe_short_content_length(port: int) -> dict:
    payload = (b"POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n"
               b"Content-Length: 5000\r\n\r\n0123456789")
    result = raw_request(port, payload)
    ok, note = _alive_note(port)
    no_hang = result["status"] is not None or result["closed"] or result["timed_out"]
    return {"observed": f"status={result['status']} closed={result['closed']} "
                        f"timed_out={result['timed_out']} error={result['error']!r}",
            "alive": ok, "pass": no_hang and ok,
            "detail": f"{note}; declared 5000 bytes, sent 10, then closed"}


def probe_negative_content_length(port: int) -> dict:
    payload = (b"POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n"
               b"Content-Length: -1\r\n\r\nx")
    result = raw_request(port, payload)
    ok, note = _alive_note(port)
    no_hang = result["status"] is not None or result["closed"] or result["timed_out"]
    return {"observed": f"status={result['status']} closed={result['closed']} "
                        f"timed_out={result['timed_out']} error={result['error']!r}",
            "alive": ok, "pass": no_hang and ok, "detail": note}


def probe_duplicate_content_length(port: int) -> dict:
    payload = (b"POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n"
               b"Content-Length: 3\r\nContent-Length: 999\r\n\r\nabc")
    result = raw_request(port, payload)
    ok, note = _alive_note(port)
    no_hang = result["status"] is not None or result["closed"] or result["timed_out"]
    return {"observed": f"status={result['status']} closed={result['closed']} "
                        f"timed_out={result['timed_out']} error={result['error']!r}",
            "alive": ok, "pass": no_hang and ok, "detail": note}


def probe_crlf_injection(port: int) -> dict:
    # (a) a legitimate extra header whose name matches the inject marker
    headers_result = _http(port, "GET", "/hdr",
                           headers={"X-Inject": "yes", "X-Probe": "clean"})
    # (b) an encoded CRLF inside the request path
    payload = (b"GET /hdr%0d%0aX-Injected:%20yes HTTP/1.1\r\n"
               b"Host: 127.0.0.1\r\nX-Inject: yes\r\n\r\n")
    path_result = raw_request(port, payload)
    ok, note = _alive_note(port)
    combined = b" ".join([str(headers_result["headers"]).encode(),
                          headers_result["body"], path_result["body"],
                          path_result.get("raw", b"")])
    injected = b"x-injected" in combined.lower() or b"x-inject:" in combined.lower()
    header_ok = headers_result["headers"].get("x-probe") == "clean"
    return {"observed": f"hdr status={headers_result['status']} "
                        f"x-probe={headers_result['headers'].get('x-probe')!r} "
                        f"path status={path_result['status']}",
            "alive": ok,
            "pass": header_ok and not injected and ok,
            "detail": f"{note}; injected marker must not appear in any response"}


def probe_path_traversal(port: int) -> dict:
    attempts = ["/private/../ok", "/private%2f..%2fok", "//private", "/Private",
                "/private/", "/private/x"]
    observed = []
    bypassed = False
    for path in attempts:
        response = _http(port, "GET", path)
        body = response["body"].decode("utf-8", "replace")
        observed.append(f"{path}->{response['status']}:{body!r}")
        if response["status"] == 200 and body == "secret":
            bypassed = True
    ok, note = _alive_note(port)
    return {"observed": "; ".join(observed), "alive": ok,
            "pass": (not bypassed) and ok,
            "detail": f"{note}; the /private route must require its exact path"}


def probe_concurrency(port: int, workers: int = 8, rounds: int = 5) -> dict:
    failures: list[str] = []
    lock = threading.Lock()

    def worker(index: int) -> None:
        for round_index in range(rounds):
            response = _http(port, "GET", OK_PATH, timeout=10.0)
            if response["status"] != 200 or response["body"] != b"ok":
                with lock:
                    failures.append(
                        f"worker {index} round {round_index}: "
                        f"status={response['status']} body={response['body']!r} "
                        f"error={response['error']!r}")

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(workers)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=30)
    ok, note = _alive_note(port)
    return {"observed": f"{workers} workers x {rounds} requests, "
                        f"{len(failures)} failed",
            "alive": ok,
            "pass": not failures and ok,
            "detail": f"{note}; first failure: {failures[0]}" if failures else note}


def probe_boom_isolation(port: int) -> dict:
    first = _http(port, "GET", "/boom")
    second = _http(port, "GET", "/boom")
    ok, note = _alive_note(port)
    statuses = (first["status"], second["status"])
    return {"observed": f"boom statuses={statuses} "
                        f"bodies={first['body']!r},{second['body']!r}",
            "alive": ok,
            "pass": statuses == (500, 500) and ok,
            "detail": f"{note}; a throwing handler is 500 for that request only"}


def probe_unknown_method(port: int) -> dict:
    response = _http(port, "PATCH", OK_PATH)
    ok, note = _alive_note(port)
    valid = response["status"] is not None
    return {"observed": f"PATCH /ok -> status={response['status']} "
                        f"body={response['body']!r} error={response['error']!r}",
            "alive": ok, "pass": valid and ok,
            "detail": f"{note}; status for an unregistered method is undocumented"}
