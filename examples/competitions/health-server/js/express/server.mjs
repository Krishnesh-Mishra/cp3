// health-server (js entry, Express variant) -- used by the suite runner when
// this folder's node_modules exists (npm install succeeded); otherwise the
// runner falls back to the zero-dependency variant in ../main.mjs.
//
// Install:  npm install        (from this folder)
// Run:      node server.mjs [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"

import express from "express";

const port = Number(process.argv[2] ?? 8080);
const app = express();

app.get("/healthz", (req, res) => {
  res.type("text/plain").send("ok");
});

app.get("/", (req, res) => {
  res.type("text/plain").send("health-server ok");
});

app.use((req, res) => {
  res.status(404).type("text/plain").send("not found");
});

app.listen(port, "127.0.0.1", () => {
  console.log(`listening on http://127.0.0.1:${port}`);
});
