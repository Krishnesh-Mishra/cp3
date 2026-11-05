// =============================================================================
// src/std/network/runtime_server_dispatch.cpp - route-chain execution.
//
// OWNERSHIP: the std/network family (src/std/network/**). Split out of
// runtime_server.cpp (STYLE.md sec.2 source budget): the single-loop server and
// the D79 scaled server share this dispatcher. It walks `ServerImpl::routes` in
// registration order (webserver semantics), runs `next()` continuations, and
// keeps a request alive across async handlers until their promise settles.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "runtime_server_internal.hpp"

namespace cp3::runtime::network::detail {
namespace {

/// Match `pattern` against `path` segment by segment (ledger H wildcards):
///   * literal segments must be byte-equal;
///   * `:name` segments swallow one path segment and record it under `name`
///     (used by the `req.params` Map);
///   * a `*` segment (a whole segment exactly "*") is a wildcard: as the LAST
///     segment it swallows the remainder of the path (any number of segments,
///     slashes included), and mid-pattern it swallows exactly one segment.
///     The swallowed text is recorded under the capture name "*" (a repeated
///     wildcard overwrites). A trailing `/*` needs the `/` in the path, so
///     `/files/*` matches `/files/a/b` but not bare `/files` (webserver rule).
///   * `*` and `/*` as the whole pattern match any path; a `*` inside a
///     larger segment is NOT a wildcard (it compares literally).
bool pathMatches(const std::string& pattern, const std::string& path,
                 std::vector<std::pair<std::string, std::string>>* params) {
    if (pattern == "*" || pattern == "/*") return true;
    // Split both on '/', keeping empty segments (leading '/' -> first "",
    // trailing '/' -> last ""), so "//" and trailing slashes compare exactly.
    auto split = [](const std::string& s) {
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
    };
    const std::vector<std::string> pSegs = split(pattern);
    const std::vector<std::string> sSegs = split(path);
    std::size_t pi = 0;
    std::size_t si = 0;
    while (pi < pSegs.size()) {
        const std::string& seg = pSegs[pi];
        const bool last = pi + 1 == pSegs.size();
        if (seg == "*") {
            if (last) {
                // Remainder (steps onto at least one remaining segment).
                if (si >= sSegs.size()) return false;
                if (params != nullptr) {
                    std::string rest = sSegs[si];
                    for (std::size_t k = si + 1; k < sSegs.size(); ++k) {
                        rest += "/";
                        rest += sSegs[k];
                    }
                    params->emplace_back("*", rest);
                }
                return true;
            }
            // Mid-pattern: exactly one segment.
            if (si >= sSegs.size()) return false;
            if (params != nullptr) {
                params->emplace_back("*", sSegs[si]);
            }
            ++pi;
            ++si;
            continue;
        }
        if (si >= sSegs.size()) return false;
        if (!seg.empty() && seg.front() == ':') {
            if (params != nullptr) {
                params->emplace_back(seg.substr(1), sSegs[si]);
            }
            ++pi;
            ++si;
            continue;
        }
        if (seg != sSegs[si]) return false;
        ++pi;
        ++si;
    }
    return si == sSegs.size();
}

void runChain(const std::shared_ptr<PendingRequest>& pending);
void continueChain(const std::shared_ptr<PendingRequest>& pending);

/// Answer 404 when the chain is exhausted and nothing is still in flight.
void finishChain(RequestContext& ctx) {
    if (ctx.done) return;
    if (ctx.pending != 0) return;
    if (ctx.response.sent()) {
        ctx.done = true;
        return;
    }
    if (ctx.index < ctx.server->routes.size()) return;
    ctx.done = true;
    ctx.response.status(404).text("not found");
}

/// One handler threw or its promise rejected: 500 for THIS request only.
void failRequest(RequestContext& ctx) {
    if (ctx.done) return;
    ctx.done = true;
    if (!ctx.response.sent()) {
        ctx.response.status(500).text("internal server error");
    }
}

/// Ledger L (appended): route a failure through `app.onError(...)` when one
/// is registered and nothing was sent yet; otherwise the default 500.
void failRequestWithError(const std::shared_ptr<PendingRequest>& pending, Thrown err) {
    RequestContext& ctx = pending->context;
    if (ctx.done) return;
    if (ctx.response.sent() || ctx.server == nullptr || !ctx.server->errorHandler) {
        failRequest(ctx);
        return;
    }
    ctx.done = true;
    try {
        Promise<void> promise = ctx.server->errorHandler(err, ctx.request, ctx.response);
        if (!promise.settled()) {
            promise.then([] {}, [pending](Thrown) {
                if (!pending->context.response.sent()) {
                    pending->context.response.status(500).text("internal server error");
                }
            });
        } else if (promise.rejected()) {
            throw promise.reason();
        }
    } catch (...) {
        if (!ctx.response.sent()) ctx.response.status(500).text("internal server error");
    }
}

/// Keep the request alive until `promise` settles, then resume the chain.
void observeHandler(const std::shared_ptr<PendingRequest>& pending,
                    Promise<void> promise) {
    ++pending->context.pending;
    promise.then(
        [pending]() {
            if (pending->context.pending != 0) --pending->context.pending;
            continueChain(pending);
        },
        [pending](Thrown err) {
            if (pending->context.pending != 0) --pending->context.pending;
            failRequestWithError(pending, err);
        });
}

void continueChain(const std::shared_ptr<PendingRequest>& pending) {
    RequestContext& ctx = pending->context;
    if (ctx.done) return;
    try {
        runChain(pending);
        finishChain(ctx);
    } catch (...) {
        failRequestWithError(pending, Thrown::make("internal server error"));
    }
}

void runChain(const std::shared_ptr<PendingRequest>& pending) {
    RequestContext& ctx = pending->context;
    while (!ctx.done && !ctx.response.sent() &&
           ctx.index < ctx.server->routes.size()) {
        // Copy the route: a handler may register more routes (the vector can
        // reallocate) while this one is being invoked.
        const ServerImpl::Route route = ctx.server->routes[ctx.index++];
        if (route.method != "*" && route.method != ctx.request.method) continue;
        std::vector<std::pair<std::string, std::string>> captures;
        if (!pathMatches(route.pattern, ctx.request.path, &captures)) continue;
        // The matched route's `:name` parameters as one fresh Map per match, so
        // a chain that runs through several routes never sees stale captures.
        StringMap params = collections::makeMap<std::string, std::string>();
        for (auto& [name, value] : captures) params->set(name, value);
        ctx.request.setParams(std::move(params));
        // webserver `app.param(name, fn)` (routing/router agents): run the
        // callbacks for captured names, in capture order, before the route
        // handler. Each callback's next() advances; a pending promise defers
        // the rest of the chain (via observeHandler on resume).
        {
            std::vector<std::pair<ParamFn, std::string>> steps;
            for (auto& [cname, cvalue] : captures) {
                for (auto& [pname, pf] : ctx.server->paramHandlers) {
                    if (pname == cname) steps.emplace_back(pf, cvalue);
                }
            }
            if (!steps.empty()) {
                auto stepsShared = std::make_shared<std::vector<std::pair<ParamFn, std::string>>>(std::move(steps));
                auto routeShared = std::make_shared<ServerImpl::Route>(route);
                auto runStep = std::make_shared<std::function<void(std::size_t)>>();
                *runStep = [pending, stepsShared, routeShared, runStep](std::size_t idx) {
                    RequestContext& c = pending->context;
                    if (c.done || c.response.sent()) return;
                    if (idx >= stepsShared->size()) {
                        auto advanced = std::make_shared<bool>(false);
                        Next next([pending, advanced] { *advanced = true; runChain(pending); });
                        Promise<void> promise = (*routeShared).handler(c.request, c.response, next);
                        if (!promise.settled()) { observeHandler(pending, std::move(promise)); return; }
                        if (promise.rejected()) throw promise.reason();
                        if (*advanced) return;
                        return;
                    }
                    auto advanced = std::make_shared<bool>(false);
                    Next next([runStep, idx, advanced] { *advanced = true; (*runStep)(idx + 1); });
                    Promise<void> promise = (*stepsShared)[idx].first(c.request, c.response, next, (*stepsShared)[idx].second);
                    if (!promise.settled()) {
                        ++c.pending;
                        promise.then([pending, runStep, idx, advanced]() { if (pending->context.pending != 0) --pending->context.pending; if (*advanced) return; (*runStep)(idx + 1); continueChain(pending); }, [pending](Thrown err) { if (pending->context.pending != 0) --pending->context.pending; failRequestWithError(pending, err); });
                        return;
                    }
                    if (promise.rejected()) throw promise.reason();
                    if (*advanced) return;
                    (*runStep)(idx + 1);
                    return;
                };
                try { (*runStep)(0); } catch (...) { failRequestWithError(pending, Thrown::make("internal server error")); }
                return;
            }
        }
        // `next()` owns chain progress from here. The flag lives on the heap so
        // an async handler that stores its `next` and calls it later is safe.
        auto advanced = std::make_shared<bool>(false);
        Next next([pending, advanced] {
            *advanced = true;
            runChain(pending);
        });
        Promise<void> promise = route.handler(ctx.request, ctx.response, next);
        if (!promise.settled()) {
            observeHandler(pending, std::move(promise));
            return;
        }
        if (promise.rejected()) throw promise.reason();
        if (*advanced) return;  // the nested next() call drives the chain
    }
}

}  // namespace

void dispatch(const std::shared_ptr<PendingRequest>& pending) {
    continueChain(pending);
}

}  // namespace cp3::runtime::network::detail
