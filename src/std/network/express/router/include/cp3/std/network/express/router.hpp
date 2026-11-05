// =============================================================================
// cp3/std/network/express/router.hpp - Express `Router()` for std/network.
//
// OWNERSHIP: the std/network family (src/std/network/express/router/**).
// This header is self-contained: it adds nothing to cp3/std/network.hpp,
// so the shared generated-code contract is untouched. The Router is a
// `std/network` named import (`import { Router } from "std/network"`);
// sema/codegen fragments for it live beside this header's directory
// (sema_router.cpp / codegen_router.cpp) and are spliced into the network
// phase units by the lead (see the wiring notes at the bottom of those
// files). Generated code only ever sees this header plus network.hpp.
//
// ---------------------------------------------------------------------------
// SURFACE (Express semantics, docs 04 §network's server contract)
// ---------------------------------------------------------------------------
//   const router = new Router();                  // Router() also works
//   router.get("/users/:id", (req, res) => {...});
//   router.post/put/del/patch/head/options(path, handler);
//   router.all(path, handler);                    // every method
//   router.use((req, res, next) => {...});        // middleware, any path
//   router.use("/prefix", middleware);            // prefix middleware
//   router.use("/v1", subRouter);                 // nested router mount
//   router.param("id", (req, res, next, id) => {...});
//   router.route("/x").get(h).post(h);            // chained route object
//   app.use(router.handler());                    // mount at "/" 
//   app.use(router.mountMiddleware("/api"));      // mount at "/api"
//
// Stack discipline is registration order, exactly like HttpServer: a layer
// stops the chain by responding, `next()` continues it, and a router whose
// stack is exhausted without a response calls the outer `next()` (Express
// fallthrough: the server's 404 decides, the router never answers 404).
// Handlers may be `async`; the dispatcher waits for their promise.
//
// ---------------------------------------------------------------------------
// req.baseUrl / req.originalUrl
// ---------------------------------------------------------------------------
// HttpRequest (cp3/std/network.hpp) has no baseUrl/originalUrl fields, and
// this module must not edit that shared header, so the values live in a
// per-request side table (runtime_router.cpp) keyed by the HttpRequest
// object. `requestBaseUrl(req)` / `requestOriginalUrl(req)` are the reads
// codegen lowers `req.baseUrl` / `req.originalUrl` to:
//
//   * baseUrl: the matched mount prefix chain, e.g. "/api/v1" inside a
//     router mounted at /api that itself mounts one at /v1 ("" at the top).
//   * originalUrl: the full request path as the server received it. The
//     server strips the query string into req.query before dispatch, so
//     originalUrl carries no query string (documented limitation).
//
// ---------------------------------------------------------------------------
// OPTIONS (Express `new Router(options)`)
// ---------------------------------------------------------------------------
//   caseSensitive (default false): literal segments compare lower-cased.
//   strict        (default false): a trailing slash is ignored, so "/a"
//                 matches "/a/". With strict true they differ.
//   mergeParams   (default false): a route's params include its mount
//                 prefix's params (mergeParams: true in Express). Without
//                 it req.params holds the route's own captures only.
//
// ---------------------------------------------------------------------------
// NOTES / LIMITS
// ---------------------------------------------------------------------------
//   * `router.use("/prefix", mw)`: mw sees req.path stripped of the prefix
//     (Express behavior) and baseUrl extended; the path is restored when
//     the chain continues past the middleware.
//   * Param callbacks run in the order their `:name` appears in the route
//     pattern, not registration order (Express behavior). A param callback
//     that responds stops the chain; one that calls `next()` continues to
//     the next param callback and then the route handler.
//   * The mount prefix is matched with the *parent* router's options; the
//     child router's own options govern its internal routes.
// =============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::express {

/// Express `new Router(options)`: see the header notes above.
struct RouterOptions {
    bool caseSensitive = false;
    bool strict = false;
    bool mergeParams = false;
};

