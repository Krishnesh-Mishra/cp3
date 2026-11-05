// =============================================================================
// src/std/network/webserver/router/runtime_router.cpp - webserver `Router()`.
//
// OWNERSHIP: the std/network family (src/std/network/webserver/router/**).
// Self-contained: it is the only TU the Router needs, and it compiles
// against cp3/std/network.hpp alone (no uWS headers, so no vendor flags).
// Selection: add this TU to kNetworkNativePackage's runtimeSources (see
// native.hpp beside this file); a program importing std/network for the
// server already links everything the Router uses.
//
// ---------------------------------------------------------------------------
// DISPATCH MODEL
// ---------------------------------------------------------------------------
// The engine mirrors runtime_server_dispatch.cpp (the server's own chain
// walker): registration order, `next()` continuations, async handlers kept
// alive by their promise, and a chain that ends without a response falling
// through. One difference is deliberate: this router never answers 404.
// When its stack is exhausted the outer `next()` runs exactly once, so
// `app.use(router.handler())` composes with the server chain. A handler
// that throws synchronously propagates to the server dispatcher (which
// answers 500 / runs `app.onError`); an *async* rejection after dispatch
// started is answered 500 here (no deferred promise slot exists to
// forward it through), then the chain stops.
// =============================================================================
#include "cp3/std/network/webserver/router.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cp3::runtime::network::webserver {

namespace detail {
namespace {

using RouterImpl = detail::RouterImpl;
using Layer = RouterImpl::Layer;
using LayerKind = RouterImpl::LayerKind;

// ---------------------------------------------------------------------------
// req.baseUrl / req.originalUrl side table (the header notes explain why
// HttpRequest cannot carry these fields: the shared header is off-limits).
// One entry per request object, on the thread that serves it (the server
// keeps each connection on one loop for its whole lifetime).
// ---------------------------------------------------------------------------
struct UrlState {
    std::string baseUrl;
    std::string originalUrl;
};

std::map<const HttpRequest*, UrlState>& urlStates() {
    static thread_local std::map<const HttpRequest*, UrlState> states;
    return states;
}

std::string lowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = path.find('/', start);
        if (slash == std::string::npos) {
            parts.push_back(path.substr(start));
            return parts;
        }
        parts.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
}

/// Strict routing keeps a trailing slash significant: "/a" and "/a/" differ.
/// Non-strict routing drops trailing slashes (root "/" keeps its slash).
std::string stripTrailingSlashes(std::string text, bool strict) {
    if (strict) return text;
    while (text.size() > 1 && text.back() == '/') text.pop_back();
    return text;
}

bool segmentEquals(const std::string& patternSeg, const std::string& pathSeg,
                   bool caseSensitive) {
    if (caseSensitive) return patternSeg == pathSeg;
    return lowerAscii(patternSeg) == lowerAscii(pathSeg);
}

/// Full-path route match (the server dispatcher's `pathMatches`, with the
/// Router's caseSensitive/strict options). `:name` captures one segment,
/// a `*` segment swallows the remainder (or one segment mid-pattern).
bool matchRoute(const std::string& pattern, const std::string& path,
                const RouterOptions& options,
                std::vector<std::pair<std::string, std::string>>* captures) {
    if (pattern == "*" || pattern == "/*") return true;
    const std::vector<std::string> pSegs =
        splitPath(stripTrailingSlashes(pattern, options.strict));
    const std::vector<std::string> sSegs =
        splitPath(stripTrailingSlashes(path, options.strict));
    std::size_t pi = 0;
    std::size_t si = 0;
    while (pi < pSegs.size()) {
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
            ++pi;
            ++si;
            continue;
        }
        if (si >= sSegs.size()) return false;
        if (!seg.empty() && seg.front() == ':') {
            if (captures != nullptr) {
                captures->emplace_back(seg.substr(1), sSegs[si]);
            }
            ++pi;
            ++si;
            continue;
        }
        if (!segmentEquals(seg, sSegs[si], options.caseSensitive)) return false;
        ++pi;
        ++si;
    }
    return si == sSegs.size();
}

