// =============================================================================
// src/std/network/express/middleware/runtime/middleware_engine.cpp
// Express middleware engine: mounting, ordering, next("route"), async,
// 4-arg error dispatch, end-vs-next. See middleware.hpp for the contract.
// OWNERSHIP: src/std/network/express/middleware/** only (SOLO subtree).
// =============================================================================
#include "cp3/std/network/express/middleware.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cp3::runtime::network::express {

std::string normalizeMount(const std::string& mount) {
    if (mount.empty() || mount == "*") return "/";
    std::string out = mount;
    if (out.front() != '/') out = "/" + out;
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

bool mountMatches(const std::string& mount, const std::string& path,
                  std::string* remainder) {
    const std::string m = normalizeMount(mount);
    if (m == "/") {
        if (remainder) *remainder = path.empty() ? "/" : path;
        return true;
    }
    if (path == m) {
        if (remainder) *remainder = "/";
        return true;
    }
    if (path.size() > m.size() && path.compare(0, m.size(), m) == 0 &&
        path[m.size()] == '/') {
        if (remainder) *remainder = path.substr(m.size());
        return true;
    }
    return false;
}

bool routePatternMatches(const std::string& pattern, const std::string& path,
                         std::vector<std::pair<std::string, std::string>>* params) {
    if (pattern == "*" || pattern == "/*") return true;
    auto split = [](const std::string& s) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (;;) {
            const std::size_t slash = s.find('/', start);
            if (slash == std::string::npos) { parts.push_back(s.substr(start)); return parts; }
            parts.push_back(s.substr(start, slash - start));
            start = slash + 1;
        }
    };
    const auto pSegs = split(pattern);
    const auto sSegs = split(path);
    std::size_t pi = 0, si = 0;
    while (pi < pSegs.size()) {
        const std::string& seg = pSegs[pi];
        const bool last = pi + 1 == pSegs.size();
        if (seg == "*") {
            if (last) {
                if (si >= sSegs.size()) return false;
                if (params) {
                    std::string rest = sSegs[si];
                    for (std::size_t k = si + 1; k < sSegs.size(); ++k) { rest += "/"; rest += sSegs[k]; }
                    params->emplace_back("*", rest);
                }
                return true;
            }
            if (si >= sSegs.size()) return false;
            if (params) params->emplace_back("*", sSegs[si]);
            ++pi; ++si; continue;
        }
        if (si >= sSegs.size()) return false;
        if (!seg.empty() && seg.front() == ':') {
            if (params) params->emplace_back(seg.substr(1), sSegs[si]);
            ++pi; ++si; continue;
        }
        if (seg != sSegs[si]) return false;
        ++pi; ++si;
    }
    return si == sSegs.size();
}

namespace detail {
struct Cursor {
    MiddlewareEngine* engine = nullptr;
    HttpRequest* request = nullptr;
    HttpResponse* response = nullptr;
    std::size_t index = 0;
    std::optional<Thrown> pendingError;
    bool done = false;
    std::function<void(HttpRequest&, HttpResponse&)> exhaustedHook_;
    std::function<void(Thrown, HttpRequest&, HttpResponse&)> unhandledHook_;
};
}  // namespace detail

