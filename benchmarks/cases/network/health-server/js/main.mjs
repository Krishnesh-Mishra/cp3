// health-server (js entry, zero-dependency variant) -- node:http only.
//
// Run: node main.mjs [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. This variant is ALWAYS available; the suite runner
// prefers the optional Express variant in ./express/ (same behavior) when its
// node_modules exists, i.e. when `npm install` there succeeded.

import http from "node:http";

const port = Number(process.argv[2] ?? 8080);

const server = http.createServer((req, res) => {
  if (req.method === "GET" && req.url === "/healthz") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("ok");
  } else if (req.method === "GET" && req.url === "/") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("health-server ok");
  } else {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("not found");
  }
});

server.listen(port, "127.0.0.1", () => {
  console.log(`listening on http://127.0.0.1:${port}`);
});
