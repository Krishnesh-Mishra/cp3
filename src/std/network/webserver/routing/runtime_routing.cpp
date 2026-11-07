// =============================================================================
// src/std/network/webserver/routing/runtime_routing.cpp - webserver complex routing.
//
// OWNERSHIP: the std/network family, the `webserver/routing` sub-family
// (src/std/network/webserver/routing/** ONLY). Standalone implementation of
// runtime_routing.hpp: the segment matcher (with `:name?` optional params and
// the strict trailing-slash toggle), handler-list composition, param-callback
// dispatch, mount stripping, and mergeParams. The shared ServerImpl dispatcher
// (src/std/network/server/runtime_server_dispatch.cpp) is untouched; this file
// only registers through the public HttpServer surface (use/any), so it links
// against the existing network TU set.
// =============================================================================
#include "runtime_routing.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/collections.hpp"

namespace cp3::runtime::network::webserver {

namespace {

using ::cp3::runtime::Promise;
using ::cp3::runtime::network::HttpRequest;
using ::cp3::runtime::network::HttpResponse;
using ::cp3::runtime::network::Next;
using ::cp3::runtime::network::detail::RouteFn;
using ::cp3::runtime::network::StringMap;

std::vector<std::string> splitSegments(const std::string& s) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = s.find('/', start);
        if (slash == std::string::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, slash - start));
        start = slash + 1;
    }
}

// Non-strict (webserver default): a single trailing slash is ignored, so
// '/x/' == '/x' (root '/' is exempt).
std::vector<std::string> normalizedSegments(const std::string& s, bool strict) {
    std::vector<std::string> parts = splitSegments(s);
    if (!strict && parts.size() > 2 && parts.back().empty()) {
        parts.pop_back();
    }
    return parts;
}

bool matchSegments(const std::vector<std::string>& pSegs,
                   const std::vector<std::string>& sSegs, std::size_t pi,
                   std::size_t si,
                   std::vector<std::pair<std::string, std::string>>* captures) {
    if (pi == pSegs.size()) return si == sSegs.size();
    const std::string& seg = pSegs[pi];
    const bool last = pi + 1 == pSegs.size();
    if (seg == "*") {
        if (last) {
            if (si >= sSegs.size()) return false;
            if (captures != nullptr) {
                std::string rest = sSegs[si];
                for (std::size_t k = si + 1; k < sSegs.size(); ++k) {
                    rest += "/";
                    rest += sSegs[k];
                }
                captures->emplace_back("*", rest);
            }
            return true;
        }
        if (si >= sSegs.size()) return false;
        if (captures != nullptr) captures->emplace_back("*", sSegs[si]);
        return matchSegments(pSegs, sSegs, pi + 1, si + 1, captures);
    }
    if (!seg.empty() && seg.front() == ':') {
        std::string name = seg.substr(1);
        bool optional = false;
        if (!name.empty() && name.back() == '?') {
            name.pop_back();
            optional = true;
        }
        // Greedy first, then the skip branch for optional params (backtrack:
        // pop the tentative capture before trying to skip).
        if (si < sSegs.size()) {
            const std::size_t marked = captures != nullptr ? captures->size() : 0;
            if (captures != nullptr) captures->emplace_back(name, sSegs[si]);
            if (matchSegments(pSegs, sSegs, pi + 1, si + 1, captures)) return true;
            if (captures != nullptr) captures->resize(marked);
        }
        if (optional) return matchSegments(pSegs, sSegs, pi + 1, si, captures);
        return false;
    }
    if (si >= sSegs.size() || seg != sSegs[si]) return false;
    return matchSegments(pSegs, sSegs, pi + 1, si + 1, captures);
}

/// Runs `steps` in order; step i's `next` is runFrom(i+1) and exhausting the
/// steps runs `tail`. Sync and async steps follow the dispatcher's model: a
/// pending promise keeps the chain alive, a rejection propagates.
Promise<void> runSteps(std::vector<std::function<Promise<void>(Next&)>> steps,
                       std::function<Promise<void>()> tail, HttpRequest& req,
                       HttpResponse& res) {
    auto state = std::make_shared<std::vector<std::function<Promise<void>(Next&)>>>(
        std::move(steps));
    auto tailShared = std::make_shared<std::function<Promise<void>()>>(std::move(tail));
    // Self-referential driver: stepNext closures invoke (*runFrom)(i+1).
    auto runFrom = std::make_shared<std::function<Promise<void>(std::size_t)>>();
    *runFrom = [runFrom, state, tailShared, &req, &res](std::size_t i) -> Promise<void> {
        if (res.sent()) return Promise<void>::resolve();
        if (i >= state->size()) {
            if (*tailShared) return (*tailShared)();
            return Promise<void>::resolve();
        }
        auto advanced = std::make_shared<bool>(false);
        auto nested = std::make_shared<Promise<void>>(Promise<void>::resolve());
        Next stepNext([runFrom, i, advanced, nested] {
            *advanced = true;
            *nested = (*runFrom)(i + 1);
        });
        Promise<void> promise = (*state)[i](stepNext);
        if (!promise.settled()) {
            return promise.then(
                [advanced, nested]() -> Promise<void> {
                    if (*advanced) return *nested;
                    return Promise<void>::resolve();
                },
                [](::cp3::runtime::Thrown err) -> Promise<void> { throw err; });
        }
        if (promise.rejected()) throw promise.reason();
        if (*advanced) return *nested;
        return Promise<void>::resolve();
    };
    return (*runFrom)(0);
}

}  // namespace

