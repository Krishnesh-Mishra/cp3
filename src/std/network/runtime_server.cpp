// =============================================================================
// src/std/network/runtime_server.cpp - `std/network` HTTP server.
//
// OWNERSHIP: the std/network family (src/std/network/**). Same selection rule as
// runtime.cpp (the std/network native package row, D68).
//
// The server is one uWS App per `createServer()` with a single wildcard route
// (`any("/*")`) and the routing itself lives here: routes and middleware run in
// registration order (Express semantics), a handler stops the chain by
// answering, `next()` continues it, and a chain that ends without an answer
// replies 404. Request bodies are fully buffered before the chain runs, so
// every handler can answer synchronously, and one handler that throws (a
// `Thrown` or any native exception) turns into a 500 for that request only -
// the server keeps serving. `listen`/`close` hold/release the runtime loop's
// external count, so a listening server keeps the program alive and a closed
// one lets it exit.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "App.h"
#include "HttpResponse.h"
#include "Loop.h"

#include "cp3/std/json.hpp"

#include "runtime_loop.hpp"

namespace cp3::runtime::network {
namespace {

using NativeRequest = uWS::HttpRequest;
using NativeResponse = uWS::HttpResponse<false>;

std::string lowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string reasonPhrase(double code) {
    switch (static_cast<int>(code)) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

/// `*` and `/*` match any path; `:name` segments swallow one path segment.
bool pathMatches(const std::string& pattern, const std::string& path) {
    if (pattern == "*" || pattern == "/*") return true;
    std::size_t pi = 0;
    std::size_t si = 0;
    while (pi < pattern.size() && si < path.size()) {
        if (pattern[pi] == ':') {
            while (pi < pattern.size() && pattern[pi] != '/') ++pi;
            while (si < path.size() && path[si] != '/') ++si;
            continue;
        }
        if (pattern[pi] != path[si]) return false;
        ++pi;
        ++si;
    }
    return pi == pattern.size() && si == path.size();
}

}  // namespace

namespace detail {

/// State behind one `createServer()`. The uWS App is deliberately leaked (its
/// lifetime is the process, like every uWS context in this runtime); the routes
/// are kept alive by the wildcard handler capturing this impl.
struct ServerImpl {
    struct Route {
        std::string method;   // "*" = app.use (any method)
        std::string pattern;  // "*" = any path; else literal, `:name` segments
        std::function<void(HttpRequest&, HttpResponse&, Next&)> handler;
    };