/// Mount-prefix match: the prefix must consume whole leading segments, so
/// "/api" matches "/api" and "/api/x" but not "/apix". `:name` segments in
/// the prefix capture; a trailing `*` swallows the rest. `*`/`/*` mount at
/// the root (no strip, no baseUrl extension). On a match `remainder` is the
/// path the child sees ("/" when nothing is left) and `matched` is the
/// consumed portion of the request path (joined onto baseUrl).
bool matchMount(const std::string& prefix, const std::string& path,
                const RouterOptions& options, std::string& remainder,
                std::vector<std::pair<std::string, std::string>>* captures,
                std::string& matched) {
    if (prefix == "*" || prefix == "/*") {
        remainder = path;
        matched.clear();
        return true;
    }
    const std::vector<std::string> pSegs =
        splitPath(stripTrailingSlashes(prefix, options.strict));
    const std::vector<std::string> sSegs =
        splitPath(stripTrailingSlashes(path, options.strict));
    std::size_t si = 0;
    for (std::size_t pi = 0; pi < pSegs.size(); ++pi) {
        const std::string& seg = pSegs[pi];
        const bool last = pi + 1 == pSegs.size();
        if (seg == "*") {
            // Swallows the remainder; the child sees "/" from here down.
            if (captures != nullptr && si < sSegs.size()) {
                std::string rest = sSegs[si];
                for (std::size_t k = si + 1; k < sSegs.size(); ++k) {
                    rest += "/";
                    rest += sSegs[k];
                }
                captures->emplace_back("*", rest);
            }
            remainder = "/";
            matched = stripTrailingSlashes(path, options.strict);
            return true;
        }
        if (si >= sSegs.size()) return false;
        if (!seg.empty() && seg.front() == ':') {
            if (captures != nullptr) {
                captures->emplace_back(seg.substr(1), sSegs[si]);
            }
            ++si;
            continue;
        }
        if (!segmentEquals(seg, sSegs[si], options.caseSensitive)) return false;
        ++si;
    }
    remainder = "/";
    for (std::size_t k = si; k < sSegs.size(); ++k) {
        if (remainder.size() > 1) remainder += "/";
        remainder += sSegs[k];
    }
    matched = stripTrailingSlashes(path, options.strict);
    if (remainder.size() > 1) {
        matched = matched.substr(0, matched.size() - remainder.size());
    }
    return true;
}

// ---------------------------------------------------------------------------
// The dispatch context: one frame per nesting level, each remembering the
// path/url state to restore when it exhausts (or a prefix-middleware
// placeholder frame, which carries no impl and only restores).
// ---------------------------------------------------------------------------
struct Frame {
    std::shared_ptr<RouterImpl> impl;
    std::size_t index = 0;
    StringMap baseParams;  // mount captures visible here (mergeParams rules)
    std::string savedPath;
    std::string savedBaseUrl;
    bool savesPath = false;
};

struct RouterContext {
    HttpRequest& req;
    HttpResponse& res;
    Next outerNext;
    std::vector<Frame> frames;
    std::size_t pending = 0;
    bool done = false;
    bool outerCalled = false;

    RouterContext(HttpRequest& req_, HttpResponse& res_, Next outer)
        : req(req_), res(res_), outerNext(std::move(outer)) {}
};

void restoreFrame(RouterContext& ctx, const Frame& frame) {
    if (!frame.savesPath) return;
    ctx.req.path = frame.savedPath;
    // Re-trim baseUrl to what it was before the mount/prefix strip. The
    // original never changes after the first mount, so it stays.
    UrlState& state = urlStates()[&ctx.req];
    state.baseUrl = frame.savedBaseUrl;
}

void runChain(const std::shared_ptr<RouterContext>& ctx);

