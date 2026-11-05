// =============================================================================
// src/std/network/express/routing/runtime_routing.hpp - Express complex routing.
//
// OWNERSHIP: the std/network family, the `express/routing` sub-family
// (src/std/network/express/routing/** ONLY). This header is the additive
// routing surface that sits on top of the public cp3/std/network.hpp contract;
// it does not modify the shared HttpServer header (see WIRING.md for the
// optional forwarding lines the lead applies to the shared files).
//
// Express shapes covered here (docs: Express 4/5 API):
//   * route chaining:            app.route('/x').get(h).post(h)
//   * app.all(path, handler):    every method
//   * optional params:           '/users/:id?' matches '/users' and '/users/7'
//   * multiple handlers:         app.get('/x', [h1, h2]) / variadic tail
//   * param callbacks:           app.param('id', (req,res,next,id)=>...)
//   * nested routers:            Router + app.use('/prefix', router),
//                                router option mergeParams
//   * strict routing:            router option strict (trailing-slash exact)
//                                vs the Express default (trailing slash ignored)
//
// Integration without touching shared files:
//   * `Router` is standalone (own route table, own matcher). Mount it on the
//     existing HttpServer with `server.use(router.middleware())`.
//   * `ExpressApp` owns one HttpServer + one Router and exposes the full
//     surface (verb methods, route(), all(), param(), use(), listen()...).
//   * WIRING.md lists the one-line forwarding members that put route()/all()/
//     param() directly on HttpServer, plus the sema/codegen splice lines.
// =============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::express {

/// Router construction options (Express `Router({ mergeParams, strict })`).
struct RouterOptions {
    /// When false (default) a nested router only sees its own params; when
    /// true it also sees the parent mount's params (mergeParams). Own params
    /// always win on a name collision.
    bool mergeParams = false;
    /// When false (default, Express behaviour) '/x' and '/x/' match the same
    /// route. When true the trailing slash must match exactly.
    bool strict = false;
};

/// The uniform handler list of one route (see composeHandlers).
using HandlerList = std::vector<::cp3::runtime::network::detail::RouteFn>;

/// A `app.param(name, fn)` callback: `(req, res, next, value)`. `value` is the
/// captured param text; mutations are written back to `req.params` before the
/// route handlers run.
using ParamFn = std::function<::cp3::runtime::Promise<void>(
    ::cp3::runtime::network::HttpRequest&, ::cp3::runtime::network::HttpResponse&,
    ::cp3::runtime::network::Next&, std::string value)>;

namespace detail_ext {

/// Sequentially runs `handlers`, wiring each handler's `next` to the following
/// handler and the last handler's `next` to `outer`. A handler that responds
/// stops the chain; a handler whose promise is still pending keeps the chain
/// alive until it settles (same model as the server dispatcher).
::cp3::runtime::network::detail::RouteFn composeHandlers(HandlerList handlers);

/// Matches `pattern` against `path` honouring RouterOptions::strict and the
/// Express segment grammar (`:name`, `:name?`, `*`). Captures are appended to
/// `captures` in pattern order (duplicated names keep the last write when the
/// caller builds the params Map, Express order).
bool matchPattern(const std::string& pattern, const std::string& path, bool strict,
                  std::vector<std::pair<std::string, std::string>>* captures);

/// Wraps one 3-arity handler `(req, res, next)` as a RouteFn. The plain
/// HttpServer::get-style registration (detail::makeRoute) drops `next`;
/// multi-handler arrays and param callbacks use this wrapper so `next()`
/// advances to the following handler.
template <class F>
::cp3::runtime::network::detail::RouteFn makeHandlerWithNext(F fn) {
    return [fn = std::move(fn)](::cp3::runtime::network::HttpRequest& req,
                                 ::cp3::runtime::network::HttpResponse& res,
                                 ::cp3::runtime::network::Next& next) mutable
           -> ::cp3::runtime::Promise<void> {
        return ::cp3::runtime::network::detail::callAsPromise(fn, req, res, next);
    };
}

/// `app.param(name, fn)` with fn = `(req, res, next, value)`.
template <class F>
ParamFn makeParam(F fn) {
    return [fn = std::move(fn)](::cp3::runtime::network::HttpRequest& req,
                                 ::cp3::runtime::network::HttpResponse& res,
                                 ::cp3::runtime::network::Next& next,
                                 std::string value) mutable
           -> ::cp3::runtime::Promise<void> {
        return ::cp3::runtime::network::detail::callAsPromise(fn, req, res, next, value);
    };
}

}  // namespace detail_ext

class RouteChain;
class Router;

/// The return of `app.route(path)` / `router.route(path)`: each verb registers
/// on the same path and returns `*this`, so `app.route('/x').get(h).post(h)`
/// chains. Holds a non-owning Router pointer (the router must outlive the
/// chain; in practice both are module-scope).
class RouteChain {
public:
    RouteChain(Router* router, std::string path) : router_(router), path_(std::move(path)) {}

    /// Registers `HandlerList` under `method` ("*" for all) and returns *this.
    RouteChain& add(const char* method, HandlerList handlers);