namespace {
using detail::Cursor;

Thrown toThrown(const std::exception& e) { return Thrown::make(e.what()); }

void runLoop(std::shared_ptr<Cursor> cursor, Promise<void> donePromise);

void finishLoop(std::shared_ptr<Cursor> cursor) {
    if (cursor->done) return;
    cursor->done = true;
    if (cursor->response->sent()) return;
    if (cursor->pendingError) {
        // Default: 500 (overridden by MiddlewareEngine::onUnhandledError,
        // which dispatch() installs into the cursor before running).
        if (cursor->unhandledHook_) {
            cursor->unhandledHook_(*cursor->pendingError, *cursor->request, *cursor->response);
        } else {
            cursor->response->status(500).text("internal server error");
        }
        return;
    }
    if (cursor->exhaustedHook_) {
        cursor->exhaustedHook_(*cursor->request, *cursor->response);
    } else {
        cursor->response->status(404).text("not found");
    }
}

void runLoop(std::shared_ptr<Cursor> cursor, Promise<void> /*donePromise*/) {
    if (cursor->done) return;
    if (cursor->response->sent()) { cursor->done = true; return; }
    const std::vector<Layer>& layers = cursor->engine->layers();
    while (!cursor->done && !cursor->response->sent() && cursor->index < layers.size()) {
        const Layer layer = layers[cursor->index];  // copy: handlers may add layers
        const std::size_t layerIndex = cursor->index++;
        const bool wantError = cursor->pendingError.has_value();
        if (wantError != layer.isError) continue;
        if (layer.isRoute) {
            if (layer.method != "*" && layer.method != cursor->request->method) continue;
            std::vector<std::pair<std::string, std::string>> captures;
            if (!routePatternMatches(layer.pattern, cursor->request->path, &captures)) continue;
            StringMap params = collections::makeMap<std::string, std::string>();
            for (auto& [name, value] : captures) params->set(name, value);
            cursor->request->setParams(std::move(params));
        } else {
            if (!mountMatches(layer.mount, cursor->request->path)) continue;
        }
        auto advanced = std::make_shared<bool>(false);
        ExpressNext next(cursor, layerIndex, layer.routeGroup, layer.isError, advanced);
        Promise<void> promise;
        try {
            if (layer.isError) {
                promise = layer.errorMiddleware(*cursor->pendingError, *cursor->request,
                                                *cursor->response, next);
            } else {
                promise = layer.middleware(*cursor->request, *cursor->response, next);
            }
        } catch (const Thrown& t) {
            cursor->pendingError = t;
            continue;  // seek next error layer from here
        } catch (const std::exception& e) {
            cursor->pendingError = toThrown(e);
            continue;
        } catch (...) {
            cursor->pendingError = Thrown::make("internal server error");
            continue;
        }
        if (!promise.settled()) {
            // Async handler: wait, then resume. If it already called next(),
            // the nested runLoop owns the chain (advanced flag).
            std::weak_ptr<Cursor> weak = cursor;
            promise.then(
                [weak, advanced] {
                    if (auto c = weak.lock()) {
                        if (!*advanced && !c->done && !c->response->sent()) runLoop(c, Promise<void>::resolve());
                        else if (!c->done && !c->response->sent() && c->index >= c->engine->layers().size()) finishLoop(c);
                    }
                },
                [weak, advanced](Thrown err) {
                    if (auto c = weak.lock()) {
                        if (c->done || c->response->sent()) return;
                        if (*advanced) { c->pendingError = err; runLoop(c, Promise<void>::resolve()); return; }
                        c->pendingError = err;
                        runLoop(c, Promise<void>::resolve());
                    }
                });
            return;  // resume happens in the continuation
        }
        if (promise.rejected()) {
            cursor->pendingError = promise.reason();
            continue;
        }
        if (*advanced) return;  // next() already drove the chain
        if (cursor->response->sent()) { cursor->done = true; return; }
        // Handler neither sent nor called next(): chain proceeds to the next
        // layer (matches the existing HttpServer dispatcher behaviour).
    }
    if (!cursor->done && !cursor->response->sent() && cursor->index >= layers.size()) {
        finishLoop(cursor);
    } else if (cursor->response->sent()) {
        cursor->done = true;
    }
}
}  // namespace

// --- ExpressNext -----------------------------------------------------------
ExpressNext::ExpressNext(std::shared_ptr<detail::Cursor> cursor, std::size_t layerIndex,
                         std::size_t routeGroup, bool isErrorLayer,
                         std::shared_ptr<bool> advanced)
    : cursor_(std::move(cursor)), layerIndex_(layerIndex), routeGroup_(routeGroup),
      isErrorLayer_(isErrorLayer), advanced_(std::move(advanced)) {}

