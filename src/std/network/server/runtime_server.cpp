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
// replies 404. Request bodies are fully buffered before the chain runs. A
// handler may return a promise (an `async` handler): the dispatcher keeps the
// request alive and waits for it before it continues the chain or answers the
// 404. One handler that throws (a `Thrown` or any native exception) turns into
// a 500 for that request only - the server keeps serving. `listen`/`close`
// hold/release the runtime loop's external count, so a listening server keeps
// the program alive and a closed one lets it exit.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "App.h"
#include "HttpResponse.h"
#include "Loop.h"

#include "cp3/std/json.hpp"

#include "../runtime/runtime_loop.hpp"
#include "runtime_server_internal.hpp"

namespace cp3::runtime::network {
namespace {

using NativeRequest = detail::NativeRequest;
using NativeResponse = detail::NativeResponse;

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

/// Percent-decoding for one query component (`%xx`, and `+` as space), the
/// URLSearchParams reading the documented `query: Map<string, string>` wants.
/// An invalid escape stays literal.
std::string percentDecode(const std::string& text) {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+') {
            out.push_back(' ');
            continue;
        }
        if (c == '%' && i + 2 < text.size()) {
            const int hi = hex(text[i + 1]);
            const int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

/// `key=value&k2=two` -> the documented `query` Map (a later duplicate key
/// wins, exactly like `Map.set`).
StringMap parseQuery(const std::string& text) {
    StringMap map = collections::makeMap<std::string, std::string>();
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t amp = text.find('&', start);
        const std::size_t end = amp == std::string::npos ? text.size() : amp;
        const std::string part = text.substr(start, end - start);
        if (!part.empty()) {
            const std::size_t eq = part.find('=');
            const std::string name =
                percentDecode(eq == std::string::npos ? part : part.substr(0, eq));
            const std::string value =
                eq == std::string::npos ? std::string() : percentDecode(part.substr(eq + 1));
            map->set(name, value);
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return map;
}

/// The request header block as the documented `headers` Map; names lower cased
/// like the JS Headers object.
StringMap requestHeaders(NativeRequest* req) {
    StringMap map = collections::makeMap<std::string, std::string>();
    for (const auto& [key, value] : *req) {
        map->set(lowerAscii(std::string(key)), std::string(value));
    }
    return map;
}

/// `req.cookies`: the `Cookie` header parsed into a map. Cookie pairs are
/// `name=value; ...`; values are NOT percent-decoded (Express's parser
/// keeps them raw unless a decoder is configured).
StringMap parseCookies(const std::string& header) {
    StringMap map = collections::makeMap<std::string, std::string>();
    std::size_t start = 0;
    while (start < header.size()) {
        const std::size_t semi = header.find(';', start);
        const std::size_t end = semi == std::string::npos ? header.size() : semi;
        const std::string part = header.substr(start, end - start);
        const std::size_t eq = part.find('=');
        if (eq != std::string::npos) {
            std::string name = part.substr(0, eq);
            const std::string value = part.substr(eq + 1);
            // Trim the space after the `;` separator.
            const std::size_t first = name.find_first_not_of(' ');
            if (first != std::string::npos) {
                name = name.substr(first, name.find_last_not_of(' ') - first + 1);
            }
            map->set(name, value);
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return map;
}

}  // namespace

namespace detail {

/// State behind one `createServer()`. The uWS App is deliberately leaked (its
/// lifetime is the process, like every uWS context in this runtime); the routes
/// are kept alive by the wildcard handler capturing this impl. The definition
/// lives in runtime_server_internal.hpp so the scaled TU shares it.

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

/// A static-site content type by file extension (`res.sendFile`); unknown
/// extensions are served as bytes.
std::string fileContentType(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    const std::string ext = lowerAscii(path.substr(dot));
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".xml") return "application/xml; charset=utf-8";
    if (ext == ".wasm") return "application/wasm";
    return "application/octet-stream";
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

void HttpResponse::json(const ::cp3::runtime::json::Value& data, double code) {
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

void HttpResponse::write(const buffer::Buffer& chunk) {
    write(chunk.bytes());
}

void HttpResponse::end() {
    if (!impl_ || impl_->finished) return;
    sendBuffered(*impl_, "", true);
}

void HttpResponse::redirect(const std::string& url, double code) {
    if (!impl_ || impl_->finished) return;
    if (code <= 0) code = 302.0;
    impl_->status = code;
    impl_->headers.emplace_back("Location", url);
    sendBuffered(*impl_, "text/plain; charset=utf-8", false);
}

HttpResponse& HttpResponse::cookie(const std::string& setCookieValue) {
    if (impl_) impl_->headers.emplace_back("Set-Cookie", setCookieValue);
    return *this;
}

void HttpResponse::sendFile(const std::string& path) {
    if (!impl_ || impl_->finished) return;
    std::ifstream in(path, std::ios::binary);
    std::string bytes;
    if (in) {
        bytes.assign(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
    } else {
        impl_->status = 404.0;
        impl_->body = "Not Found";
        sendBuffered(*impl_, "text/plain; charset=utf-8", false);
        return;
    }
    impl_->body = bytes;
    sendBuffered(*impl_, detail::fileContentType(path), false);
}

// =============================================================================
// The uWS glue: one wildcard route feeds every request into the dispatcher
// (runtime_server_dispatch.cpp)
// =============================================================================
namespace {

using ServerImpl = detail::ServerImpl;
using detail::PendingRequest;
using detail::dispatch;

void onRequestData(const std::shared_ptr<PendingRequest>& pending, std::string_view chunk,
                   bool isLast) {
    if (pending->aborted) return;
    pending->body.append(chunk.data(), chunk.size());
    if (!isLast) return;
    pending->context.request.setBody(std::move(pending->body));
    dispatch(pending);
}

void onRequest(const std::shared_ptr<ServerImpl>& server, NativeResponse* res,
               NativeRequest* req) {
    auto pending = std::make_shared<PendingRequest>();
    pending->server = server;
    pending->context.server = server.get();
    pending->context.request.method = std::string(req->getCaseSensitiveMethod());
    // `req.path` excludes the query string (docs 04 §network): uWS hands the
    // path and the raw query text separately (`getUrl` / `getQuery`).
    pending->context.request.path = std::string(req->getUrl());
    pending->context.request.setQuery(parseQuery(std::string(req->getQuery())));
    pending->context.request.setHeaders(requestHeaders(req));
    pending->context.request.setCookies(
        parseCookies(std::string(req->getHeader("cookie"))));
    pending->context.request.setRemoteAddress(
        std::string(req->getRemoteAddressAsText()));
    pending->context.response.bindNative(res);
    res->onAborted([pending] {
        // The native response is gone: drop the pointer so a handler that
        // resumes later cannot write through it, and stop the chain.
        pending->aborted = true;
        pending->context.done = true;
        pending->context.response.bindNative(nullptr);
    });
    res->onData([pending](std::string_view chunk, bool isLast) {
        onRequestData(pending, chunk, isLast);
    });
}

}  // namespace

namespace detail {

void installWildcardRoute(const std::shared_ptr<ServerImpl>& impl, uWS::App* app) {
    if (!impl || app == nullptr) return;
    app->any("/*", [impl](NativeResponse* res, NativeRequest* req) {
        onRequest(impl, res, req);
    });
}

}  // namespace detail

// =============================================================================
// HttpServer
// =============================================================================
HttpServer::HttpServer() : impl_(std::make_shared<detail::ServerImpl>()) {
    ensureLoop();
    if (loopHandle() != nullptr) {
        impl_->app = new uWS::App();  // leaked on purpose (process lifetime)
    }
}

void HttpServer::addRouteImpl(const char* method, const std::string& path,
                              detail::RouteFn handler) const {
    if (!impl_ || !handler) return;
    detail::ServerImpl::Route route;
    route.method = method;
    route.pattern = path;
    route.handler = std::move(handler);
    impl_->routes.push_back(std::move(route));
}

void HttpServer::addMiddlewareImpl(detail::RouteFn handler) const {
    if (!impl_ || !handler) return;
    detail::ServerImpl::Route route;
    route.method = "*";
    route.pattern = "*";
    route.handler = std::move(handler);
    impl_->routes.push_back(std::move(route));
}

void HttpServer::listen(double port, std::function<void()> callback) const {
    if (!impl_ || impl_->listening) return;
    if (impl_->scaled) {
        detail::scaledListen(impl_, port, callback);
        return;
    }
    if (impl_->app == nullptr) return;
    impl_->listening = true;
    retainNetwork();
    std::shared_ptr<detail::ServerImpl> impl = impl_;
    detail::installWildcardRoute(impl, impl->app);
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
    if (!impl_ || impl_->closed) return;
    if (impl_->scaled) {
        detail::scaledClose(impl_);
        return;
    }
    if (impl_->app == nullptr) return;
    impl_->closed = true;
    impl_->app->close();  // closes the listen sockets and every connection
    if (impl_->listening) {
        impl_->listening = false;
        releaseNetwork();
    }
}

}  // namespace cp3::runtime::network