namespace detail {

/// One `router.param("name", fn)` callback: `(req, res, next, value)`.
using ParamFn = std::function<Promise<void>(HttpRequest&, HttpResponse&,
                                            Next&, const std::string&)>;

/// `router.param(name, fn)` as the dispatcher's uniform callable: sync and
/// async handlers both become a `Promise<void>`. Mirrors
/// `network::detail::makeRoute` / `makeMiddleware` (cp3/std/network.hpp).
template <class F>
ParamFn makeParamHandler(F fn) {
    return [fn = std::move(fn)](HttpRequest& req, HttpResponse& res,
                                Next& next, const std::string& value) mutable
           -> Promise<void> {
        using Result = std::remove_cvref_t<
            std::invoke_result_t<F&, HttpRequest&, HttpResponse&, Next&,
                               const std::string&>>;
        if constexpr (std::is_void_v<Result>) {
            fn(req, res, next, value);
            return Promise<void>::resolve();
        } else if constexpr (::cp3::runtime::detail::is_promise_v<Result>) {
            if constexpr (std::is_void_v<typename Result::value_type>) {
                return fn(req, res, next, value);
            } else {
                return fn(req, res, next, value).then([](auto&&) {});
            }
        } else {
            (void)fn(req, res, next, value);
            return Promise<void>::resolve();
        }
    };
}

/// Router state. Namespace scope (not nested in Router) so the runtime TU
/// can name it without friendship; generated code never does.
struct RouterImpl {
    enum class LayerKind { Route, Middleware, Mount };

    struct Layer {
        LayerKind kind = LayerKind::Middleware;
        std::string method;    // Route only; "*" = all()/use()
        std::string pattern;   // ":name"/"*" segments; "*" = global middleware
        std::shared_ptr<RouterImpl> child;  // Mount only (the sub-router)
        network::detail::RouteFn handler;
    };

    RouterOptions options;
    std::vector<Layer> stack;
    // Param callbacks in registration order (dispatch sorts by pattern order).
    std::vector<std::pair<std::string, ParamFn>> paramHandlers;
};

/// Internal: the side-table writer (runtime_router.cpp). `baseUrl` is
/// appended to whatever the request already carries, `originalUrl` is kept
/// from the first mount (it must stay the full server path).
void setRequestUrlState(HttpRequest& req, const std::string& matchedPrefix,
                        const std::string& originalPath);
/// Internal: the side-table reads behind `requestBaseUrl`/`requestOriginalUrl`
/// when no router has touched the request yet ("" / the current path).
std::string requestBaseUrlRaw(const HttpRequest& req);
std::string requestOriginalUrlRaw(const HttpRequest& req);
/// Internal: drops the side-table entry (the server owns the request; the
/// dispatch calls this when the outermost router frame exhausts).
void clearRequestUrlState(const HttpRequest& req);

/// The dispatcher entry shared by `Router::handle`, `Router::handler`,
/// `Router::mountMiddleware` and Mount layers (runtime_router.cpp).
/// `next` is the outer continuation (server middleware chain or parent
/// router); it runs exactly once when this router's stack is exhausted
/// without a response, and never when a handler responded.
Promise<void> dispatchRouter(const std::shared_ptr<RouterImpl>& impl,
                             HttpRequest& req, HttpResponse& res,
                             Next& next);
/// Mount-prefix entry: matches `mountPath` against req.path, strips it,
/// extends baseUrl, then runs `dispatchRouter` on the child. No match
/// means "not my request": the outer `next()` runs immediately.
Promise<void> dispatchMounted(const std::shared_ptr<RouterImpl>& impl,
                              const std::string& mountPath,
                              HttpRequest& req, HttpResponse& res,
                              Next& next);

}  // namespace detail

// =============================================================================
// req.baseUrl / req.originalUrl reads (codegen: `req.baseUrl` /
// `req.originalUrl`). "" / req.path before any router mount.
// =============================================================================
/// `req.baseUrl`: the mount prefix chain ("" at the top level).
inline std::string requestBaseUrl(HttpRequest& req) {
    return detail::requestBaseUrlRaw(req);
}
/// `req.originalUrl`: the full server path (no query string; see notes).
inline std::string requestOriginalUrl(HttpRequest& req) {
    return detail::requestOriginalUrlRaw(req);
}