// The outer continuation: runs exactly once, when the root frame exhausts.
void callOuterNext(RouterContext& ctx) {
    if (ctx.outerCalled) return;
    ctx.outerCalled = true;
    ctx.done = true;
    clearRequestUrlState(ctx.req);
    Next next(std::move(ctx.outerNext));
    next();
}

/// Answer 500 for an async handler rejection (see the file notes).
void failAsync(RouterContext& ctx) {
    if (ctx.done) return;
    ctx.done = true;
    if (!ctx.res.sent()) {
        ctx.res.status(500).text("internal server error");
    }
}

void observeHandler(const std::shared_ptr<RouterContext>& ctx,
                    Promise<void> promise) {
    ++ctx->pending;
    promise.then(
        [ctx]() {
            if (ctx->pending != 0) --ctx->pending;
            runChain(ctx);
        },
        [ctx](Thrown) {
            if (ctx->pending != 0) --ctx->pending;
            failAsync(*ctx);
        });
}

/// Invokes one handler with a Next that continues the chain. Returns true
/// when the chain was parked (async) or advanced (a nested next() call is
/// driving it), so the caller must stop its own loop.
bool invokeHandler(const std::shared_ptr<RouterContext>& ctx,
                   const network::detail::RouteFn& handler) {
    auto advanced = std::make_shared<bool>(false);
    Next next([ctx, advanced] {
        *advanced = true;
        runChain(ctx);
    });
    Promise<void> promise = handler(ctx->req, ctx->res, next);
    if (!promise.settled()) {
        observeHandler(ctx, std::move(promise));
        return true;
    }
    if (promise.rejected()) throw promise.reason();
    return *advanced;
}

// ---------------------------------------------------------------------------
// Param callbacks: run in pattern order, then the route handler. The step
// chain lives in a shared_ptr so an async callback's stored next() stays
// valid after runChain returns.
// ---------------------------------------------------------------------------
struct ParamChain {
    std::shared_ptr<RouterContext> ctx;
    Layer layer;
    std::vector<std::pair<std::string, ParamFn>> callbacks;
    std::vector<std::pair<std::string, std::string>> captures;
    std::shared_ptr<bool> advanced;
    bool parked = false;  // an async step owns the chain until it settles
};

void runParamChain(const std::shared_ptr<ParamChain>& chain, std::size_t index);

void runParamStep(const std::shared_ptr<ParamChain>& chain, std::size_t index) {
    if (index >= chain->callbacks.size()) {
        // All param callbacks done: the route handler runs.
        auto advanced = std::make_shared<bool>(false);
        Next next([ctx = chain->ctx, advanced, chain] {
            *advanced = true;
            *chain->advanced = true;
            runChain(ctx);
        });
        Promise<void> promise =
            chain->layer.handler(chain->ctx->req, chain->ctx->res, next);
        if (!promise.settled()) {
            chain->parked = true;
            observeHandler(chain->ctx, std::move(promise));
            return;
        }
        if (promise.rejected()) throw promise.reason();
        (void)*advanced;
        return;
    }
    const std::string name = chain->callbacks[index].first;
    ParamFn fn = chain->callbacks[index].second;
    std::string value;
    for (auto& [capName, capValue] : chain->captures) {
        if (capName == name) value = capValue;
    }
    auto advanced = std::make_shared<bool>(false);
    Next next([chain, advanced, index] {
        *advanced = true;
        *chain->advanced = true;
        runParamChain(chain, index + 1);
    });
    Promise<void> promise =
        fn(chain->ctx->req, chain->ctx->res, next, value);
    if (!promise.settled()) {
        chain->parked = true;
        observeHandler(chain->ctx, std::move(promise));
        return;
    }
    if (promise.rejected()) throw promise.reason();
    (void)*advanced;
}

void runParamChain(const std::shared_ptr<ParamChain>& chain, std::size_t index) {
    runParamStep(chain, index);
}

