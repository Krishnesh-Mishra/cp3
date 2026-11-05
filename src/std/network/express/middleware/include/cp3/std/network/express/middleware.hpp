// =============================================================================
// src/std/network/express/middleware/include/cp3/std/network/express/middleware.hpp
// Express-style middleware engine for `std/network` (SOLO subtree).
//
// OWNERSHIP: src/std/network/express/middleware/** only. This header is
// self-contained: it depends only on the public `cp3/std/network.hpp`
// contract (HttpRequest / HttpResponse / Promise / Thrown) and does NOT
// modify or require changes to any shared file. The integrator wires it in
// with the lines listed in README.md (native row + optional HttpServer
// `use(path, ...)` forwarding).
//
// Features (Express semantics):
//   * `use(mw)` and `use("/api", mw)`          - path mounting (prefix,
//                                                  segment-boundary match)
//   * registration order = execution order      - middleware/route chain
//   * `next("route")` / `next.route()`          - skip rest of route stack
//   * sync and `async` (Promise<void>) handlers - dispatcher awaits promises
//   * 4-arg error middleware `(err, req, res, next)` dispatch on throw,
//     rejection, or `next(err)`
//   * a handler that sends a response ends the chain; `next()` continues it
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::express {

/// Normalise a mount path: "" / "*" -> "/", "/api/" -> "/api", "/api" -> "/api".
/// A mount not starting with '/' (other than "*" / "") is prefixed with '/'.
std::string normalizeMount(const std::string& mount);

/// True when `path` is at or under `mount` on a segment boundary:
/// mount "/api" matches "/api", "/api/", "/api/users" but NOT "/apix".
/// Mount "/" (and "*") matches every path. On a match, `remainder` receives
/// the part of `path` below the mount ("/" when the path equals the mount).
bool mountMatches(const std::string& mount, const std::string& path,
                  std::string* remainder = nullptr);

/// Full route-pattern match (same segment rules as the HttpServer dispatcher):
/// literal segments, `:name` captures one segment, a whole-segment `*` as the
/// last segment swallows the remainder. Captures are appended to `params`.
bool routePatternMatches(const std::string& pattern, const std::string& path,
                         std::vector<std::pair<std::string, std::string>>* params = nullptr);

class MiddlewareEngine;

namespace detail {
struct Cursor;
}  // namespace detail

/// The `next` handed to middleware / route-stack handlers by this engine.
///   next()          - continue the chain (normal layers; in an error layer
///                     clears the pending error and resumes normal layers)
///   next("route")   - skip every remaining handler in the current route
///                     stack (same route group), then continue
///   next.route()    - alias for next("route")
///   next(err)       - enter / continue error dispatch with `err`
///   await next()    - returns an already-settled Promise<void>; the chain
///                     itself is driven synchronously by the call, so awaiting
///                     it is safe inside an async handler
class ExpressNext {
public:
    ExpressNext() = default;
    ExpressNext(std::shared_ptr<detail::Cursor> cursor, std::size_t layerIndex,
                std::size_t routeGroup, bool isErrorLayer,
                std::shared_ptr<bool> advanced);

    void operator()();
    void operator()(const std::string& token);  // "route" (other tokens: plain next)
    void operator()(Thrown err);                // next(err)
    void route();
    void error(Thrown err);

    /// For `await next()` in async handlers: drives the chain, returns a
    /// settled promise (mirrors `network::Next::operator()`).
    Promise<void> call() { (*this)(); return Promise<void>::resolve(); }

    bool advanced() const { return advanced_ != nullptr && *advanced_; }

private:
    std::shared_ptr<detail::Cursor> cursor_;
    std::size_t layerIndex_ = 0;
    std::size_t routeGroup_ = 0;
    bool isErrorLayer_ = false;
    std::shared_ptr<bool> advanced_;
};

/// `(req, res, next) -> void | Promise<void>`. Sync and async both accepted.
using MiddlewareFn =
    std::function<Promise<void>(HttpRequest&, HttpResponse&, ExpressNext&)>;
/// `(err, req, res, next) -> void | Promise<void>` - the 4-arg error handler.
using ErrorMiddlewareFn = std::function<Promise<void>(
    Thrown, HttpRequest&, HttpResponse&, ExpressNext&)>;
/// One handler inside a route stack; receives `next` so `next("route")` works.
using RouteStackFn =
    std::function<Promise<void>(HttpRequest&, HttpResponse&, ExpressNext&)>;

namespace detail {
/// Wrap any sync/async callable into the uniform Promise-returning shape.
/// Mirrors `network::detail::callAsPromise` but for this engine's signatures.
template <class F, class... Args>
Promise<void> callAsPromise(F& fn, Args&... args) {
    using Result = std::remove_cvref_t<std::invoke_result_t<F&, Args&...>>;
    if constexpr (std::is_void_v<Result>) {
        fn(args...);
        return Promise<void>::resolve();
    } else if constexpr (::cp3::runtime::detail::is_promise_v<Result>) {
        if constexpr (std::is_void_v<typename Result::value_type>) {
            return fn(args...);
        } else {
            return fn(args...).then([](auto&&) {});
        }
    } else {
        (void)fn(args...);
        return Promise<void>::resolve();
    }
}
}  // namespace detail

struct Layer {
    std::string mount = "/";        // normalised mount (middleware) or ""
    std::string method;             // route only: "GET" ... or "*"
    std::string pattern;            // route only: full pattern
    std::size_t routeGroup = 0;     // 0 = not in a route stack
    bool isError = false;
    bool isRoute = false;
    MiddlewareFn middleware;        // normal middleware + route-stack handlers
    ErrorMiddlewareFn errorMiddleware;  // error layers only
};

/// Ordered middleware stack. Typical use (standalone or as one HttpServer
/// middleware via `asRouteFn()`):
///
///   MiddlewareEngine engine;
///   engine.use("/api", [](auto& req, auto& res, auto& next) { ... next(); });
///   engine.use([](auto& req, auto& res, auto& next) { res.text("hi"); }); // ends
///   engine.use([](Thrown err, auto& req, auto& res, auto& next) { ... }); // 4-arg
///   // route stack: next("route") in handler 1 skips handler 2
///   engine.route("GET", "/items/:id", {h1, h2});
///   engine.dispatch(req, res);  // 404 when exhausted, 500 on unhandled error
class MiddlewareEngine {
public:
    MiddlewareEngine() = default;

    // Typed overloads (explicit std::function values, e.g. factory results).
    void use(MiddlewareFn fn) { addMiddleware("/", std::move(fn)); }
    void use(const std::string& mount, MiddlewareFn fn) {
        addMiddleware(mount, std::move(fn));
    }
    void use(ErrorMiddlewareFn fn) { addErrorMiddleware("/", std::move(fn)); }
    void use(const std::string& mount, ErrorMiddlewareFn fn) {
        addErrorMiddleware(mount, std::move(fn));
    }

    /// Templated form: accepts sync `void` or async `Promise<void>` lambdas.
    /// Arity selects the layer kind: 4-arg -> error layer, else normal.
    template <class F>
    void use(F fn) {
        use("/", std::move(fn));
    }
    template <class F>
    void use(const std::string& mount, F fn) {
        if constexpr (std::is_invocable_v<F&, Thrown, HttpRequest&, HttpResponse&,
                                          ExpressNext&>) {
            addErrorMiddleware(mount,
                               [f = std::move(fn)](Thrown e, HttpRequest& r,
                                                   HttpResponse& s,
                                                   ExpressNext& n) mutable {
                                   return detail::callAsPromise(f, e, r, s, n);
                               });
        } else {
            addMiddleware(mount,
                          [f = std::move(fn)](HttpRequest& r, HttpResponse& s,
                                              ExpressNext& n) mutable {
                              return detail::callAsPromise(f, r, s, n);
                          });
        }
    }

    /// Register a route stack: all handlers share one route group, so
    /// `next("route")` inside any of them skips the rest of the stack and
    /// continues with whatever was registered after the stack.
    void route(const std::string& method, const std::string& pattern,
               std::vector<RouteStackFn> handlers);
    void route(const std::string& method, const std::string& pattern,
               RouteStackFn handler) {
        route(method, pattern, {std::move(handler)});
    }
    template <class F>
    void route(const std::string& method, const std::string& pattern, F fn) {
        route(method, pattern,
              RouteStackFn([fn = std::move(fn)](HttpRequest& req, HttpResponse& res,
                                               ExpressNext& next) mutable {
                  return detail::callAsPromise(fn, req, res, next);
              }));
    }

    // Unambiguous named forms (preferred when passing std::function values).
    void useMiddleware(const std::string& mount, MiddlewareFn fn) { addMiddleware(mount, std::move(fn)); }
    void useMiddleware(MiddlewareFn fn) { addMiddleware("/", std::move(fn)); }
    void useError(const std::string& mount, ErrorMiddlewareFn fn) { addErrorMiddleware(mount, std::move(fn)); }
    void useError(ErrorMiddlewareFn fn) { addErrorMiddleware("/", std::move(fn)); }

    /// Terminal hooks (overridable; defaults: 404 text / 500 text).
    void onExhausted(std::function<void(HttpRequest&, HttpResponse&)> fn) {
        exhausted_ = std::move(fn);
    }
    void onUnhandledError(
        std::function<void(Thrown, HttpRequest&, HttpResponse&)> fn) {
        unhandled_ = std::move(fn);
    }

    /// Run the stack for one request. Returns a promise that settles when
    /// the chain finishes or the response is sent. A response sent by any
    /// layer (res.sent()) stops the chain immediately - later layers run
    /// only if that layer also called next() before sending.
    Promise<void> dispatch(HttpRequest& req, HttpResponse& res);

    /// Adapter for the existing HttpServer: register the whole engine as a
    /// single `app.use(...)` middleware. The engine must outlive the server.
    /// Mounted layers inside the engine still filter by path; the underlying
    /// server route that runs this adapter should be "*" (app.use).
    detail::RouteFn asRouteFn();

    std::size_t size() const { return layers_.size(); }
    const std::vector<Layer>& layers() const { return layers_; }

private:
    void addMiddleware(const std::string& mount, MiddlewareFn fn);
    void addErrorMiddleware(const std::string& mount, ErrorMiddlewareFn fn);

    std::vector<Layer> layers_;
    std::size_t nextRouteGroup_ = 1;
    std::function<void(HttpRequest&, HttpResponse&)> exhausted_;
    std::function<void(Thrown, HttpRequest&, HttpResponse&)> unhandled_;
    std::shared_ptr<MiddlewareEngine> selfRef_;  // keeps cursor alive if needed
};

/// Helpers for the sema/codegen layers (pure, no engine state needed).
namespace sema_codegen {

/// How many declared parameters a `use(...)` handler arrow has: 3 = normal
/// middleware, 4 = error middleware. Any other arity is a type error.
inline constexpr std::size_t kNormalArity = 3;
inline constexpr std::size_t kErrorArity = 4;
inline bool isErrorArity(std::size_t paramCount) { return paramCount == kErrorArity; }
inline bool isMiddlewareArity(std::size_t paramCount) {
    return paramCount == kNormalArity || paramCount == kErrorArity;
}

/// True when a `next(...)` call argument is the literal `"route"`.
bool isNextRouteToken(const std::string& literalValue);

/// Validate a mount string literal for `app.use(mount, mw)`: must be "" /
/// "*" / start with '/'. Returns "" when valid, else a diagnostic message.
std::string validateMountLiteral(const std::string& mount);

/// C++ call text for a `use` registration: `engine.use("/api", lambda)` or
/// `engine.use("/api", errorLambda)` (chosen by `errorHandler`).
std::string useCallText(const std::string& mountExpr, const std::string& handlerExpr,
                        bool hasMount, bool errorHandler);

/// C++ call text for `next(...)`: `next()`, `next.route()` for the
/// `"route"` token, or `next.error(<err>)`.
std::string nextCallText(const std::string& nextExpr, const std::string& argExpr,
                         bool isRouteToken, bool hasArg);

}  // namespace sema_codegen
}  // namespace cp3::runtime::network::express
