#!/usr/bin/env python3
# =============================================================================
# benchmarks/runner/server_load.py
#
# One load-generator process for the server rows. The suite starts several of
# these so the multicore row is driven by more than one client OS process (a
# single Python process with many threads is often client-bound before an
# eight- or twenty-loop server is).
#
# Each thread owns one persistent keep-alive HTTP/1.1 connection and loops
# until the shared deadline. Responses are parsed at the socket level (status,
# Content-Length or chunked body) so the client is not the bottleneck: the
# stdlib http.client round trip is several times more expensive than the raw
# keep-alive exchange these servers produce. The process prints one JSON object
# with its counters; the suite aggregates the processes and divides by the
# fixed window.
#
# Python 3 stdlib only.
# =============================================================================
from __future__ import annotations

import argparse
import http.client
import json
import socket
import threading
import time

def make_request(path: str) -> bytes:
    return (f"GET {path} HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: keep-alive\r\n"
            "\r\n").encode("ascii")


def recv_more(sock: socket.socket, buffer: bytearray) -> None:
    chunk = sock.recv(65536)
    if not chunk:
        raise ConnectionError("server closed the connection")
    buffer.extend(chunk)


def read_response(sock: socket.socket, buffer: bytearray) -> int:
    while b"\r\n\r\n" not in buffer:
        recv_more(sock, buffer)
    head_end = buffer.index(b"\r\n\r\n")
    head = bytes(buffer[:head_end])
    del buffer[:head_end + 4]

    lines = head.split(b"\r\n")
    status = int(lines[0].split()[1])
    headers: dict[bytes, bytes] = {}
    for line in lines[1:]:
        key, _, value = line.partition(b":")
        headers[key.strip().lower()] = value.strip()

    if b"content-length" in headers:
        length = int(headers[b"content-length"])
        while len(buffer) < length:
            recv_more(sock, buffer)
        del buffer[:length]
    elif headers.get(b"transfer-encoding", b"").lower() == b"chunked":
        while True:
            while b"\r\n" not in buffer:
                recv_more(sock, buffer)
            size_line = bytes(buffer[:buffer.index(b"\r\n")])
            del buffer[:buffer.index(b"\r\n") + 2]
            size = int(size_line.split(b";")[0], 16)
            if size == 0:
                while b"\r\n" not in buffer:
                    recv_more(sock, buffer)
                del buffer[:buffer.index(b"\r\n") + 2]
                break
            while len(buffer) < size + 2:
                recv_more(sock, buffer)
            del buffer[:size + 2]
    return status


class Session:
    def __init__(self, port: int) -> None:
        self.port = port
        self.sock: socket.socket | None = None
        self.buffer = bytearray()
        self._connect()

    def _connect(self) -> None:
        self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=5.0)
        self.sock.settimeout(5.0)
        self.buffer.clear()

    def get(self, request: bytes) -> int:
        assert self.sock is not None
        try:
            self.sock.sendall(request)
            return read_response(self.sock, self.buffer)
        except (OSError, ValueError, ConnectionError):
            self.close()
            self._connect()
            raise

    def close(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None


class HttpSession:
    """The established http.client probe, used by the single-core row."""

    def __init__(self, port: int) -> None:
        self.port = port
        self.conn: http.client.HTTPConnection | None = None
        self._connect()

    def _connect(self) -> None:
        self.conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5.0)

    def get(self, path: str) -> int:
        assert self.conn is not None
        try:
            self.conn.request("GET", path)
            response = self.conn.getresponse()
            response.read()
            return response.status
        except (OSError, http.client.HTTPException):
            self.close()
            self._connect()
            raise

    def close(self) -> None:
        if self.conn is not None:
            self.conn.close()
            self.conn = None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--concurrency", type=int, required=True)
    parser.add_argument("--start-at", type=float, required=True,
                        help="Unix epoch second at which every worker starts")
    parser.add_argument("--path", default="/healthz")
    parser.add_argument("--client", choices=("raw", "http"), default="raw",
                        help="raw keep-alive parser (multicore row) or http.client (single row)")
    args = parser.parse_args()

    request = make_request(args.path)
    delay = args.start_at - time.time()
    if delay > 0:
        time.sleep(delay)
    deadline = time.perf_counter() + args.seconds
    counts = {"ok": 0, "err": 0}
    lock = threading.Lock()

    def worker() -> None:
        local_ok = local_err = 0
        raw = None if args.client == "http" else Session(args.port)
        http_session = HttpSession(args.port) if args.client == "http" else None
        while time.perf_counter() < deadline:
            try:
                status = (http_session.get(args.path) if http_session is not None
                          else raw.get(request))
                if status == 200:
                    local_ok += 1
                else:
                    local_err += 1
            except (OSError, ValueError, ConnectionError,
                    http.client.HTTPException):
                local_err += 1
        if raw is not None:
            raw.close()
        if http_session is not None:
            http_session.close()
        with lock:
            counts["ok"] += local_ok
            counts["err"] += local_err

    threads = [threading.Thread(target=worker) for _ in range(args.concurrency)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    print(json.dumps(counts), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