    template <class F>
    RouteChain& add(const char* method, F handler) {
        return add(method, HandlerList{::cp3::runtime::network::detail::makeRoute(std::move(handler))});
    }

    template <class F>
    RouteChain& get(F handler) { return add("GET", std::move(handler)); }
    template <class F>
    RouteChain& post(F handler) { return add("POST", std::move(handler)); }
    template <class F>
    RouteChain& put(F handler) { return add("PUT", std::move(handler)); }
    template <class F>
    RouteChain& del(F handler) { return add("DELETE", std::move(handler)); }
    template <class F>
    RouteChain& patch(F handler) { return add("PATCH", std::move(handler)); }
    template <class F>
    RouteChain& head(F handler) { return add("HEAD", std::move(handler)); }
    template <class F>
    RouteChain& options(F handler) { return add("OPTIONS", std::move(handler)); }
    template <class F>
    RouteChain& all(F handler) { return add("*", std::move(handler)); }

    /// Vector form: `app.route('/x').get([h1, h2])`.
    RouteChain& get(HandlerList handlers) { return add("GET", std::move(handlers)); }
    RouteChain& post(HandlerList handlers) { return add("POST", std::move(handlers)); }
    RouteChain& put(HandlerList handlers) { return add("PUT", std::move(handlers)); }
    RouteChain& del(HandlerList handlers) { return add("DELETE", std::move(handlers)); }
    RouteChain& patch(HandlerList handlers) { return add("PATCH", std::move(handlers)); }
    RouteChain& head(HandlerList handlers) { return add("HEAD", std::move(handlers)); }
    RouteChain& options(HandlerList handlers) { return add("OPTIONS", std::move(handlers)); }
    RouteChain& all(HandlerList handlers) { return add("*", std::move(handlers)); }

private:
    Router* router_;
    std::string path_;
};

/// A standalone Express router: own route table (method/pattern/handler list),
/// own matcher (optional params, strict toggle), param callbacks and
/// mergeParams. Mountable on HttpServer via middleware() (or on an ExpressApp /
/// another Router via use()).
class Router {
public:
    Router() = default;
    explicit Router(RouterOptions options) : options_(options) {}

    bool mergeParams() const { return options_.mergeParams; }
    void setMergeParams(bool on) { options_.mergeParams = on; }
    bool strictRouting() const { return options_.strict; }
    void setStrictRouting(bool on) { options_.strict = on; }

    /// Registers a route. `method` is upper case ("*" = every method, see all).
    void add(const std::string& method, const std::string& path, HandlerList handlers);

    template <class F>
    void add(const std::string& method, const std::string& path, F handler) {
        add(method, path,
            HandlerList{::cp3::runtime::network::detail::makeRoute(std::move(handler))});
    }

    template <class F>
    void get(const std::string& path, F handler) { add("GET", path, std::move(handler)); }
    template <class F>
    void post(const std::string& path, F handler) { add("POST", path, std::move(handler)); }
    template <class F>
    void put(const std::string& path, F handler) { add("PUT", path, std::move(handler)); }
    template <class F>
    void del(const std::string& path, F handler) { add("DELETE", path, std::move(handler)); }
    template <class F>
    void patch(const std::string& path, F handler) { add("PATCH", path, std::move(handler)); }
    template <class F>
    void head(const std::string& path, F handler) { add("HEAD", path, std::move(handler)); }
    template <class F>
    void options(const std::string& path, F handler) { add("OPTIONS", path, std::move(handler)); }
    /// `app.all(path, handler)`: every method (the wildcard method row).
    template <class F>
    void all(const std::string& path, F handler) { add("*", path, std::move(handler)); }

    /// Vector form: `router.get('/x', [h1, h2])`.
    void get(const std::string& path, HandlerList handlers) { add("GET", path, std::move(handlers)); }
    void post(const std::string& path, HandlerList handlers) { add("POST", path, std::move(handlers)); }
    void put(const std::string& path, HandlerList handlers) { add("PUT", path, std::move(handlers)); }
    void del(const std::string& path, HandlerList handlers) { add("DELETE", path, std::move(handlers)); }
    void patch(const std::string& path, HandlerList handlers) { add("PATCH", path, std::move(handlers)); }
    void head(const std::string& path, HandlerList handlers) { add("HEAD", path, std::move(handlers)); }
    void options(const std::string& path, HandlerList handlers) { add("OPTIONS", path, std::move(handlers)); }
    void all(const std::string& path, HandlerList handlers) { add("*", path, std::move(handlers)); }

    /// `app.use(middleware)`: runs for every path, in registration order.
    template <class F>
    void use(F middleware) {
        useImpl(::cp3::runtime::network::detail::makeMiddleware(std::move(middleware)));
    }

    /// `app.use(child)`: mounts `child` at the root.
    void use(Router& child) { use(std::string(), child); }

    /// `app.use(mountPath, child)`: mounts `child` under `mountPath`. The
    /// child's routes match the path with `mountPath` stripped; with
    /// `mergeParams` the parent mount's params stay visible.
    void use(const std::string& mountPath, Router& child);