    uWS::App* app = nullptr;
    bool listening = false;
    bool closed = false;
    std::vector<Route> routes;
};

/// The buffered response bound to one request.
struct ResponseImpl {
    void* native = nullptr;  // uWS::HttpResponse<false>*
    double status = 200.0;
    bool headStarted = false;
    bool finished = false;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

}  // namespace detail

// =============================================================================
// HttpResponse
// =============================================================================
namespace {

using ResponseImpl = detail::ResponseImpl;

bool hasHeader(const ResponseImpl& impl, const char* name) {
    for (const auto& [key, value] : impl.headers) {
        if (lowerAscii(key) == name) return true;
    }
    return false;
}

/// Writes the status line and the buffered headers exactly once.
NativeResponse* startHead(ResponseImpl& impl) {
    auto* res = static_cast<NativeResponse*>(impl.native);
    if (res == nullptr || impl.headStarted) return res;
    impl.headStarted = true;
    res->writeStatus(std::to_string(static_cast<long long>(impl.status)) + " " +
                     reasonPhrase(impl.status));
    for (const auto& [name, value] : impl.headers) {
        if (lowerAscii(name) == "content-length") continue;  // `end()` owns it
        res->writeHeader(name, value);
    }
    return res;
}

/// Sends `impl.body` as the whole response (Content-Length) or as the tail of a
/// chunked one, adding a content type when the handler set none.
void sendBuffered(ResponseImpl& impl, const std::string& contentType, bool chunked) {
    if (impl.finished) return;
    impl.finished = true;
    if (!contentType.empty() && !hasHeader(impl, "content-type")) {
        impl.headers.emplace_back("Content-Type", contentType);
    }
    NativeResponse* res = startHead(impl);
    if (res == nullptr) return;
    if (chunked) {
        if (!impl.body.empty()) res->write(impl.body);
        res->end();
        return;
    }
    res->end(impl.body);
}

}  // namespace

HttpResponse::HttpResponse() : impl_(std::make_shared<detail::ResponseImpl>()) {}

void HttpResponse::bindNative(void* native) {
    if (impl_) impl_->native = native;
}

bool HttpResponse::sent() const { return impl_ != nullptr && impl_->finished; }

HttpResponse& HttpResponse::status(double code) {
    if (impl_) impl_->status = code;
    return *this;
}

HttpResponse& HttpResponse::header(const std::string& name, const std::string& value) {
    if (impl_) impl_->headers.emplace_back(name, value);
    return *this;
}

void HttpResponse::text(const std::string& data, double code) {
    if (!impl_ || impl_->finished) return;
    if (code > 0) impl_->status = code;
    impl_->body = data;
    sendBuffered(*impl_, "text/plain; charset=utf-8", false);
}

void HttpResponse::json(double data, double code) {
    if (!impl_ || impl_->finished) return;
    if (code > 0) impl_->status = code;
    impl_->body = cp3::runtime::json::stringify(data);
    sendBuffered(*impl_, "application/json; charset=utf-8", false);
}

void HttpResponse::json(const std::string& data, double code) {
    if (!impl_ || impl_->finished) return;
    if (code > 0) impl_->status = code;
    impl_->body = cp3::runtime::json::stringify(data);
    sendBuffered(*impl_, "application/json; charset=utf-8", false);
}

void HttpResponse::json(bool data, double code) {
    if (!impl_ || impl_->finished) return;
    if (code > 0) impl_->status = code;
    impl_->body = cp3::runtime::json::stringify(data);
    sendBuffered(*impl_, "application/json; charset=utf-8", false);
}

void HttpResponse::send(double code) {
    if (!impl_ || impl_->finished) return;
    if (code > 0) impl_->status = code;
    sendBuffered(*impl_, "", false);
}

void HttpResponse::write(const std::string& chunk) {
    if (!impl_ || impl_->finished) return;
    NativeResponse* res = startHead(*impl_);
    if (res == nullptr) return;
    res->write(chunk);  // no content-length written: uWS switches to chunked
}

void HttpResponse::end() {
    if (!impl_ || impl_->finished) return;
    sendBuffered(*impl_, "", true);
}

// =============================================================================
// Routing and dispatch
// =============================================================================
namespace {

using ServerImpl = detail::ServerImpl;

/// One request in flight: the chain walks `routes` in registration order.
struct RequestContext {
    ServerImpl* server = nullptr;
    HttpRequest request;
    HttpResponse response;
    std::size_t index = 0;
};

void runChain(RequestContext& ctx) {
    while (!ctx.response.sent() && ctx.index < ctx.server->routes.size()) {
        const ServerImpl::Route& route = ctx.server->routes[ctx.index++];
        if (route.method != "*" && route.method != ctx.request.method) continue;
        if (!pathMatches(route.pattern, ctx.request.path)) continue;
        Next next([&ctx] { runChain(ctx); });
        route.handler(ctx.request, ctx.response, next);
    }
}

void dispatch(RequestContext& ctx) {
    try {
        runChain(ctx);
        if (!ctx.response.sent()) {
            ctx.response.status(404).text("not found");
        }
    } catch (...) {
        // One handler threw: 500 for THIS request, the server keeps serving.
        if (!ctx.response.sent()) {
            ctx.response.status(500).text("internal server error");
        }
    }
}

void addRoute(const std::shared_ptr<ServerImpl>& impl, const char* method,
              const std::string& path, const HttpServer::RouteHandler& handler) {
    if (!impl || !handler) return;
    ServerImpl::Route route;
    route.method = method;
    route.pattern = path;
    route.handler = [handler](HttpRequest& req, HttpResponse& res, Next&) { handler(req, res); };
    impl->routes.push_back(std::move(route));
}

// --- the uWS glue: one wildcard route feeds every request in here ------------
struct PendingRequest {
    std::shared_ptr<ServerImpl> server;
    RequestContext context;
    std::string body;
    bool aborted = false;
};

void onRequestData(const std::shared_ptr<PendingRequest>& pending, std::string_view chunk,
                   bool isLast) {
    if (pending->aborted) return;
    pending->body.append(chunk.data(), chunk.size());
    if (!isLast) return;
    pending->context.request.setBody(std::move(pending->body));
    dispatch(pending->context);
}

void onRequest(const std::shared_ptr<ServerImpl>& server, NativeResponse* res,
               NativeRequest* req) {
    auto pending = std::make_shared<PendingRequest>();
    pending->server = server;
    pending->context.server = server.get();
    pending->context.request.method = std::string(req->getCaseSensitiveMethod());
    pending->context.request.path = std::string(req->getUrl());
    pending->context.response.bindNative(res);
    res->onAborted([pending] { pending->aborted = true; });
    res->onData([pending](std::string_view chunk, bool isLast) {
        onRequestData(pending, chunk, isLast);
    });
}

}  // namespace

// =============================================================================
// HttpServer
// =============================================================================
HttpServer::HttpServer() : impl_(std::make_shared<detail::ServerImpl>()) {
    ensureLoop();
    if (loopHandle() != nullptr) {
        impl_->app = new uWS::App();  // leaked on purpose (process lifetime)
    }
}

void HttpServer::get(const std::string& path, RouteHandler handler) const {
    addRoute(impl_, "GET", path, handler);
}

void HttpServer::post(const std::string& path, RouteHandler handler) const {
    addRoute(impl_, "POST", path, handler);
}

void HttpServer::put(const std::string& path, RouteHandler handler) const {
    addRoute(impl_, "PUT", path, handler);
}

void HttpServer::del(const std::string& path, RouteHandler handler) const {
    addRoute(impl_, "DELETE", path, handler);
}

void HttpServer::use(Middleware middleware) const {
    if (!impl_ || !middleware) return;
    detail::ServerImpl::Route route;
    route.method = "*";
    route.pattern = "*";
    route.handler = std::move(middleware);
    impl_->routes.push_back(std::move(route));
}

void HttpServer::listen(double port, std::function<void()> callback) const {
    if (!impl_ || impl_->app == nullptr || impl_->listening) return;
    impl_->listening = true;
    retainNetwork();
    std::shared_ptr<detail::ServerImpl> impl = impl_;
    impl->app->any("/*", [impl](NativeResponse* res, NativeRequest* req) {
        onRequest(impl, res, req);
    });
    const int bound = static_cast<int>(port);
    try {
        impl->app->listen(bound, [callback](us_listen_socket_t* socket) {
            if (socket == nullptr) {
                throw Thrown::make("std/network: could not listen (port already in use?)");
            }
            if (callback) callback();
        });
    } catch (...) {
        // Binding failed: undo the pending-work retain before rethrowing.
        impl->listening = false;
        releaseNetwork();
        throw;
    }
}

void HttpServer::close() const {
    if (!impl_ || impl_->app == nullptr || impl_->closed) return;
    impl_->closed = true;
    impl_->app->close();  // closes the listen sockets and every connection
    if (impl_->listening) {
        impl_->listening = false;
        releaseNetwork();
    }
}

}  // namespace cp3::runtime::network