void runChain(const std::shared_ptr<RouterContext>& ctx) {
    RouterContext& context = *ctx;
    while (!context.done && !context.res.sent()) {
        if (context.frames.empty()) {
            callOuterNext(context);
            return;
        }
        Frame& frame = context.frames.back();
        if (frame.impl == nullptr ||
            frame.index >= frame.impl->stack.size()) {
            restoreFrame(context, frame);
            context.frames.pop_back();
            continue;
        }
        // Copy the layer: a handler may register routes (the vector can
        // reallocate) while it runs.
        const Layer layer = frame.impl->stack[frame.index++];
        const std::shared_ptr<RouterImpl> impl = frame.impl;

        if (layer.kind == LayerKind::Mount) {
            std::string remainder;
            std::string matched;
            std::vector<std::pair<std::string, std::string>> captures;
            if (!matchMount(layer.pattern, context.req.path, impl->options,
                            remainder, &captures, &matched)) {
                continue;
            }
            Frame child;
            child.impl = layer.child;
            child.savedPath = context.req.path;
            child.savedBaseUrl = requestBaseUrlRaw(context.req);
            child.savesPath = true;
            // mergeParams: the mount captures join the child's base params.
            if (layer.child->options.mergeParams) {
                child.baseParams = frame.baseParams;
                for (auto& [name, value] : captures) {
                    child.baseParams->set(name, value);
                }
            }
            setRequestUrlState(context.req, matched,
                               requestOriginalUrlRaw(context.req));
            context.req.path = remainder;
            context.frames.push_back(std::move(child));
            continue;
        }

        if (layer.kind == LayerKind::Middleware && layer.pattern != "*") {
            // Prefix middleware: strip, run, restore on exhaust/next.
            std::string remainder;
            std::string matched;
            std::vector<std::pair<std::string, std::string>> captures;
            if (!matchMount(layer.pattern, context.req.path, impl->options,
                            remainder, &captures, &matched)) {
                continue;
            }
            Frame placeholder;
            placeholder.impl = nullptr;
            placeholder.savedPath = context.req.path;
            placeholder.savedBaseUrl = requestBaseUrlRaw(context.req);
            placeholder.savesPath = true;
            setRequestUrlState(context.req, matched,
                               requestOriginalUrlRaw(context.req));
            context.req.path = remainder;
            context.frames.push_back(std::move(placeholder));
            auto advanced = std::make_shared<bool>(false);
            Next next([ctx, advanced] {
                *advanced = true;
                Frame* top = ctx->frames.empty() ? nullptr : &ctx->frames.back();
                if (top != nullptr && top->impl == nullptr) {
                    restoreFrame(*ctx, *top);
                    ctx->frames.pop_back();
                }
                runChain(ctx);
            });
            Promise<void> promise = layer.handler(context.req, context.res, next);
            if (!promise.settled()) {
                observeHandler(ctx, std::move(promise));
                return;
            }
            if (promise.rejected()) throw promise.reason();
            if (*advanced) return;
            // Settled without next(): the placeholder frame exhausts on
            // the next loop iteration (restore + continue).
            continue;
        }

        if (layer.kind == LayerKind::Middleware) {
            if (invokeHandler(ctx, layer.handler)) return;
            continue;
        }

        // Route layer: method + full-path match.
        if (layer.method != "*" && layer.method != context.req.method) {
            continue;
        }
        std::vector<std::pair<std::string, std::string>> captures;
        if (!matchRoute(layer.pattern, context.req.path, impl->options,
                        &captures)) {
            continue;
        }
        // Fresh params per match: the frame's base params (mergeParams
        // mounts) under the route's own captures.
        StringMap params = frame.baseParams;
        for (auto& [name, value] : captures) params->set(name, value);
        context.req.setParams(params);

        // Param callbacks, in pattern order (registration order breaks
        // ties within one name).
        std::vector<std::pair<std::string, ParamFn>> callbacks;
        for (const std::string& segment : splitPath(layer.pattern)) {
            if (segment.empty() || segment.front() != ':') continue;
            const std::string name = segment.substr(1);
            for (auto& [paramName, paramFn] : impl->paramHandlers) {
                if (paramName == name) callbacks.emplace_back(name, paramFn);
            }
        }
        if (callbacks.empty()) {
            if (invokeHandler(ctx, layer.handler)) return;
            continue;
        }
        auto chain = std::make_shared<ParamChain>();
        chain->ctx = ctx;
        chain->layer = layer;
        chain->callbacks = std::move(callbacks);
        chain->captures = captures;
        chain->advanced = std::make_shared<bool>(false);
        runParamChain(chain, 0);
        // Parked means an async step owns the chain until it settles;
        // advanced means a nested next() already drove it. Either way
        // the stack loop must not continue on its own.
        if (*chain->advanced || chain->parked) return;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Side-table accessors (cp3/std/network/webserver/router.hpp).
// ---------------------------------------------------------------------------
void setRequestUrlState(HttpRequest& req, const std::string& matchedPrefix,
                        const std::string& originalPath) {
    UrlState& state = urlStates()[&req];
    state.baseUrl += matchedPrefix;
    // The first mount pins the original; later mounts must keep it.
    if (state.originalUrl.empty()) state.originalUrl = originalPath;
}

std::string requestBaseUrlRaw(const HttpRequest& req) {
    const auto it = urlStates().find(&req);
    return it == urlStates().end() ? std::string() : it->second.baseUrl;
}

std::string requestOriginalUrlRaw(const HttpRequest& req) {
    const auto it = urlStates().find(&req);
    if (it == urlStates().end() || it->second.originalUrl.empty()) {
        return req.path;
    }
    return it->second.originalUrl;
}

void clearRequestUrlState(const HttpRequest& req) {
    urlStates().erase(&req);
}

// ---------------------------------------------------------------------------
// Entry points.
// ---------------------------------------------------------------------------
Promise<void> dispatchRouter(const std::shared_ptr<RouterImpl>& impl,
                             HttpRequest& req, HttpResponse& res,
                             Next& next) {
    auto ctx = std::make_shared<RouterContext>(req, res, next);
    ctx->frames.push_back(Frame{impl});
    // Pin the original URL at the outermost router so a top-level handler
    // can read req.originalUrl even before any mount.
    setRequestUrlState(req, std::string(), req.path);
    runChain(ctx);
    return Promise<void>::resolve();
}

Promise<void> dispatchMounted(const std::shared_ptr<RouterImpl>& impl,
                              const std::string& mountPath,
                              HttpRequest& req, HttpResponse& res,
                              Next& next) {
    std::string remainder;
    std::string matched;
    std::vector<std::pair<std::string, std::string>> captures;
    if (!matchMount(mountPath, req.path, RouterOptions(), remainder,
                    &captures, &matched)) {
        next();  // not under the mount prefix: straight through
        return Promise<void>::resolve();
    }
    // The mount itself becomes the root frame's saved state: the child
    // runs with the stripped path, and exhaust (+ outer next) restores.
    auto ctx = std::make_shared<RouterContext>(req, res, next);
    Frame root;
    root.impl = impl;
    root.savedPath = req.path;
    root.savedBaseUrl = requestBaseUrlRaw(req);
    root.savesPath = true;
    if (impl->options.mergeParams) {
        for (auto& [name, value] : captures) {
            root.baseParams->set(name, value);
        }
    }
    ctx->frames.push_back(std::move(root));
    setRequestUrlState(req, matched, requestOriginalUrlRaw(req));
    req.path = remainder;
    runChain(ctx);
    return Promise<void>::resolve();
}

}  // namespace detail
}  // namespace cp3::runtime::network::webserver
