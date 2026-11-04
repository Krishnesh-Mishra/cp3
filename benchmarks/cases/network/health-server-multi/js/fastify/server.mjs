// health-server-multi (js entry, Fastify variant) -- node:cluster with one
// Fastify worker process per logical CPU.
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
// Fastify server and listens on the same address; Node's cluster module shares
// the listening handle so the workers accept connections in parallel.
// Binds 127.0.0.1 only.

import cluster from "node:cluster";
import os from "node:os";
import Fastify from "fastify";

const port = Number(process.argv[2] ?? 8080);

if (cluster.isPrimary) {
  const workers = Math.max(1, os.cpus().length);
  for (let i = 0; i < workers; i += 1) cluster.fork();
  console.log(`listening on http://127.0.0.1:${port} (${workers} workers)`);
} else {
  const app = Fastify({ logger: false });

  app.get("/healthz", (request, reply) => {
    reply.type("text/plain").send("ok");
  });

  app.get("/", (request, reply) => {
    reply.type("text/plain").send("health-server ok");
  });

  app.setNotFoundHandler((request, reply) => {
    reply.code(404).type("text/plain").send("not found");
  });

  try {
    await app.listen({ port, host: "127.0.0.1" });
  } catch (error) {
    app.log.error(error);
    process.exit(1);
  }
}
