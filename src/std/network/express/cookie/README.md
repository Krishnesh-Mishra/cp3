# Express cookie parity (`src/std/network/express/cookie/**`)

Solo subagent deliverable. Nothing outside this directory was created/edited.
No build/test/commit was run.

## Files

| File | Role |
|---|---|
| `include/cp3/std/network/express_cookie.hpp` | Header-only runtime: `CookieOptions`, `buildSetCookie`/`buildClearCookie`, SHA-1/HMAC-SHA-1 (pure, no libsodium), `signValue`/`unsignValue` (cookie-signature wire format `s:<v>.<b64>`), `percentEncode`/`percentDecodeTolerant`, `parseCookieHeader`, `signedCookiesFor`, `setCookie`/`clearCookie` (over landed `HttpResponse::cookie`), `makeCookieParserMiddleware`, secret registry. |
| `sema_cookie.inc` | Paste-in closed-table rows for `src/std/network/codegen/sema.cpp`. |
| `codegen_cookie.inc` | Paste-in emit for `codegen_methods.cpp` (+ `codegen.cpp` for `cookieParser`). |
| `README.md` | This file: wiring, export surface, limitations. |

## Export surface (CP3)

```cp3
// res (chainable, both)
res.cookie(name, value, { maxAge, expires, path, domain, secure, httpOnly,
                          sameSite: "strict"|"lax"|"none", signed })
res.clearCookie(name, { path?, domain?, ... })   // Expires=past, Max-Age=0, Path=/ default
// req
req.cookies          // landed (raw, server-parsed)
req.signedCookies    // Map<string,string>, verified only
// app
app.set("secret", "s3cret")          // chainable; only "secret" is lowered
app.use(cookieParser("s3cret"))      // or cookieParser() / cookieParser(secretArray)
// usage
app.use(cookieParser("s3cret"));
app.get("/x", (req, res) => {
  res.cookie("session", req.signedCookies.get("session") ?? "", { signed: true, httpOnly: true, sameSite: "lax" });
  res.cookie("theme", "dark", { maxAge: 7*24*3600*1000, path: "/" });
  res.clearCookie("theme");
});
```

## Exact wiring for lead (shared files — you own these edits)

1. **Include path + probe** — `src/std/network/codegen/native.hpp`:
   - Add `"src/std/network/express/cookie/include"` to the include list (line ~32).
   - Header-only: **no** new runtime TU, no link change, no probe string needed
     (`makeCookieParserMiddleware(` rides the existing `makeStaticMiddleware(|makeCorsMiddleware(|makeBodyLimitMiddleware(` probe only if you also add `makeCookieParserMiddleware(` there — recommended one-token addition so a cookie-only program still pulls `network.hpp`; the header itself needs no symbols beyond `network.hpp`).
2. **Generated-code include** — wherever `needsNetwork_` emits `#include "cp3/std/network.hpp"`, also emit `#include "cp3/std/network/express_cookie.hpp"` when the cookie emit fires (or unconditionally alongside it). The codegen snippet assumes the `express::` names are visible.
3. **Sema** — paste `sema_cookie.inc` blocks 1–6 into `sema.cpp` (anchors are exact line numbers).
4. **Codegen** — paste `codegen_cookie.inc` blocks A–C into `codegen_methods.cpp` / `codegen.cpp`. Block A **replaces** the old `cookie` arm (lines 383–427); keep the old arm only if you want CP3-seconds `maxAge` (see Limitations).
5. **Request read** — `networkFieldType` + `emitNetworkMemberRead` per block B (2-line change) so `req.signedCookies.get(...)` type-checks and lowers.

## Limitations / deliberate deviations

- **Process-global secret registry** (`cookieSecrets()`): `HttpServer`/`HttpRequest` have no settings/fields store and this subagent may not touch them, so `app.set("secret")` and `cookieParser(secret)` write one global; `signedCookiesFor` reads it lazily. Two servers with different secrets in one process share the last-set secret. Per-app isolation needs a lead-side `HttpServer::settings` map + passing it through `HttpRequest` — then `signedCookiesFor(req)` becomes `signedCookiesFor(req, req.serverSecrets)`.
- **Tampered signed cookies**: Express yields `false`; CP3 `Map<string,string>` can't hold `false`, so they're omitted from `signedCookies` (still present raw in `req.cookies`).
- **`maxAge` units**: snippet implements Express **milliseconds** (`/1000.0`); landed CP3 codegen/sema documented **seconds**. Pick one — snippet defaults to Express parity (task requirement); dropping `/1000.0` keeps CP3 compat.
- **`expires`**: number = epoch **ms** → IMF-fixdate via `httpDateFromEpochMs`; string passes through. No `Date` object form (CP3 has none here).
- **`sameSite`**: string literal only at codegen; anything else is a codegen error. `SameSite=None` does not auto-add `Secure` (Express/cookie also leaves that to the app).
- **Encoding**: values are `encodeURIComponent`-encoded on write and tolerantly decoded on read — this differs from the landed server parse (raw, last-wins). `parseCookieHeader` (first-wins, decoded) is provided as the parity reference; swapping `runtime_server.cpp` to it is lead's call.
- **`j:` JSON cookies**: cookie-parser's JSON-cookie decoding is not implemented (values stay raw strings).
- **Secret rotation arrays**: runtime supports `vector<string>`; codegen snippet pins the string form (`cookieParser("s")`) — array-literal lowering is lead's collections array text.
- **`app.get(name)` settings getter**: not added — `get` is the route verb in the closed table; use the registry/middleware instead.
- **Invalid cookie names** throw `std::invalid_argument` from the header helpers (a hard failure, like Express throwing on bad names) rather than silently emitting a broken header.
- **Signing is HMAC-SHA1** (wire-compatible with Node `cookie-signature`), implemented pure-header-only; it deliberately does **not** use `std/crypto`'s HMAC-SHA256, so signed cookies are interchangeable with Express/Node.
