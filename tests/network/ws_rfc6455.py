#!/usr/bin/env python3
# =============================================================================
# tests/network/ws_rfc6455.py - a minimal, strict RFC6455 implementation for
# the network verification suite (python side only).
#
#   * EchoServer: python WebSocket server; the suite's phase C fixture connects
#     with the cp3 `WebSocket` client. Records messages, pongs, close frames.
#   * client_check(): python WebSocket client; drives the suite's phase D
#     fixture (cp3 `WebSocketServer`). Verifies the handshake accept key, sends
#     masked text/ping/close frames, and asserts the echoes/pong/close ack.
#   * probe_plain_http(): records what the cp3 ws server does with a
#     non-upgrade HTTP request (docs silent; recorded, never asserted beyond
#     "no hang").
#
# Deliberately small: no extensions, no fragmentation, payloads under 64 KiB.
# Python 3 stdlib only.
# =============================================================================
from __future__ import annotations

import base64
import hashlib
import os
import socket
import struct
import threading
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC6455 section 1.3
OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class WsProtocolError(RuntimeError):
    pass


def compute_accept(key: str) -> str:
    digest = hashlib.sha1((key + GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


class BufferedSocket:
    """recv() with a pushback buffer and exact reads."""

    def __init__(self, sock: socket.socket) -> None:
        self.sock = sock
        self.buf = bytearray()

    def read_exact(self, count: int) -> bytes:
        while len(self.buf) < count:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise WsProtocolError("connection closed mid-frame")
            self.buf.extend(chunk)
        head = bytes(self.buf[:count])
        del self.buf[:count]
        return head

    def read_until(self, marker: bytes, limit: int = 65536) -> bytes:
        while marker not in self.buf:
            if len(self.buf) > limit:
                raise WsProtocolError("header block over limit")
            chunk = self.sock.recv(65536)
            if not chunk:
                raise WsProtocolError("connection closed during handshake")
            self.buf.extend(chunk)
        index = self.buf.index(marker) + len(marker)
        head = bytes(self.buf[:index])
        del self.buf[:index]
        return head


def encode_frame(opcode: int, payload: bytes, masked: bool) -> bytes:
    head = bytearray()
    head.append(0x80 | opcode)  # FIN + opcode; no fragmentation, no RSV bits
    length = len(payload)
    mask_bit = 0x80 if masked else 0x00
    if length < 126:
        head.append(mask_bit | length)
    elif length < 65536:
        head.append(mask_bit | 126)
        head.extend(struct.pack(">H", length))
    else:
        head.append(mask_bit | 127)
        head.extend(struct.pack(">Q", length))
    if not masked:
        return bytes(head) + payload
    key = os.urandom(4)
    masked_payload = bytes(byte ^ key[i % 4] for i, byte in enumerate(payload))
    return bytes(head) + key + masked_payload


def read_frame(buffered: BufferedSocket) -> tuple[int, bytes]:
    first, second = buffered.read_exact(2)
    if not first & 0x80:
        raise WsProtocolError("fragmented frames are out of scope")
    if first & 0x70:
        raise WsProtocolError("RSV bits set without an extension negotiated")
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack(">H", buffered.read_exact(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", buffered.read_exact(8))[0]
    if length > 1 << 20:
        raise WsProtocolError(f"frame of {length} bytes exceeds the test cap")
    if masked:
        key = buffered.read_exact(4)
        payload = buffered.read_exact(length)
        payload = bytes(byte ^ key[i % 4] for i, byte in enumerate(payload))
    else:
        payload = buffered.read_exact(length)
    return opcode, payload


def close_payload(code: int, reason: str = "") -> bytes:
    return struct.pack(">H", code) + reason.encode("utf-8")


def parse_close(payload: bytes) -> tuple[int, str]:
    if len(payload) < 2:
        return 1005, ""
    code = struct.unpack(">H", payload[:2])[0]
    return code, payload[2:].decode("utf-8", "replace")


def server_handshake(buffered: BufferedSocket) -> dict:
    raw = buffered.read_until(b"\r\n\r\n")
    text = raw.decode("latin-1")
    lines = text.split("\r\n")
    request = lines[0].split(" ")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            key, value = line.split(":", 1)
            headers[key.strip().lower()] = value.strip()
    if len(request) < 3 or request[0] != "GET":
        raise WsProtocolError(f"bad request line: {lines[0]!r}")
    if headers.get("upgrade", "").lower() != "websocket":
        raise WsProtocolError("missing Upgrade: websocket")
    key = headers.get("sec-websocket-key")
    if not key:
        raise WsProtocolError("missing Sec-WebSocket-Key")
    response = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {compute_accept(key)}\r\n"
        "\r\n"
    )
    buffered.sock.sendall(response.encode("ascii"))
    return {"path": request[1], "headers": headers}


def client_handshake(sock: socket.socket, port: int, path: str = "/") -> dict:
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: 127.0.0.1:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(request.encode("ascii"))
    buffered = BufferedSocket(sock)
    raw = buffered.read_until(b"\r\n\r\n")
    text = raw.decode("latin-1")
    lines = text.split("\r\n")
    status = lines[0]
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    if " 101 " not in status:
        raise WsProtocolError(f"expected 101, got: {status!r}")
    expected = compute_accept(key)
    if headers.get("sec-websocket-accept") != expected:
        raise WsProtocolError("Sec-WebSocket-Accept mismatch")
    return {"status": status, "buffered": buffered, "headers": headers}


class EchoServer:
    """Python WebSocket server for phase C; one connection, scripted reactions.

    Sends one ping right after the handshake (the cp3 client must auto-pong),
    echoes every text message as "echo:<text>", sends one binary frame
    (0x00 0x01 0x02 0xff "bin") after the second text echo, records every
    binary frame the client sends, and acknowledges the close.
    """

    def __init__(self, port: int, deadline_s: float = 60.0) -> None:
        self.port = port
        self.deadline_s = deadline_s
        self.messages: list[str] = []
        self.binary: list[bytes] = []
        self.pongs: list[str] = []
        self.close_code: int | None = None
        self.close_reason: str = ""
        self.ping_sent = False
        self.error: str | None = None
        self.connected = False
        self._done = threading.Event()
        self._thread = threading.Thread(target=self._run, name="ws-echo", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def wait(self, timeout: float | None = None) -> bool:
        return self._done.wait(timeout if timeout is not None else self.deadline_s + 2)

    def _run(self) -> None:
        deadline = time.monotonic() + self.deadline_s
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.1", self.port))
            listener.listen(4)
            # Accept-loop: the suite's readiness probe (and any other bare TCP
            # connect) closes before sending a request line; only a connection
            # that completes the RFC6455 handshake becomes the session.
            while time.monotonic() < deadline:
                listener.settimeout(max(0.2, deadline - time.monotonic()))
                try:
                    conn, _ = listener.accept()
                except socket.timeout:
                    break
                conn.settimeout(max(0.2, deadline - time.monotonic()))
                try:
                    with conn:
                        buffered = BufferedSocket(conn)
                        server_handshake(buffered)
                        self.connected = True
                        self._session(conn, buffered)
                        return
                except (OSError, WsProtocolError) as exc:
                    if self.connected:
                        self.error = str(exc)
                        return
                    # not a real client (probe or aborted handshake): keep going
                    continue
            if not self.connected:
                self.error = "no websocket client completed the handshake"
        except OSError as exc:
            self.error = str(exc)
        finally:
            listener.close()
            self._done.set()

    def _session(self, conn: socket.socket, buffered: BufferedSocket) -> None:
        conn.sendall(encode_frame(OP_PING, b"srv-ping", masked=False))
        self.ping_sent = True
        text_count = 0
        while True:
            opcode, payload = read_frame(buffered)
            if opcode == OP_TEXT:
                message = payload.decode("utf-8")
                self.messages.append(message)
                conn.sendall(encode_frame(OP_TEXT, b"echo:" + payload, masked=False))
                text_count += 1
                if text_count == 2:
                    # One server-to-client binary frame; the cp3 side receives
                    # it through the documented `string | Buffer` message slot.
                    conn.sendall(encode_frame(OP_BINARY, b"\x00\x01\x02\xffbin",
                                              masked=False))
            elif opcode == OP_BINARY:
                self.binary.append(payload)
            elif opcode == OP_PING:
                conn.sendall(encode_frame(OP_PONG, payload, masked=False))
            elif opcode == OP_PONG:
                self.pongs.append(payload.decode("utf-8", "replace"))
            elif opcode == OP_CLOSE:
                self.close_code, self.close_reason = parse_close(payload)
                conn.sendall(encode_frame(OP_CLOSE, payload, masked=False))
                return
            elif opcode == OP_CONT:
                raise WsProtocolError("fragmented frames are out of scope")
            else:
                raise WsProtocolError(f"unknown opcode {opcode:#x}")


def client_check(port: int, path: str = "/", timeout_s: float = 8.0) -> dict:
    """Drive a cp3 WebSocketServer: text echo, ping/pong, binary echo, close."""
    result: dict = {"messages": [], "binary": [], "pong": None, "close_code": None,
                    "close_reason": "", "error": None}
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=timeout_s)
        sock.settimeout(timeout_s)
        with sock:
            info = client_handshake(sock, port, path)
            buffered: BufferedSocket = info["buffered"]
            sock.sendall(encode_frame(OP_TEXT, b"hello", masked=True))
            sock.sendall(encode_frame(OP_TEXT, b"second", masked=True))
            sock.sendall(encode_frame(OP_PING, b"p1", masked=True))
            binary_payload = b"\xde\xad\xbe\xef"
            expecting = ["echo:hello", "echo:second"]
            sent_binary = False
            sent_close = False
            deadline = time.monotonic() + timeout_s
            while time.monotonic() < deadline:
                opcode, payload = read_frame(buffered)
                if opcode == OP_TEXT:
                    result["messages"].append(payload.decode("utf-8"))
                    if result["messages"] == expecting and not sent_binary:
                        sock.sendall(encode_frame(OP_BINARY, binary_payload, masked=True))
                        sent_binary = True
                elif opcode == OP_BINARY:
                    result["binary"].append(payload)
                    if payload == binary_payload and not sent_close:
                        sock.sendall(encode_frame(OP_CLOSE,
                                                  close_payload(1000, "bye"),
                                                  masked=True))
                        sent_close = True
                elif opcode == OP_PING:
                    sock.sendall(encode_frame(OP_PONG, payload, masked=True))
                elif opcode == OP_PONG:
                    result["pong"] = payload.decode("utf-8", "replace")
                elif opcode == OP_CLOSE:
                    result["close_code"], result["close_reason"] = parse_close(payload)
                    return result
                if sent_close and result["messages"] != expecting:
                    raise WsProtocolError("unexpected message sequence")
            raise WsProtocolError("no close frame within the deadline")
    except (OSError, WsProtocolError) as exc:
        result["error"] = str(exc)
    return result


def probe_plain_http(port: int, timeout_s: float = 4.0) -> dict:
    """Record what the cp3 ws server does with a non-upgrade HTTP request."""
    result: dict = {"response": None, "closed": False, "error": None}
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=timeout_s)
        sock.settimeout(timeout_s)
        with sock:
            sock.sendall(b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
            try:
                data = sock.recv(4096)
            except socket.timeout:
                result["error"] = "timeout waiting for a response"
                return result
            if not data:
                result["closed"] = True
                return result
            result["response"] = data.decode("latin-1", "replace").split("\r\n")[0]
    except OSError as exc:
        result["error"] = str(exc)
    return result


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="standalone cp3 ws echo server")
    parser.add_argument("--port", type=int, default=48205)
    args = parser.parse_args()
    server = EchoServer(args.port)
    server.start()
    print(f"ws echo listening on ws://127.0.0.1:{args.port}/", flush=True)
    server.wait()
    print(f"messages={server.messages} pongs={server.pongs} "
          f"close={server.close_code} error={server.error}", flush=True)
