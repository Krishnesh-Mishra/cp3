# Express response helpers (`src/std/network/express/response/**`)

Solo subagent deliverable. Nothing outside this directory was created/edited.
No build/test/commit was run.

## Files

| File | Role |
|---|---|
| `runtime/runtime_express_response.cpp` | Runtime TU. Defines the `HttpResponse::*` members declared by `include/.../express_response.hpp` §1 (set/append/get/type/format/render/download/attach/links/location/vary/headersSent + view-layer setters). Reads/writes `detail::ResponseImpl::headers` via `runtime_server_internal.hpp`; file bytes via existing `sendFile`/`text`. |
| `include/cp3/std/network/express_response.hpp` | Paste source for `src/std/network/include/cp3/std/network.hpp` §1 (member declarations inside `HttpResponse`) + the two view-layer free functions. Not on the include path until wired; never `#include`d by generated code. |
| `codegen/sema_response.inc` | Paste-in field rows + member docs for `src/std/network/codegen/sema.cpp`. Not a TU. |
| `codegen/codegen_response.inc` | Paste-in emit helpers + hunks for `codegen.cpp` / `codegen_methods.cpp` (+ one `generator.h` declaration). Not a TU. |
| `README.md` | This file: wiring, export surface, limitations. |

## Export surface (CP3)

```cp3
// All chainable except format (void) and the terminals.
// Headers
res.set(name: string, value: string)              // replace one header
res.set({ "X-Foo": "bar", ... })                 // replace many (object literal)
res.set(map: Map<string, string>)                // replace many (Map)
res.append(name: string, value: string)          // append / comma-join
res.get(name: string): string                     // "" when missing
// Content-Type + negotiation
res.type(type: string)                            // mime or short ext
res.format({ "text/html": () => { ... }, default: () => { ... } })
// Templates + files
res.render(view: string, locals?: object)        // text/html, terminal
res.download(path: string, filename?: string)    // attachment + sendFile, terminal
res.attach(filename: string)                     // Content-Disposition only
// Helpers
res.links({ next: "/p2", last: "/p1" })          // append Link header
res.location(url: string)                        // set Location
res.vary(field: string)                          // append Vary token
res.vary(["Accept-Encoding", "Cookie"])          // array-literal form
res.headersSent: boolean                         // property read
// (res.status chaining already exists and is untouched.)
```

## Exact wiring for lead (shared files — you own these edits)

1. **Runtime declarations** — paste `include/.../express_response.hpp` §1 (the
   `HttpResponse` member block) into `src/std/network/include/cp3/std/network.hpp`
   immediately after `HttpResponse& cookie(…)` (before `void sendFile(…)`).
   The block declares exactly the 14 members the TU defines; `varyImpl` is the
   internal vary helper (keep it, codegen never mentions it).
2. **Runtime build** — `src/std/network/codegen/native.hpp`:
   - Add `"src/std/network/express/response/runtime/runtime_express_response.cpp"`
     to `runtimeSources` (after `runtime_web.cpp`, before `runtime_server.cpp`).
   - Add a matching probe slot `"HttpServer("` at the same position in the probe
     list (so the TU links with the server, like `runtime_server.cpp`).
   - No new include dir needed (the TU includes `cp3/std/network.hpp` and the
     private internal header by relative path).
3. **Sema** — paste `codegen/sema_response.inc` §3a into `httpResponseFields()`
   (after `end`) and §4a into `networkObjectMemberDoc` (`HttpResponse` arm, after
   `end`). Also add the `headersSent` boolean to `networkFieldType` per
   `codegen_response.inc` §5c (2 lines).
4. **Codegen** — paste `codegen/codegen_response.inc` §5a (chain list in
   `codegen.cpp:isNetworkObject`), §5b (one-line dispatch + the
   `emitExpressResponseCall` helper in `codegen_methods.cpp`), §5c (the
   `headersSent` reads). Add one declaration to
   `src/codegen/src/detail/generator/generator.h` next to `emitNetworkMemberCall`:
   `std::string emitExpressResponseCall(const CallExpr& c, const MemberExpr& m);`
5. **Generated-code include** — when `needsNetwork_` fires, also emit
   `#include "cp3/std/network/express_response.hpp"` (or just rely on the
   declarations in `network.hpp` — the header adds nothing beyond them).
   The TU is the only new link unit.

## Limitations / deliberate deviations

- **`format` has no request.** Express negotiates against `Accept`; `HttpResponse`
  alone carries no request, so `format` invokes the first entry (or a lone
  `default`) and sets its `Content-Type` when unset. True negotiation needs the
  `req` Accept header — a lead could thread it via a response-bound copy of the
  request header map, then `format` would pick the first `Accept`-matching key.
- **`render` has no engine.** No `app.engine`/`app.set('views')` surface exists
  yet (app-scope agent owns it). This TU provides the sink
  (`setExpressResponseViewDir`/`setExpressResponseViewRenderer`); until the app
  agent calls them, `render` serves `views/<view>.html` as-is or answers 500.
  No template syntax (no `{{…}}` interpolation) without an engine; `locals`
  are JSON-stringified and handed to the renderer as text.
- **`res.type()` getter not added.** Express `res.type()` with no arg returns the
  current `Content-Type`; the task specifies the setter only, so the no-arg
  form is a sema arity error (use `res.get("Content-Type")`).
- **`res.get` returns `""` for missing**, not `undefined` (CP3 `Map`/`Headers`
  precedent: `Headers.get` is `string`). `?? ""` / `=== ""` checks both work.
- **`res.download`/`attach` filename handling**: `download` defaults the name
  to `basename(path)`; `attach` strips any path to the basename (Express
  behaviour). Non-ASCII names are not RFC 5987-encoded (no `filename*=`).
- **`res.vary("*")` pins** the header to `"*"`; afterwards any `vary` is a no-op
  (Express `vary` package behaviour). `Vary` tokens compare case-insensitively.
- **`res.append` comma-joins** any existing value, including non-list headers
  (e.g. `Set-Cookie` gets a single comma-joined line, not two lines) — matching
  Express `res.append` semantics, which differ from `Set-Cookie` handling.
- **Unbound/terminated responses**: chainable helpers are no-ops returning `*this`,
  `get` returns `""`, `headersSent` is `false`, terminals are ignored — the same
  guards as the landed `text`/`sendFile`.
- **`set` with a Map** iterates `entries()`; a Map with duplicate keys is
  impossible (last-wins), while the object-literal form also dedupes by source
  order (later property wins is handled by sequential `setHeader` replaces).
