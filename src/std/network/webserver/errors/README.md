# webserver error handling + static/sendFile options — `webserver/errors`

SOLO scope: **only** `src/std/network/webserver/errors/**`. No shared file was
touched, no build/test run, no commit.

## Files

| File | Role |
|---|---|
| `webserver_errors.hpp` | Public runtime surface (`cp3::runtime::network::webserver::errors`) |
| `runtime_errors.cpp` | Implementation (chain executor, default HTML handler, sendFile/static) |
| `sema_errors.hpp` / `sema_errors.cpp` | Sema spec tables + arity validators (standalone, no `Analyzer` dep) |
| `codegen_errors.hpp` / `codegen_errors.cpp` | Codegen string builders (standalone, no `Generator` dep) |
| `README.md` | This file |

## Export surface (runtime)

```cpp
namespace cp3::runtime::network::webserver::errors {

// err.status / err.expose
struct HttpError { int status; bool expose; std::string message, code, stack;
                   Thrown toThrown() const; static HttpError fromThrown(Thrown,int=500); };
HttpError httpError(int, std::string, std::string={});
HttpError badRequest/unauthorized/forbidden/notFound/methodNotAllowed/
          payloadTooLarge/internalError(std::string);

// next(err) flow
class NextError { Promise<void> operator()();          // next()
                  Promise<void> operator()(Thrown);    // next(err)
                  Promise<void> operator()(HttpError); // next(err)
                  Promise<void> operator()(std::nullptr_t); };
using RouteHandlerFn    = std::function<Promise<void>(HttpRequest&,HttpResponse&,NextError&)>;
using ErrorHandlerFn    = std::function<Promise<void>(HttpError,HttpRequest&,HttpResponse&,NextError&)>; // 4-arg
class ErrorChain { void use(RouteHandlerFn); void useError(ErrorHandlerFn);
                   Promise<void> run(HttpRequest&,HttpResponse&[,ErrorHandlerOptions]); };
MiddlewareFn asMiddleware(ErrorChain, ErrorHandlerOptions={});
detail::ErrorFn asErrorHandler(ErrorHandlerFn);   // 4-arg -> existing onError slot

// default HTML handler (status text; stack only in development)
std::string statusText(int); std::string escapeHtml(std::string);
bool isDevelopment(); bool isProduction();
std::string renderErrorHtml(HttpError const&, bool development);
ErrorHandlerFn defaultErrorHandler(ErrorHandlerOptions={});

// sendFile options
struct SendFileOptions { std::string root; std::string dotfiles="ignore";
  bool etag=true, lastModified=true, acceptRanges=true, fallthrough=true;
  double maxAge=0; StringMap headers; };
bool sendFileWithOptions(HttpResponse&, HttpRequest const&, std::string const&, SendFileOptions const&);
bool sendFile(HttpResponse&, HttpRequest const&, std::string const&, SendFileOptions={});

// static options
struct StaticOptions { std::string root; std::string dotfiles="ignore";
  bool etag=true, lastModified=true, acceptRanges=true, fallthrough=true,
       index=true, redirect=true; std::string indexName="index.html";
  double maxAge=0; StringMap headers; };
MiddlewareFn makeStatic(StaticOptions);
MiddlewareFn makeStatic(std::string dir, StaticOptions={});
}
```

Sema namespace: `...::errors::sema` — marker names `$network:HttpError`,
`$network:NextError`, field/option name tables, `checkErrorHandlerArity(4)`,
`checkNextArity(<=1)`, `checkSendFileArity(1..2)`.

Codegen namespace: `...::errors::codegen` — `sendFileOptionsText`,
`staticOptionsText`, `sendFileCallText`, `staticCallText`,
`errorHandlerAdaptText`, `nextCallText`, `httpErrorText`.

## Behaviour

- **Per-route + global**: `ErrorChain` keeps two ordered lists; a route's
  error handler is just the first `useError` entries for that chain, global
  ones follow. `asMiddleware(chain)` mounts the whole chain as one
  `app.use(...)`; on chain exhaustion with no response it calls outer `next()`.
- **next(err)**: diverts to error list at index 0; error-handler `next()`
  advances the error list with the same error, `next(newErr)` replaces it.
  Sync throw and promise rejection both divert. `res.sent()` stops everything.