namespace detail_ext {

bool matchPattern(const std::string& pattern, const std::string& path, bool strict,
                  std::vector<std::pair<std::string, std::string>>* captures) {
    if (pattern == "*" || pattern == "/*") return true;
    const std::vector<std::string> pSegs = normalizedSegments(pattern, strict);
    const std::vector<std::string> sSegs = normalizedSegments(path, strict);
    return matchSegments(pSegs, sSegs, 0, 0, captures);
}

RouteFn composeHandlers(HandlerList handlers) {
    return [handlers = std::move(handlers)](HttpRequest& req, HttpResponse& res,
                                            Next& outer) mutable -> Promise<void> {
        std::vector<std::function<Promise<void>(Next&)>> steps;
        steps.reserve(handlers.size());
        for (const RouteFn& handler : handlers) {
            steps.push_back([handler, &req, &res](Next& next) {
                return handler(req, res, next);
            });
        }
        return runSteps(std::move(steps),
                        [&outer] { outer(); return Promise<void>::resolve(); },
                        req, res);
    };
}

}  // namespace detail_ext

// =============================================================================
// RouteChain / Router
// =============================================================================

RouteChain& RouteChain::add(const char* method, HandlerList handlers) {
    router_->add(method, path_, std::move(handlers));
    return *this;
}

void Router::add(const std::string& method, const std::string& path, HandlerList handlers) {
    if (handlers.empty()) {
        handlers.push_back([](HttpRequest&, HttpResponse&, Next&) {
            return Promise<void>::resolve();
        });
    }
    routes_.push_back(Route{method, path, std::move(handlers)});
}

void Router::useImpl(RouteFn handler) {
    routes_.push_back(Route{"*", "*", HandlerList{std::move(handler)}});
}

void Router::use(const std::string& mountPath, Router& child) {
    // Mount stripping happens at request time: the child's routes match the
    // remainder. A mount of '' or '/' disables stripping (child matches
    // everything). The child's own middleware() dispatch serves as the
    // handler, so its next() falls through to the outer chain.
    const std::string mount = mountPath;
    RouteFn childFn = child.middleware();
    RouteFn mounted = [mount, childFn](HttpRequest& req, HttpResponse& res,
                                       Next& outer) mutable -> Promise<void> {
        if (mount.empty() || mount == "/") return childFn(req, res, outer);
        const std::string& path = req.path;
        if (path != mount && path.rfind(mount + "/", 0) != 0) {
            outer();
            return Promise<void>::resolve();
        }
        std::string saved = req.path;
        req.path = path.size() <= mount.size() ? std::string("/")
                                                : path.substr(mount.size());
        Next restoreOuter([&saved, &req, &outer] {
            req.path = saved;
            outer();
        });
        return childFn(req, res, restoreOuter).then(
            [&saved, &req]() {
                req.path = saved;
                return Promise<void>::resolve();
            },
            [&saved, &req](::cp3::runtime::Thrown err) -> Promise<void> {
                req.path = saved;
                throw err;
            });
    };
    useImpl(std::move(mounted));
}

RouteFn Router::middleware() {
    return [this](HttpRequest& req, HttpResponse& res, Next& next) -> Promise<void> {
        return this->dispatch(req, res, next);
    };
}

Promise<void> Router::dispatch(HttpRequest& req, HttpResponse& res, Next& next) {
    // Save the parent params: with mergeParams they stay visible to the child
    // route handlers; without, the handlers see only this router's captures.
    StringMap parent = req.params;
    for (const Route& route : routes_) {
        if (route.method != "*" && route.method != req.method) continue;
        std::vector<std::pair<std::string, std::string>> captures;
        if (!detail_ext::matchPattern(route.pattern, req.path, options_.strict, &captures)) {
            continue;
        }
        StringMap merged = ::cp3::runtime::collections::makeMap<std::string, std::string>();
        if (options_.mergeParams && parent) {
            // `entries()` returns an owning `Array` (a `Ref`); keep it in a
            // named local so the vector outlives the loop (no deref of a
            // prvalue Ref).
            const auto entries = parent->entries();
            for (const auto& entry : *entries) merged->set(entry.first, entry.second);
        }
        for (auto& [name, value] : captures) merged->set(name, value);
        req.setParams(merged);
        // Param callbacks in pattern order, registration order per name.
        std::vector<std::function<Promise<void>(Next&)>> paramSteps;
        for (auto& [name, value] : captures) {
            for (auto& [cbName, cb] : paramCallbacks_) {
                if (cbName != name) continue;
                paramSteps.push_back([cb, value, &req, &res](Next& cbNext) mutable {
                    return cb(req, res, cbNext, value);
                });
            }
        }
        RouteFn chain = detail_ext::composeHandlers(route.handlers);
        if (paramSteps.empty()) return chain(req, res, next);
        return runSteps(std::move(paramSteps),
                        [&chain, &req, &res, &next] { return chain(req, res, next); }, req,
                        res);
    }
    next();
    return Promise<void>::resolve();
}

// =============================================================================
// webserverApp
// =============================================================================

void webserverApp::install() {
    if (installed_) return;
    installed_ = true;
    server_.use([this](HttpRequest& req, HttpResponse& res, Next& next) -> Promise<void> {
        return router_.dispatch(req, res, next);
    });
}

}  // namespace cp3::runtime::network::webserver