void ExpressNext::operator()() {
    if (!cursor_ || cursor_->done || cursor_->response->sent()) return;
    if (advanced_ && *advanced_) return;  // double next(): ignored
    if (advanced_) *advanced_ = true;
    if (isErrorLayer_) cursor_->pendingError.reset();  // error next() resumes normal
    runLoop(cursor_, Promise<void>::resolve());
}
void ExpressNext::operator()(const std::string& token) {
    if (token == "route") { route(); return; }
    (*this)();
}
void ExpressNext::operator()(Thrown err) { error(std::move(err)); }
void ExpressNext::route() {
    if (!cursor_ || cursor_->done || cursor_->response->sent()) return;
    if (advanced_ && *advanced_) return;
    if (advanced_) *advanced_ = true;
    if (isErrorLayer_) cursor_->pendingError.reset();
    if (routeGroup_ != 0) {
        const auto& layers = cursor_->engine->layers();
        while (cursor_->index < layers.size() && layers[cursor_->index].routeGroup == routeGroup_) {
            ++cursor_->index;
        }
    }
    runLoop(cursor_, Promise<void>::resolve());
}
void ExpressNext::error(Thrown err) {
    if (!cursor_ || cursor_->done || cursor_->response->sent()) return;
    if (advanced_ && *advanced_) return;
    if (advanced_) *advanced_ = true;
    cursor_->pendingError = std::move(err);
    runLoop(cursor_, Promise<void>::resolve());
}

// --- MiddlewareEngine ------------------------------------------------------
void MiddlewareEngine::addMiddleware(const std::string& mount, MiddlewareFn fn) {
    if (!fn) return;
    Layer layer;
    layer.mount = normalizeMount(mount);
    layer.middleware = std::move(fn);
    layers_.push_back(std::move(layer));
}
void MiddlewareEngine::addErrorMiddleware(const std::string& mount, ErrorMiddlewareFn fn) {
    if (!fn) return;
    Layer layer;
    layer.mount = normalizeMount(mount);
    layer.isError = true;
    layer.errorMiddleware = std::move(fn);
    layers_.push_back(std::move(layer));
}
void MiddlewareEngine::route(const std::string& method, const std::string& pattern,
                             std::vector<RouteStackFn> handlers) {
    const std::size_t group = nextRouteGroup_++;
    for (auto& h : handlers) {
        if (!h) continue;
        Layer layer;
        layer.isRoute = true;
        layer.method = method;
        layer.pattern = pattern;
        layer.routeGroup = group;
        layer.middleware = std::move(h);
        layers_.push_back(std::move(layer));
    }
}

Promise<void> MiddlewareEngine::dispatch(HttpRequest& req, HttpResponse& res) {
    auto cursor = std::make_shared<Cursor>();
    cursor->engine = this;
    cursor->request = &req;
    cursor->response = &res;
    cursor->exhaustedHook_ = exhausted_;
    cursor->unhandledHook_ = unhandled_;
    // Keep the finish promise observable: resolve it when the loop ends.
    // The chain itself is synchronous except for async handlers, whose
    // continuations keep `cursor` alive via weak_ptr/shared captures.
    auto finish = std::make_shared<Promise<void>>(Promise<void>::pending());
    // Re-wire finishLoop to resolve: wrap hooks so settlement is visible.
    auto origExhausted = cursor->exhaustedHook_;
    auto origUnhandled = cursor->unhandledHook_;
    cursor->exhaustedHook_ = [this, origExhausted, finish](HttpRequest& r, HttpResponse& s) {
        if (origExhausted) origExhausted(r, s);
        else s.status(404).text("not found");
        finish->fulfill();
    };
    cursor->unhandledHook_ = [this, origUnhandled, finish](Thrown e, HttpRequest& r, HttpResponse& s) {
        if (origUnhandled) origUnhandled(e, r, s);
        else s.status(500).text("internal server error");
        finish->fulfill();
    };
    runLoop(cursor, *finish);
    if ((cursor->done || res.sent()) && !finish->settled()) finish->fulfill();
    return *finish;
}

detail::RouteFn MiddlewareEngine::asRouteFn() {
    // The engine is captured by `this`; it must outlive the HttpServer.
    // The underlying server route should be app.use ("*"), so the engine
    // sees every request and its own mounts do the filtering.
    return [this](HttpRequest& req, HttpResponse& res, Next&) -> Promise<void> {
        return dispatch(req, res);
    };
}

}  // namespace cp3::runtime::network::express
