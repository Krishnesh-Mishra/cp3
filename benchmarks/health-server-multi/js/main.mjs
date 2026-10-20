// health-server-multi (js entry) -- node:cluster, one worker per logical CPU.
//
// Run: node main.mjs [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// The primary process forks one worker per CPU. Every worker creates the same
// node:http server and listens on the same address; Node's cluster module
// shares the listening handle so the workers accept connections in parallel.
// Binds 127.0.0.1 only.

import cluster from "node:cluster";
import http from "node:http";
import os from "node:os";

const port = Number(process.argv[2] ?? 8080);

function startServer() {
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
  server.listen(port, "127.0.0.1");
}

if (cluster.isPrimary) {
  const workers = Math.max(1, os.cpus().length);
  for (let i = 0; i < workers; i += 1) cluster.fork();
  console.log(`listening on http://127.0.0.1:${port} (${workers} workers)`);
} else {
  startServer();
}
