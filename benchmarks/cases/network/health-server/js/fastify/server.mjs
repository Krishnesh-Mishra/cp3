// health-server (js entry, Fastify variant) -- Fastify on node:http.
//
// Install:  npm install        (from this folder)
// Run:      node server.mjs [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only.

import Fastify from "fastify";

const port = Number(process.argv[2] ?? 8080);
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
  console.log(`listening on http://127.0.0.1:${port}`);
} catch (error) {
  app.log.error(error);
  process.exit(1);
}
