# Express middleware engine — `src/std/network/express/middleware/**`

SOLO subtree. No shared file was touched. Code only (no build/test/commit).

## Files

| File | Role |
|---|---|
| `include/cp3/std/network/express/middleware.hpp` | Public API: `ExpressNext`, `MiddlewareEngine`, `MiddlewareFn`, `ErrorMiddlewareFn`, `RouteStackFn`, mount/pattern helpers, sema/codegen helper decls |
| `runtime/middleware_engine.cpp` | Runtime: mount matching, ordered chain, `next("route")`, async wait, 4-arg error dispatch, end-vs-next, 404/500 terminals |
| `sema/middleware_sema.cpp` | Sema helpers: arity classification (3 vs 4), `next("route")` token, mount validation |
| `codegen/middleware_codegen.cpp` | Codegen helpers: emit `engine.use(...)` / `next()` / `next.route()` / `next.error(err)` text |
| `native/middleware_native.hpp` | D68 native row snippet (paste-only, not applied) |
| `README.md` | This file: export surface, wiring lines, limitations |

## Export surface

```cpp
namespace cp3::runtime::network::express {

std::string normalizeMount(const std::string&);
bool mountMatches(const std::string& mount, const std::string& path, std::string* remainder = nullptr);
bool routePatternMatches(const std::string& pattern, const std::string& path,
                         std::vector<std::pair<std::string,std::string>>* params = nullptr);

class ExpressNext {
  void operator()();                    // next()
  void operator()(const std::string&);  // next("route")
  void operator()(Thrown);              // next(err)
  void route();                         // next("route")
  void error(Thrown);                   // next(err)
  Promise<void> call();                 // await next()
  bool advanced() const;
};
using MiddlewareFn      = std::function<Promise<void>(HttpRequest&, HttpResponse&, ExpressNext&)>;
using ErrorMiddlewareFn = std::function<Promise<void>(Thrown, HttpRequest&, HttpResponse&, ExpressNext&)>;
using RouteStackFn      = std::function<Promise<void>(HttpRequest&, HttpResponse&, ExpressNext&)>;

class MiddlewareEngine {
  void use(MiddlewareFn);  void use(const std::string& mount, MiddlewareFn);
  void use(ErrorMiddlewareFn);  void use(const std::string& mount, ErrorMiddlewareFn);
  template<class F> void use(F);                        // arity picks normal vs error
  template<class F> void use(const std::string& mount, F);
  void useMiddleware(...);  void useError(...);          // unambiguous named forms
  void route(const std::string& method, const std::string& pattern, std::vector<RouteStackFn>);
  void route(const std::string& method, const std::string& pattern, RouteStackFn);
  template<class F> void route(const std::string& method, const std::string& pattern, F);
  void onExhausted(std::function<void(HttpRequest&, HttpResponse&)>);
  void onUnhandledError(std::function<void(Thrown, HttpRequest&, HttpResponse&)>);
  Promise<void> dispatch(HttpRequest&, HttpResponse&);
  detail::RouteFn asRouteFn();   // adapt to existing HttpServer app.use(...)
  std::size_t size() const;  const std::vector<Layer>& layers() const;
};

namespace sema_codegen {
  bool isErrorArity(std::size_t);  bool isMiddlewareArity(std::size_t);
  bool isNextRouteToken(const std::string&);
  std::string validateMountLiteral(const std::string&);
  std::string useCallText(mountExpr, handlerExpr, hasMount, errorHandler);
  std::string nextCallText(nextExpr, argExpr, isRouteToken, hasArg);
}
}
```

Semantics: registration order = execution order; mount match is prefix on a
segment boundary (`/api` matches `/api/x`, not `/apix`); `res.sent()` ends
the chain; sync throw / async rejection / `next(err)` enters error dispatch
(normal layers skipped until a matching error layer); error-layer `next()`
clears the error and resumes normal layers; `next("route")` skips the
remaining handlers of the current `route()` stack; exhausted stack → 404,
unhandled error → 500 (both overridable).

## Wiring lines (for the integrator — NOT applied, shared files untouched)

1. **Native row** (`src/std/network/codegen/native.hpp`, D68): add include root
   `src/std/network/express/middleware/include`, TUs runtime/sema/codegen
   `.cpp` above, and marker `MiddlewareEngine(|express::` — see
   `native/middleware_native.hpp`.
2. **Sema** (`src/std/network/codegen/sema.cpp`, `httpServerFields`): extend
   the `use` field to `use(handler) | use(mount, handler)` where the handler
   is `httpMiddleware()` (3-param) or `httpErrorHandler()` (4-param);
   classify by `sema_codegen::isErrorArity(arrow.params.size())` and reject
   other arities; validate mounts with `validateMountLiteral`.
3. **Codegen** (`src/std/network/codegen/codegen_methods.cpp`, `networkArg`
   `property == "use"` arm): with a mount arg emit
   `sema_codegen::useCallText(mountText, lambdaText, true, isErrorArity(...))`;
   for a `next` call emit `sema_codegen::nextCallText("next", arg, isNextRouteToken(lit), hasArg)`.
   Lambda context for 4-arg handlers: `{Thrown, HttpRequest&, HttpResponse&, ExpressNext}`.
4. **Runtime (optional)**: `HttpServer::use(path, mw)` forwarding to
   `impl_->routes` with `path` as pattern, or register an engine once via
   `server.use(engine.asRouteFn())` (engine must outlive the server).

## Limitations

- Mounts filter by path but do **not** strip the prefix from `req.path`
  (no `req.baseUrl` on `HttpRequest`); mounted handlers see the full path.
- A handler that neither sends nor calls `next()` lets the chain proceed
  (matches the existing `HttpServer` dispatcher, differs from strict Express
  where it stalls). Double `next()` from one layer is ignored.
- `next("route")` only skips handlers in the same `route()` stack; in plain
  middleware it behaves as `next()`.
- Passing a `std::function` variable directly to templated `use` can be
  ambiguous — use `useMiddleware` / `useError` for those.
- Route params are overwritten per matched route layer (existing behaviour);
  mounted middleware params are not captured.
- Not built/tested (per task: code only); async dispatch relies on the
  existing `Promise` microtask loop.
