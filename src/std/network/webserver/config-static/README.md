# createServer() config — static + body limits (`config_static`)

SOLO subtree: `src/std/network/webserver/config-static/**` only. No shared
file was touched. The integrator (lead) applies the WIRING lines in
`WIRING.md`; until then the header compiles standalone against the current
`cp3/std/network.hpp` + body_parse + settings contracts.

## CP3 source forms (after wiring)

```cp3
import { createServer } from "std/network";

// Everything in one call:
const app = createServer({
  static: { dir: "./public", index: "index.html", etag: true,
            maxAge: 3600, fallthrough: true },
  bodyLimit: "1mb",
  json: { limit: "1mb", strict: true },
  urlencoded: { extended: true },
  trustProxy: "loopback",
  timeouts: { request: 30000, keepAlive: 5000 },
});

app.get("/api/health", (req, res) => { res.json({ ok: true }); });
app.listen(3000);
```

Any subset works; omitted keys install nothing (`threads` keeps its D79
meaning). The pre-existing spellings are unchanged and win nothing over
this one — they compose: `app.use(static("other"))` after a config-static
`createServer` just adds another middleware after the config's chain.

## Export surface (C++)

Namespace `cp3::runtime::network::webserver::config_static`:

- Config: `StaticConfig{enabled, dir, index, serveIndex, etag, maxAge,
  fallthrough}`, `JsonConfig{enabled, hasLimit, limitBytes, strict}`,
  `UrlEncodedConfig{enabled, hasLimit, limitBytes, extended}`,
  `ServerTimeouts{requestMs, headersMs, idleMs, keepAliveMs}`,
  `CreateServerConfig{threads, static_, hasBodyLimit, bodyLimitBytes,
  json, urlencoded, hasTrustProxy, trustProxy, timeouts}`.
- Validation: `validateConfig(cfg) -> vector<string>`,
  `ensureValid(cfg)` (throws `Error`), `effectiveParserLimit(...)`.
- Static: `makeConfigStaticMiddleware(StaticConfig)`.
- Composition: `middlewaresFor(cfg)` (bodyLimit → json → urlencoded →
  static), `installCreateServerConfig(app, cfg)`,
  `applyToSettings(cfg, settings)`, `createServerWithConfig(cfg)` (the
  codegen target), `lastCreateServerConfig()` (+ `...Slot()`).
- `sema_codegen`: `isTopLevelKey`/`isStaticKey`/`isJsonKey`/
  `isUrlEncodedKey`/`isTimeoutKey`, `validateOptionKey(group, key)`,
  `createServerCallText(configExpr)`, `limitValueText(expr, isString)`.

## Behaviour

- **Validation at construction**: empty `static.dir` when `static` is
  present, negative/NaN `maxAge`, negative limits, and negative/NaN
  timeouts throw `Error` from `createServerWithConfig` (500/`onError` at
  startup, not per-request).
- **Static**: GET/HEAD only; traversal outside `dir` never serves;
  directory → `<dir>/<index>` when enabled; weak ETag
  `W/"<size>-<mtime>"` with `If-None-Match` → 304; `maxAge > 0` →
  `Cache-Control: public, max-age=<n>`; miss → `next()` (fallthrough) or
  404 (`fallthrough: false`).
- **Body limits**: the global gate runs before parsing (413 above the
  limit, stock bodyLimit wording); parsers then apply their effective
  limit (own → global → "100kb") and answer 400/413 themselves
  (body_parse behaviour, canonical-body storage included).
- **trustProxy/timeouts**: see WIRING.md steps 9–10 and Limitations.

## Limitations

See `WIRING.md` → Limitations (timeouts stored-not-enforced, trust-proxy
scope, zero-means-different-things, serve-static divergences, non-literal
options, construction-only application, thread-local snapshot).