- **err.status/expose**: factories set `expose = status < 500`; plain
  `Thrown`/cp3 `Error` converts via `fromThrown` (message from `.message`).
- **Default handler**: `text/html`, `X-Content-Type-Options: nosniff`,
  `<h1>{status} {statusText}</h1>`; production shows the message **only**
  when `expose`, never the stack; development shows message + `<pre>stack</pre>`.
  Dev is `NODE_ENV=development` or `CP3_ENV=development`, read per request.
- **sendFile**: `root` confines via `weakly_canonical` (escapes → 403 or
  fallthrough); `dotfiles` allow/deny/ignore on the root-relative path;
  weak `ETag: W/"{size-hex}-{mtime-hex}"`; `If-None-Match`/`If-Modified-Since`
  → **304** with ETag/Last-Modified/Cache-Control and no body; `maxAge` →
  `Cache-Control: public, max-age=N`; `headers` override; `fallthrough=false`
  throws `HttpError(404)` instead of returning false; `HEAD` sends headers only.
- **static**: GET/HEAD only; directory without trailing `/` → **301**
  `Location: {path}/` when `redirect`; directory with `/` serves `indexName`
  when `index`; miss → `next()` when `fallthrough` else throws 404.
  Reuses `sendFileWithOptions`, so ETag/304/maxAge/headers apply there too.

## Wiring lines (for the lead — NOT applied)

1. Native package (`src/std/network/codegen/native.hpp`, shared — lead only):
   add `"src/std/network/webserver/errors/runtime_errors.cpp"` to
   `runtimeSources`, and selectors `"sendFileWithOptions(|makeStatic(|defaultErrorHandler(|ErrorChain|httpError("`
   already covered by the `HttpServer(` row if routed through `app.use`.
2. Include for generated code: `#include` is via the new header path
   `src/std/network/webserver/errors/webserver_errors.hpp` (keep it on the
   native-package include list next to `src/std/network/include`).
3. `network.hpp` (shared — lead only): forward-declare nothing; users include
   the new header directly. Optionally add `using` aliases in
   `cp3::runtime::network` for `HttpError`/`SendFileOptions`/`StaticOptions`.
4. `sema.cpp` (shared): add `$network:HttpError` fields
   (`status,statusCode,expose,message,code,stack`), extend
   `httpResponseFields` `sendFile` to `sendFile(path, options?)`, add
   `app.useError(handler4)` + `app.onError` 4-arg overload, and `static`
   options validation from `sema_errors.cpp` tables.
5. `codegen.cpp`/`codegen_methods.cpp` (shared): `res.sendFile(p,o)` →
   `codegen::sendFileCallText(...)` (needs `req` in scope — pass the handler's
   request param); `next(err)` → `nextCallText("next", arg)` with
   `NextError` param type; 4-arg arrows get context
   `{HttpError, HttpRequest&, HttpResponse&, NextError}`.
6. Dispatcher (shared): for native `next(err)` inside the existing
   `app.use` chain, route `NextError` rejections through the existing
   `failRequestWithError` + `asErrorHandler(...)` adapter.

## Limitations

- **Range requests not implemented**: `Accept-Ranges: bytes` is advertised
  (webserver default) but `Range`/`206` is ignored — full body is always sent.
- **No `setHeaders` callback** in static options (webserver hook); use the
  `headers` map instead. **No `lastModified` date formatting beyond
  IMF-fixdate**; `If-Modified-Since` parse is minimal (day/month/year/time).
- **URL decoding**: `req.path` is used raw for static/sendFile (the server
  does not percent-decode paths); `%2e%2e` traversal is still confined by
  the canonicalisation check, but encoded filenames won't match on disk.
- **sendFile does not stream**: bytes are buffered via the existing
  `HttpResponse::sendFile` (whole-file `Content-Length`); large files load
  fully into memory. **ETag is weak** and derived from size+mtime only.
- **ErrorChain async capture**: `req`/`res` are borrowed from the dispatcher
  frame; the chain must be awaited by `asMiddleware` (it is) — storing a
  `NextError` and calling it after the response finished is a no-op via
  `res.sent()`. **No timeout** on error handlers (webserver has none either).
- **sema/codegen TUs are spec-only**: they compile standalone but have no
  effect until the lead applies the wiring above; generated cp3 code cannot
  reach these symbols yet.
