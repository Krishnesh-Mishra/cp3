// =============================================================================
// src/std/network/runtime_server_dispatch.cpp - route-chain execution.
//
// OWNERSHIP: the std/network family (src/std/network/**). Split out of
// runtime_server.cpp (STYLE.md sec.2 source budget): the single-loop server and
// the D79 scaled server share this dispatcher. It walks `ServerImpl::routes` in
// registration order (Express semantics), runs `next()` continuations, and
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

/// `*` and `/*` match any path; `:name` segments swallow one path segment and
/// record the captured value under `name` (used by the `req.params` Map).
bool pathMatches(const std::string& pattern, const std::string& path,
                 std::vector<std::pair<std::string, std::string>>* params) {
    if (pattern == "*" || pattern == "/*") return true;
    std::size_t pi = 0;
    std::size_t si = 0;
    while (pi < pattern.size() && si < path.size()) {
        if (pattern[pi] == ':') {
            const std::size_t nameStart = ++pi;
            while (pi < pattern.size() && pattern[pi] != '/') ++pi;
            const std::size_t valueStart = si;
            while (si < path.size() && path[si] != '/') ++si;
            if (params != nullptr) {
                params->emplace_back(pattern.substr(nameStart, pi - nameStart),
                                     path.substr(valueStart, si - valueStart));
            }
            continue;
        }
        if (pattern[pi] != path[si]) return false;
        ++pi;
        ++si;
    }
    return pi == pattern.size() && si == path.size();
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

/// Keep the request alive until `promise` settles, then resume the chain.
void observeHandler(const std::shared_ptr<PendingRequest>& pending,
                    Promise<void> promise) {
    ++pending->context.pending;
    promise.then(
        [pending]() {
            if (pending->context.pending != 0) --pending->context.pending;
            continueChain(pending);
        },
        [pending](Thrown) {
            if (pending->context.pending != 0) --pending->context.pending;
            failRequest(pending->context);
        });
}

void continueChain(const std::shared_ptr<PendingRequest>& pending) {
    RequestContext& ctx = pending->context;
    if (ctx.done) return;
    try {
        runChain(pending);
        finishChain(ctx);
    } catch (...) {
        failRequest(ctx);
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
