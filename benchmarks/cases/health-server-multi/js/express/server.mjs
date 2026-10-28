// health-server-multi (js entry, Express variant) -- node:cluster with one
// Express worker process per logical CPU.
//
// Install:  npm install        (from this folder)
// Run:      node server.mjs [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// The primary process forks one worker per CPU. Every worker creates the same
// Express server and listens on the same address; Node's cluster module shares
// the listening handle so the workers accept connections in parallel.
// Binds 127.0.0.1 only.

import cluster from "node:cluster";
import os from "node:os";
import express from "express";

const port = Number(process.argv[2] ?? 8080);

if (cluster.isPrimary) {
  const workers = Math.max(1, os.cpus().length);
  for (let i = 0; i < workers; i += 1) cluster.fork();
  console.log(`listening on http://127.0.0.1:${port} (${workers} workers)`);
} else {
  const app = express();
  app.set("strict routing", true);

  app.get("/healthz", (req, res) => {
    res.type("text/plain").send("ok");
  });

  app.get("/", (req, res) => {
    res.type("text/plain").send("health-server ok");
  });

  app.use((req, res) => {
    res.status(404).type("text/plain").send("not found");
  });

  app.listen(port, "127.0.0.1");
}
