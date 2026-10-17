"""health-server (python entry) -- standard library only.

Run:   python main.py [port]        (default 8080)

Contract, identical in every language entry:
  GET /healthz  -> 200 text/plain "ok"
  GET /         -> 200 text/plain "health-server ok"
  anything else -> 404 text/plain "not found"

Binds 127.0.0.1 only. ThreadingHTTPServer so requests are not serialized the
way the plain single-threaded HTTPServer would serialize them.
"""

import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

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


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"listening on http://127.0.0.1:{PORT}", flush=True)
    server.serve_forever()
