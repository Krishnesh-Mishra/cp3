#!/usr/bin/env python3
# =============================================================================
# tests/network/ws_rfc6455_tls.py - the phase T (TLS) RFC6455 client.
#
# Identical protocol behavior to ws_rfc6455.client_check, but the connection
# is wrapped in TLS and verified against fixtures/tls/cert.pem (the suite
# passes that path as a CA file; the loopback cert is self-signed, so the
# default trust store would reject it). Used against the cp3 WebSocketServer
# with { tls } (uWS SSLApp) - the independent-client check for the SSLApp
# path.
# =============================================================================
from __future__ import annotations

import os
import socket
import ssl
import time
from pathlib import Path

import ws_rfc6455 as ws

FIXTURES = Path(__file__).parent / "fixtures"
CA_PEM = FIXTURES / "tls" / "cert.pem"


def client_check_tls(port: int, path: str = "/", ca_file: str | None = None,
                     timeout_s: float = 8.0) -> dict:
    """Drive a cp3 TLS WebSocketServer; mirrors client_check over TLS."""
    result: dict = {"messages": [], "binary": [], "pong": None, "close_code": None,
                    "close_reason": "", "error": None}
    try:
        context = ssl.create_default_context(cafile=ca_file or str(CA_PEM))
        raw = socket.create_connection(("127.0.0.1", port), timeout=timeout_s)
        sock = context.wrap_socket(raw, server_hostname="localhost")
        sock.settimeout(timeout_s)
        with sock:
            info = ws.client_handshake(sock, port, path)
            buffered = info["buffered"]
            sock.sendall(ws.encode_frame(ws.OP_TEXT, b"hello", masked=True))
            sock.sendall(ws.encode_frame(ws.OP_TEXT, b"second", masked=True))
            sock.sendall(ws.encode_frame(ws.OP_PING, b"p1", masked=True))
            binary_payload = b"\xde\xad\xbe\xef"
            expecting = ["echo:hello", "echo:second"]
            sent_binary = False
            sent_close = False
            deadline = time.monotonic() + timeout_s
            while time.monotonic() < deadline:
                opcode, payload = ws.read_frame(buffered)
                if opcode == ws.OP_TEXT:
                    result["messages"].append(payload.decode("utf-8"))
                    if result["messages"] == expecting and not sent_binary:
                        sock.sendall(ws.encode_frame(ws.OP_BINARY, binary_payload, masked=True))
                        sent_binary = True
                elif opcode == ws.OP_BINARY:
                    result["binary"].append(payload)
                    if payload == binary_payload and not sent_close:
                        sock.sendall(ws.encode_frame(ws.OP_CLOSE,
                                                     ws.close_payload(1000, "bye"),
                                                     masked=True))
                        sent_close = True
                elif opcode == ws.OP_PING:
                    sock.sendall(ws.encode_frame(ws.OP_PONG, payload, masked=True))
                elif opcode == ws.OP_PONG:
                    result["pong"] = payload.decode("utf-8", "replace")
                elif opcode == ws.OP_CLOSE:
                    result["close_code"], result["close_reason"] = ws.parse_close(payload)
                    return result
                if sent_close and result["messages"] != expecting:
                    raise ws.WsProtocolError("unexpected message sequence")
            raise ws.WsProtocolError("no close frame within the deadline")
    except (OSError, ws.WsProtocolError) as exc:
        result["error"] = str(exc)
    return result
