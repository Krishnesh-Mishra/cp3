# Express complex routing — wiring notes

All implementation lives in this directory (`src/std/network/express/routing/`).
Nothing in the shared files was touched. The lead applies the lines below.

## Files

| File | Role |
|---|---|
| `runtime_routing.hpp` | Public surface: `Router`, `RouteChain`, `ExpressApp`, `RouterOptions`, `HandlerList`, `ParamFn`, `detail_ext::{composeHandlers, matchPattern, makeHandlerWithNext, makeParam}` |
| `runtime_routing.cpp` | Matcher (`:id?`, `*`, strict), handler composition, param dispatch, mount stripping, `mergeParams`, `ExpressApp` |
| `sema_routing.cpp` | Sema fragment: marker constants, `routerFields()`/`routeChainFields()`/`paramHandler()`/`routeHandlerOrList()` + splice comments |
| `codegen_routing.cpp` | Codegen fragment: `Generator::{emitRoutingMemberCall, routingHandlers, routingHandler, routingParamCallback, routingOptions}` + splice comments |
| `WIRING.md` | This file |

TUs are currently *fragments*: `sema_routing.cpp`/`codegen_routing.cpp` are
written against the Analyzer/Generator vocabularies and become real TUs once
the splice lines below are applied (they are not in the build yet).

## Export surface (C++)

```cpp
namespace cp3::runtime::network::express {
struct RouterOptions { bool mergeParams=false; bool strict=false; };
using HandlerList = std::vector<std::function<Promise<void>(HttpRequest&,HttpResponse&,Next&)>>;
using ParamFn     = std::function<Promise<void>(HttpRequest&,HttpResponse&,Next&,std::string)>;

class Router {
  Router(); explicit Router(RouterOptions);
  void get/post/put/del/patch/head/options/all(path, F);      // single handler
  void get/post/.../all(path, HandlerList);                   // array / variadic
  void use(F); void use(Router&); void use(mountPath, Router&);
  void param(name, F);  RouteChain route(path);
  detail::RouteFn middleware(); Promise<void> dispatch(req,res,next);
  bool mergeParams() const; void setMergeParams(bool);
  bool strictRouting() const; void setStrictRouting(bool);
};
class RouteChain { /* get/post/put/del/patch/head/options/all -> RouteChain& */ };
class ExpressApp { /* owns HttpServer + Router; all of the above + listen/close/onError */ };
ExpressApp makeExpressApp(); ExpressApp makeExpressApp(RouterOptions);
Router makeRouter(); Router makeRouter(bool mergeParams, bool strict);
}
```

Usage without any shared-file change:

```cpp
namespace net = cp3::runtime::network;
net::HttpServer app;
net::express::Router api(net::express::RouterOptions{true, false}); // mergeParams
api.route("/users/:id?").get([](auto& req, auto& res){ res.json(...); })
                       .post([h1, h2](auto& req, auto& res, auto& next){ ... })..
app.use(api.middleware());
app.listen(3000);
```

## Wiring lines for the lead

### 1. Build

Add the runtime TU to the `std/network` native package (next to
`src/std/network/server/runtime_server.cpp` in the CMake/native manifest):

```
src/std/network/express/routing/runtime_routing.cpp
```

Header include (generated code): emit `#include
"cp3/std/network.hpp"` already covers `HttpServer`; routing types need

```cpp
#include "../std/network/express/routing/runtime_routing.hpp"
```

on the generated-code include list (copy the path into the
`needsNetwork_` gate in the cpp writer).

### 2. `src/std/network/include/cp3/std/network.hpp` (optional HttpServer forwarding)

These put `route()`/`all()`/`param()` directly on `HttpServer`; without them
use `Router`/`ExpressApp`. Add includes + members inside `class HttpServer`:

```cpp
// after the existing any() member:
template <class F>
express::RouteChain route(const std::string& path) const;
// needs an impl_->routes registration; see note
```

`RouteChain` needs a `Router&` owner, so the HttpServer form must own one:
recommended shape (add to `detail::ServerImpl`):

```cpp
express::Router routing;                 // app-level router state
bool routingInstalled = false;
```

and wire `HttpServer::route`/`all`/`param` to forward onto it, installing
`routing.middleware()` via `addMiddlewareImpl` on first use. `all` is the
trivial one and can land independently:

