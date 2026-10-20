"""health-server-multi (python entry) -- stdlib only, one worker process per CPU.

Run:   python main.py [port]        (default 8080)

Contract, identical in every language entry:
  GET /healthz  -> 200 text/plain "ok"
  GET /         -> 200 text/plain "health-server ok"
  anything else -> 404 text/plain "not found"

The parent creates one listening socket and passes it to every worker process
(multiprocessing duplicates the handle on Windows and inherits it on POSIX).
Each worker runs a ThreadingTCPServer on that shared socket, so accepts and
request handlers run on every core. Binds 127.0.0.1 only.
"""

import multiprocessing
import os
import socket
import socketserver
import sys
from http.server import BaseHTTPRequestHandler


PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/healthz":
            status, body = 200, b"ok"
        elif self.path == "/":
            status, body = 200, b"health-server ok"
        else:
            status, body = 404, b"not found"

        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass  # keep the console quiet; the bench does the talking


class SharedThreadingServer(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, shared_socket):
        super().__init__(
            shared_socket.getsockname(), Handler, bind_and_activate=False
        )
        self.socket.close()
        self.socket = shared_socket
        self.server_address = shared_socket.getsockname()


def serve(shared_socket):
    SharedThreadingServer(shared_socket).serve_forever(poll_interval=0.2)


def main():
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", PORT))
    listener.listen(512)

    workers = max(1, os.cpu_count() or 1)
    processes = []
    for _ in range(workers):
        process = multiprocessing.Process(target=serve, args=(listener,), daemon=True)
        process.start()
        processes.append(process)

    print(f"listening on http://127.0.0.1:{PORT} ({workers} workers)", flush=True)
    try:
        for process in processes:
            process.join()
    except KeyboardInterrupt:
        pass
    finally:
        for process in processes:
            if process.is_alive():
                process.terminate()
        listener.close()


if __name__ == "__main__":
    main()