// =============================================================================
// Route: the chainable object `router.route(path)` returns.
// `router.route("/x").get(h).post(h)` registers each verb on the owning
// router for the same path. Copies share the owning router's stack.
// =============================================================================
class Route {
public:
    Route() = default;
    Route(std::shared_ptr<detail::RouterImpl> impl, std::string path)
        : impl_(std::move(impl)), path_(std::move(path)) {}

    template <class F>
    Route& get(F handler) {
        return add("GET", std::move(handler));
    }
    template <class F>
    Route& post(F handler) {
        return add("POST", std::move(handler));
    }
    template <class F>
    Route& put(F handler) {
        return add("PUT", std::move(handler));
    }
    template <class F>
    Route& del(F handler) {
        return add("DELETE", std::move(handler));
    }
    template <class F>
    Route& patch(F handler) {
        return add("PATCH", std::move(handler));
    }
    template <class F>
    Route& head(F handler) {
        return add("HEAD", std::move(handler));
    }
    template <class F>
    Route& options(F handler) {
        return add("OPTIONS", std::move(handler));
    }
    template <class F>
    Route& all(F handler) {
        return add("*", std::move(handler));
    }

private:
    template <class F>
    Route& add(const char* method, F handler) {
        if (impl_ && handler) {
            detail::RouterImpl::Layer layer;
            layer.kind = detail::RouterImpl::LayerKind::Route;
            layer.method = method;
            layer.pattern = path_;
            layer.handler =
                network::detail::makeRoute(std::move(handler));
            impl_->stack.push_back(std::move(layer));
        }
        return *this;
    }

    std::shared_ptr<detail::RouterImpl> impl_;
    std::string path_;
};

// =============================================================================
// Router
// =============================================================================
/// Express `Router()` (`new Router()` lowers to this same constructor).
/// Copies share one stack (like HttpServer: the impl is shared), so a
/// router kept in a binding and mounted from two places is one router.
class Router {
public:
    Router() : impl_(std::make_shared<detail::RouterImpl>()) {}

    explicit Router(RouterOptions options)
        : impl_(std::make_shared<detail::RouterImpl>()) {
        impl_->options = options;
    }