```cpp
template <class F>
void all(const std::string& path, F handler) const {
    addRouteImpl("*", path, detail::makeRoute(std::move(handler)));
}
```

### 3. `src/std/network/codegen/sema.cpp`

Add markers `kRouter`, `kRouteChain`, `kExpressApp` (see splice comments in
`sema_routing.cpp`); extend `httpServerFields()` with `all`, `route`,
`param`; add `routerFields()`/`routeChainFields()` tables to
`networkFieldsFor()`; add `Router`/`express` members to `networkMember()`;
handle `Router()`/`express()` in `tryInferNetworkCall()` and `route()`'s
result type. The `paramHandler()`/`routeHandlerOrList()` helpers in
`sema_routing.cpp` paste verbatim.

### 4. `src/codegen/src/detail/generator/generator.h`

```cpp
std::string routingHandler(const Expr& arg);
std::string routingHandlers(const ExprList& args);
std::string routingParamCallback(const Expr& arg);
std::string routingOptions(const Expr* options);
std::string emitRoutingMemberCall(const CallExpr& c, const MemberExpr& m);
```

### 5. `src/std/network/codegen/codegen_methods.cpp`

At the top of `Generator::emitNetworkMemberCall`, after the `json`/`cookie`
special cases:

```cpp
if (std::string routing = emitRoutingMemberCall(c, m); !routing.empty()) return routing;
```

### 6. `src/std/network/codegen/codegen.cpp`

`emitNetworkIdentifier`: add

```cpp
if (name == "Router")  return std::string(kRuntimeNs) + "express::makeRouter(" + routingOptions(...) + ")";
if (name == "express") return std::string(kRuntimeNs) + "express::makeExpressApp()";
```

with the options-argument read and `needsNetwork_ = true` (see
`codegen_routing.cpp` for `routingOptions`). `isNetworkObject` must treat
`Router`/`ExpressApp`/`RouteChain` receivers as network objects (add the
markers next to `kHttpServer` there and in `emitNetworkMemberRead`-adjacent
recognition).

## Limitations

- **Runtime matcher (own implementation, not the ServerImpl dispatcher):**
  whole-segment params only (`:id`, `:id?`); glued forms (`:id-:x`, `*.json`)
  and path-to-regexp magic are not supported. `*` follows the existing
  server rules (trailing `*` swallows the remainder including slashes;
  mid-pattern `*` is one segment). Duplicate param names: last write wins.
- **Single-handler verbs keep 2-arity semantics:** `next` is unavailable in
  `app.get(path, h)` (same as `HttpServer` today). Use the array form
  (`[h1, h2]`) or `use()` for `next`-ful handlers; array/variadic elements
  are wrapped with `makeHandlerWithNext`.
- **`next` chains end at response or fall-through:** a handler chain that
  neither responds nor calls `next()` ends the chain (outer `next()`/404),
  it does not hang like Express. Async `next()` (stored and called later)
  works; a stored `next` called after the outer chain answered is a no-op.
- **Mount stripping** temporarily rewrites `req.path` (per-request, single
  loop thread) and restores it on settle. There is no `req.baseUrl` /
  `originalUrl` field on `HttpRequest` (shared header untouched); nested
  mounts therefore do not expose the mount prefix to handlers.
- **`Router` captures its own `this` in `middleware()`/`use()`:** move or
  copy a `Router` *before* taking `middleware()`/mounting it (construct in
  place where possible). `ExpressApp` avoids the same trap by installing
  its router middleware lazily in `listen()`.
- **Param callbacks run only for routes whose pattern contains the name,**
  in pattern-then-registration order, each with the captured value. The
  value is a copy; mutating it does not change `req.params` (Express v4
  behaviour). A throwing/pending callback follows the dispatcher model
  (500/`onError`).
- **Strict is per-router at construction/setter time and applies to that
  router's routes only** (Express `strict routing` is app-wide; here use
  `ExpressApp::setStrictRouting` for the app router). Non-strict trailing
  slash normalisation strips at most one trailing empty segment.
- **Sema shares one `use` row name twice** (matches the existing table
  style); the second row (mount form) is reached only via `use(mount,
  router)` and is not named separately. Variadic tails beyond two handler
  arguments need the array-form type row.
- **Codegen fragments are not compiled** until the splice lines are applied;
  option literals for `Router({ mergeParams, strict })` must be boolean
  literals (non-literal values are a type error at the use site).