    /// `app.param(name, fn)`: runs `fn` before the handlers of any route whose
    /// pattern contains `:name` (Express order: before the handler, after the
    /// route matched). Multiple callbacks for one name run in registration
    /// order; each sees the previous callback's mutated value.
    template <class F>
    void param(const std::string& name, F callback) {
        paramCallbacks_.emplace_back(name, detail_ext::makeParam(std::move(callback)));
    }

    /// `app.route(path)`: the chainable route object.
    RouteChain route(const std::string& path) { return RouteChain(this, path); }

    /// The dispatch closure for HttpServer::use / Router::use. On the first
    /// matching route it runs param callbacks then handlers; when nothing
    /// matches (or a handler chain falls through without responding) it calls
    /// `next()`.
    ::cp3::runtime::network::detail::RouteFn middleware();

    /// Dispatch entry used by middleware() and mount wrappers.
    ::cp3::runtime::Promise<void> dispatch(::cp3::runtime::network::HttpRequest& req,
                                           ::cp3::runtime::network::HttpResponse& res,
                                           ::cp3::runtime::network::Next& next);

private:
    struct Route {
        std::string method;  // "*" = every method
        std::string pattern;
        HandlerList handlers;
    };
    void useImpl(::cp3::runtime::network::detail::RouteFn handler);

    RouterOptions options_;
    std::vector<Route> routes_;
    std::vector<std::pair<std::string, ParamFn>> paramCallbacks_;
};

/// The self-contained app: one HttpServer + one Router. Every verb/route/param
/// call registers on the Router; listen/close/onError delegate to the server.
/// This is the surface codegen targets for createServer-with-routing programs
/// when the HttpServer forwarding members (WIRING.md) are not applied.
class ExpressApp {
public:
    // Construction is cheap; the router middleware is installed lazily on
    // listen() so returning an ExpressApp by value (makeExpressApp) never
    // leaves a stale `this` capture in the server.
    ExpressApp() : router_(RouterOptions{}) {}
    explicit ExpressApp(RouterOptions options) : router_(options) {}

    template <class F>
    void get(const std::string& path, F handler) { router_.get(path, std::move(handler)); }
    template <class F>
    void post(const std::string& path, F handler) { router_.post(path, std::move(handler)); }
    template <class F>
    void put(const std::string& path, F handler) { router_.put(path, std::move(handler)); }
    template <class F>
    void del(const std::string& path, F handler) { router_.del(path, std::move(handler)); }
    template <class F>
    void patch(const std::string& path, F handler) { router_.patch(path, std::move(handler)); }
    template <class F>
    void head(const std::string& path, F handler) { router_.head(path, std::move(handler)); }
    template <class F>
    void options(const std::string& path, F handler) { router_.options(path, std::move(handler)); }
    template <class F>
    void all(const std::string& path, F handler) { router_.all(path, std::move(handler)); }

    void get(const std::string& path, HandlerList handlers) { router_.get(path, std::move(handlers)); }
    void post(const std::string& path, HandlerList handlers) { router_.post(path, std::move(handlers)); }
    void put(const std::string& path, HandlerList handlers) { router_.put(path, std::move(handlers)); }
    void del(const std::string& path, HandlerList handlers) { router_.del(path, std::move(handlers)); }
    void patch(const std::string& path, HandlerList handlers) { router_.patch(path, std::move(handlers)); }
    void head(const std::string& path, HandlerList handlers) { router_.head(path, std::move(handlers)); }
    void options(const std::string& path, HandlerList handlers) { router_.options(path, std::move(handlers)); }
    void all(const std::string& path, HandlerList handlers) { router_.all(path, std::move(handlers)); }

    template <class F>
    void use(F middleware) { router_.use(std::move(middleware)); }
    void use(Router& child) { router_.use(child); }
    void use(const std::string& mountPath, Router& child) { router_.use(mountPath, child); }

    template <class F>
    void param(const std::string& name, F callback) { router_.param(name, std::move(callback)); }

    RouteChain route(const std::string& path) { return router_.route(path); }

    void setMergeParams(bool on) { router_.setMergeParams(on); }
    void setStrictRouting(bool on) { router_.setStrictRouting(on); }

    ::cp3::runtime::network::HttpServer& server() { return *server_; }
    Router& router() { return router_; }

    void listen(double port, std::function<void()> callback = {}) { install(); server_->listen(port, callback); }
    void close() const { server_->close(); }
    template <class F>
    void onError(F handler) const { server_->onError(std::move(handler)); }

private:
    void install();
    ::cp3::runtime::network::HttpServer server_;
    Router router_;
    bool installed_ = false;
};

/// Factories spelling the CP3 surface (`express()` / `Router()` at codegen).
inline ExpressApp makeExpressApp() { return ExpressApp(); }
inline ExpressApp makeExpressApp(RouterOptions options) { return ExpressApp(options); }
inline Router makeRouter() { return Router(); }
inline Router makeRouter(bool mergeParams, bool strict) {
    return Router(RouterOptions{mergeParams, strict});
}

}  // namespace cp3::runtime::network::express
