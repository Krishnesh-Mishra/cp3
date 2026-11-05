# Express body parsers (`express.json` / `urlencoded` / `text` / `raw`)

SOLO subtree: `src/std/network/express/body-parse/**` only. No shared file
was touched. The integrator (lead) applies the WIRING lines below; until
then the module compiles as a standalone TU pair (header + runtime cpp)
against the current `cp3/std/network.hpp` contract.

## Files

| File | What |
|---|---|
| `include/cp3/std/network/express/body_parse.hpp` | Public C++ surface: options, pure parsers, `req.body` accessors, `BodyParseError`, the four `MiddlewareFn` factories, `sema_codegen` helpers. |
| `runtime_body_parse.cpp` | Implements the header. Reuses `std/json` (`json::parseText` / `json::stringify`) and `std/url` (`::cp3::runtime::percentDecode`). |
| `README.md` | This file: export surface, wiring lines, limitations. |

## CP3 source forms (after wiring)

```cp3
import { createServer, json, urlencoded, text, raw } from "std/network";

const app = createServer();

app.use(json());                                   // application/json (+ *+json)
app.use(json({ limit: "1mb", strict: false }));
app.use(urlencoded({ extended: false }));          // querystring flat mode
app.use(urlencoded());                             // qs brackets (default)
app.use(text());                                   // text/plain
app.use(raw());                                    // application/octet-stream

app.post("/users", (req, res) => {
  const body = req.body;                           // parsed (see below)
  res.json({ ok: true });
});

app.listen(3000);
```

The namespace spelling `express.json()` lowers the same way (sema member +
codegen `emitNetworkCall` arms below).

## Export surface (C++)

Namespace `cp3::runtime::network::express::body_parse`:

- Options: `JsonBodyOptions{limitBytes, strict, type}`,
  `UrlEncodedBodyOptions{limitBytes, extended, type}`,
  `TextBodyOptions{limitBytes, type}`, `RawBodyOptions{limitBytes, type}`.
  `limitBytes <= 0` means unlimited (documented deviation from body-parser,
  which rejects a non-positive limit).
- Factories (all return `network::MiddlewareFn`, drop into `app.use`):
  `makeJsonMiddleware`, `makeUrlEncodedMiddleware`, `makeTextMiddleware`,
  `makeRawMiddleware` (+ `jsonParser` / `urlEncodedParser` / `textParser` /
  `rawParser` aliases spelling the `express.*` names).
- Pure parsers: `parseJsonBody(text, strict)` (throws `json::ParseError`),
  `parseFormPairs(text)`, `parseUrlEncodedBody(text, extended)`.
- Helpers: `parseLimitText("100kb")` (bytes.js units, 1024-based),
  `mediaTypeOf`, `isJsonContentType` (`application/json` + `*+json`),
  `isUrlEncodedContentType`, `headerValue(req, "content-type")`
  (case-insensitive).
- `req.body` accessors: `rawBodyText(req)`, `rawBodyBuffer(req)`
  (`std/buffer Buffer`), `parsedBody(req)` (`json::Value`: parsed object,
  raw string for text/raw bodies, `{}` when empty), `formBodyMap(req)`
  (`Map<string, string>` flattening of an object body).
- `BodyParseError{status, code, message}` + `toThrown()` / `is()` /
  `fromThrown()` — the error value for the `next(err)` wiring.
- `sema_codegen`: `isBodyParserName`, `middlewareCallText`,
  `parsedBodyCallText`, `validateOptionKey` (pure tables for sema/codegen).

## Behaviour

- **Content-type gate**: a parser only touches requests whose bare media
  type matches (`; charset=...` and case ignored). Mismatch -> `next()`
  untouched, exactly like body-parser.
- **Limit**: checked against the buffered body size *before* parsing;
  over -> **413** `"request entity too large"`, no `next()`.
- **JSON**: empty body -> `{}`; `strict` (default) rejects a top-level
  scalar/`null`; malformed JSON -> **400** `"invalid JSON: <parser msg>"`.
- **Urlencoded**: `extended` (default true) is the qs grammar (`a[b][c]`,
  `a[]`, `a[0]`, depth 5, array limit 20, parameter limit 1000);
  `extended: false` is Node `querystring` (flat, duplicates -> arrays).
  All leaf values are strings. Decoding is `+` -> space then
  `percentDecode` (malformed `%HH` stays literal).
- **Text/raw**: body left byte-for-byte; `text` = `text/plain`,
  `raw` = `application/octet-stream` by default; `type` overrides both
  (exact media type, or `"*/*"` for any).
- **Errors answer directly** (400/413 terminal, chain stops). This is the
  `next(err)` equivalent *until* the dispatcher grows a `Next(err)`
  overload: today a thrown/rejected error routes to `app.onError()` or a
  default 500, so throwing would mis-report a client error as a server
  error. `BodyParseError` carries the structured value for that wiring.

## WIRING (apply in order; every line is additive)

### 1. `src/std/network/codegen/native.hpp` (the runtime TU selection row)

```cpp
// include roots: append "src/std/url/include" next to "src/std/json/include"
// (runtime_body_parse.cpp includes cp3/std/url.hpp; std/json is already there)
// sources: append "src/std/network/express/body-parse/runtime_body_parse.cpp"
// markers: append "makeJsonMiddleware(|makeUrlEncodedMiddleware(|makeTextMiddleware(|makeRawMiddleware("
//          to the LAST marker entry -- the marker list is positional with
//          the source list, so it must stay the same length. The include
//          root for this module's own header is covered by
//          "src/std/network/include" (cp3/std/network/express/body_parse.hpp
//          sits under include/cp3/std/network/express/).
```