    /// Route registration accepts a sync handler (`void`) or an async one
    /// (`Promise<void>`); `network::detail::makeRoute` makes the return
    /// uniform. `del` spells the cp3 `router.delete(...)` surface (`delete`
    /// is a C++ keyword, exactly like `HttpServer::del`).
    template <class F>
    void get(const std::string& path, F handler) const {
        addRouteImpl("GET", path, network::detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void post(const std::string& path, F handler) const {
        addRouteImpl("POST", path, network::detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void put(const std::string& path, F handler) const {
        addRouteImpl("PUT", path, network::detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void del(const std::string& path, F handler) const {
        addRouteImpl("DELETE", path, network::detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void patch(const std::string& path, F handler) const {
        addRouteImpl("PATCH", path, network::detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void head(const std::string& path, F handler) const {
        addRouteImpl("HEAD", path, network::detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void options(const std::string& path, F handler) const {
        addRouteImpl("OPTIONS", path, network::detail::makeRoute(std::move(handler)));
    }
    /// `router.all(path, handler)`: every method.
    template <class F>
    void all(const std::string& path, F handler) const {
        addRouteImpl("*", path, network::detail::makeRoute(std::move(handler)));
    }

    /// `router.use(middleware)`: any path, any method.
    template <class F>
    void use(F middleware) const {
        addMiddlewareImpl("*", network::detail::makeMiddleware(std::move(middleware)));
    }
    /// `router.use("/prefix", middleware)`: prefix middleware; inside the
    /// middleware req.path is stripped of the prefix (Express behavior).
    template <class F>
    void use(const std::string& path, F middleware) const {
        addMiddlewareImpl(path, network::detail::makeMiddleware(std::move(middleware)));
    }
    /// `router.use("/v1", subRouter)`: a nested router mount.
    void use(const std::string& path, const Router& sub) const {
        addMountImpl(path, sub.impl_);
    }
    /// `router.use(subRouter)`: a nested router at "/".
    void use(const Router& sub) const {
        addMountImpl("/", sub.impl_);
    }

    /// `router.param(name, fn)`: runs before every route whose pattern
    /// contains `:name`, with that segment's value.
    template <class F>
    void param(const std::string& name, F handler) const {
        addParamImpl(name, detail::makeParamHandler(std::move(handler)));
    }

    /// `router.route(path)`: the chainable Route for one path.
    Route route(const std::string& path) const {
        return Route(impl_, path);
    }

    /// `router.handle(req, res, next)` (Express `router.handle`): runs this
    /// router's stack against the request. On exhaustion the outer `next`
    /// runs exactly once; a response ends the request without calling it.
    Promise<void> handle(HttpRequest& req, HttpResponse& res, Next& next) const {
        return detail::dispatchRouter(impl_, req, res, next);
    }

    /// The `app.use(...)` form: a middleware that runs this router's stack.
    /// Mounts at "/": `app.use(router.handler())`.
    network::detail::RouteFn handler() const {
        std::shared_ptr<detail::RouterImpl> impl = impl_;
        return [impl](HttpRequest& req, HttpResponse& res,
                      Next& next) mutable -> Promise<void> {
            return detail::dispatchRouter(impl, req, res, next);
        };
    }

    /// The `app.use("/api", router)` form: a middleware that matches the
    /// mount prefix, strips it, extends baseUrl and runs this router's
    /// stack. `app.use(router.mountMiddleware("/api"))`. A request outside
    /// the prefix passes straight to the outer `next()`.
    network::detail::RouteFn mountMiddleware(const std::string& path) const {
        std::shared_ptr<detail::RouterImpl> impl = impl_;
        return [impl, path](HttpRequest& req, HttpResponse& res,
                            Next& next) mutable -> Promise<void> {
            return detail::dispatchMounted(impl, path, req, res, next);
        };
    }

    /// Internal: the shared stack (the use()-mount overloads and the
    /// codegen for `app.use("/api", router)` reach it). Generated code
    /// never calls it.
    const std::shared_ptr<detail::RouterImpl>& impl() const { return impl_; }

private:
    void addRouteImpl(const char* method, const std::string& path,
                      network::detail::RouteFn handler) const {
        if (!impl_ || !handler) return;
        detail::RouterImpl::Layer layer;
        layer.kind = detail::RouterImpl::LayerKind::Route;
        layer.method = method;
        layer.pattern = path;
        layer.handler = std::move(handler);
        impl_->stack.push_back(std::move(layer));
    }
    void addMiddlewareImpl(const std::string& path,
                           network::detail::RouteFn handler) const {
        if (!impl_ || !handler) return;
        detail::RouterImpl::Layer layer;
        layer.kind = detail::RouterImpl::LayerKind::Middleware;
        layer.method = "*";
        layer.pattern = path;
        layer.handler = std::move(handler);
        impl_->stack.push_back(std::move(layer));
    }
    void addMountImpl(const std::string& path,
                      std::shared_ptr<detail::RouterImpl> child) const {
        if (!impl_ || !child) return;
        detail::RouterImpl::Layer layer;
        layer.kind = detail::RouterImpl::LayerKind::Mount;
        layer.method = "*";
        layer.pattern = path;
        layer.child = std::move(child);
        impl_->stack.push_back(std::move(layer));
    }
    void addParamImpl(const std::string& name, detail::ParamFn handler) const {
        if (!impl_ || !handler) return;
        impl_->paramHandlers.emplace_back(name, std::move(handler));
    }

    std::shared_ptr<detail::RouterImpl> impl_;
};

}  // namespace cp3::runtime::network::express