### 2. Generated-code include (no `network.hpp` change)

```cpp
// body_parse.hpp includes cp3/std/network.hpp, so network.hpp must NOT
// include it back (circular). Instead the codegen emits the include in
// generated TUs: in codegen.cpp, wherever needsNetwork_ adds
// #include "cp3/std/network.hpp", add a needsBodyParse_ flag (set by the
// arms in WIRING 4) that adds
//   #include "cp3/std/network/express/body_parse.hpp"
// after it. The factories are declared in body_parse.hpp, so
// `app.use(json())` needs nothing in network.hpp at all.
```

### 3. `src/std/network/codegen/sema.cpp` (typing)

```cpp
// networkMember(): after the static/cors/bodyLimit arm add:
if (prop == "json" || prop == "urlencoded" || prop == "text" || prop == "raw") {
    return Type::function({ambient()}, httpMiddleware());
}
// tryInferNetworkCall(): after the static/cors/bodyLimit arm add:
if (id.name == "json" || id.name == "urlencoded" || id.name == "text" || id.name == "raw") {
    inferArgs(call);
    // validate option keys via body_parse::sema_codegen::validateOptionKey
    out = httpMiddleware();
    return true;
}
// tryInferNetworkMemberCall(): the express.json() namespace spelling:
// after the dns.lookup arm add a member arm routing
// m.property in {json,urlencoded,text,raw} on ambient `express` to
// httpMiddleware(), same validation as above.
// httpRequestFields(): req.body stays `ambient()` (it is a json::Value at
// runtime; member access on it is dynamic, like today). Update only the
// doc string: "The parsed body (express body parsers); text() returns it raw."
```

### 4. `src/std/network/codegen/codegen.cpp` (lowering)

```cpp
// emitNetworkIdentifier(): after the bodyLimit arm add:
if (name == "json" || name == "urlencoded" || name == "text" || name == "raw") {
    needsNetwork_ = true;  // + needsBodyParse_ include of body_parse.hpp
    std::string options;   // walk the options object literal:
    //   limit: number -> JsonBodyOptions.limitBytes = <expr>
    //   limit: string -> parseLimitText(<expr>)
    //   strict/extended: bool, type: string
    return body_parse::sema_codegen::middlewareCallText(name, options);
}
// emitNetworkCall(): route m.property in {json,urlencoded,text,raw} for the
// `express` namespace object to the same arms.
// Generator include: #include "cp3/std/network/express/body_parse.hpp"
// alongside cp3/std/network.hpp when needsNetwork_ is set by these arms.
```

### 5. `src/std/network/codegen/codegen_methods.cpp` (`req.body`)

```cpp
// Replace the req.body reportUnsupported block (the "SEAM(readstream/fs)"
// arm) with:
if (name == kHttpRequest && m.property == "body") {
    needsNetwork_ = true;
    return "(" + expr(*m.object) + ")." + "/*req.body*/"
         + body_parse::sema_codegen::parsedBodyCallText(expr(*m.object));
}
```

### 6. `next(err)` (optional, restores the structured error flow)

```cpp
// cp3/std/network.hpp: add Promise<void> Next::operator()(Thrown err) that
// routes to the error-handler list (the errors/ module's NextError design
// is the template). Then in runtime_body_parse.cpp replace each failRequest
// call with `return next(err.toThrown())` where err is the BodyParseError,
// and delete failRequest. Until then, sendBodyParseError() answers 400/413
// directly (current behaviour above).
```

## LIMITATIONS

1. **Canonical-body storage**: a successful json/urlencoded parse writes
   `json::stringify(parsed)` back via `setBody()`, and `parsedBody()`
   reparses it. Consequences: numbers are reformatted (`1e2` -> `100`),
   key order is the parser's (preserved), and every `req.body` read pays
   one re-parse. `req.text()` downstream returns the *canonical* JSON,
   not the raw wire bytes (same as Express, where `req.body` replaces the
   raw stream; there is no `verify` hook to recover it).
2. **Double-parser guard is heuristic**: json re-parse is idempotent, but
   urlencoded twice would re-read canonical JSON as form text; the
   middleware skips when the body already looks like JSON (`{`/`[`).
   The robust fix is a `req._body` flag on `HttpRequest` (one shared-file
   line, same class as the wiring above).
3. **`limit <= 0` = unlimited** (body-parser errors on a non-positive
   limit instead). No `inflate` (link is built `UWS_NO_ZLIB`, so gzip
   bodies are *not* decompressed), no `verify`, no function-valued
   `type` (exact media type or `"*/*"` only), no `defaultCharset`.
4. **No `next(err)` yet**: parser failures answer 400/413 directly and do
   not reach `app.onError()` (see WIRING 6). `BodyParseError` /
   `BodyParseError::fromThrown` are ready for it.
5. **qs subset**: depth 5, array limit 20, parameter limit 1000; a scalar
   that later needs to be an object is dropped, `__proto__` keys are
   stored as plain fields (the `json::Value` tree has no prototype chain,
   so nothing is polluted, but `req.body` will contain the key).
6. **GET/HEAD with a body** are parsed like any other method (body-parser
   does the same); the server buffers the body before dispatch, so this
   is a synchronous transform, never a stream.
