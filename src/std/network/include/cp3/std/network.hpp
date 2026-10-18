// =============================================================================
// cp3/std/network.hpp - `std/network` backing surface.
//
// OWNERSHIP: the std/network family (src/std/network/**). The header is the
// generated-code contract, so keep it additive like every cp3/std header, D14.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 §network)
// -----------------------------------------------------------------------------
//   fetch(url, options)            -> http:// only in beta one (TLS off, D70)
//   createServer(...)              -> uWebSockets-backed HTTP server
//   WebSocket / WebSocketServer    -> uSockets/uWS framing, same event loop
//   Named imports only:
//     import { fetch, createServer, WebSocket, WebSocketServer } from "std/network";
//
// Backend: libuv v1.53.0 + uWebSockets v20.80.0 (third_party/**, D70). The
// implementation TUs live in src/std/network/ and are compiled + linked
// only when a program imports "std/network"
// (src/std/network/native.hpp, D68).
//
// -----------------------------------------------------------------------------
// EVENT-LOOP MODEL
// -----------------------------------------------------------------------------
//   * One libuv loop per thread, created by the network module itself and
//     handed to uWS with `uWS::Loop::get(uvLoop)`, so uSockets borrows it
//     (cleanMe = false) instead of owning it and the runtime's EventLoop stays
//     authoritative: it drives libuv from its idle phase.
//   * A live fetch, a listening server and an open socket each retain the
//     runtime loop, so the program cannot exit while that work is pending -
//     the §14 "pending work" rule, unchanged.
//   * Handlers run synchronously on the runtime thread; an `async` handler
//     arrow is not representable yet (CP3002), so `req.text()` / `req.json<T>()`
//     exist for surface completeness but cannot be awaited by a handler today.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"

namespace cp3::runtime::network {

/// Implementation state. Namespace scope (not nested in the public classes) so
/// the private TUs can name it without friendship; generated code never does.
namespace detail {
struct ResponseImpl;
struct ServerImpl;
struct WssImpl;
struct WsImpl;
}  // namespace detail

// =============================================================================
// JSON scalar readers (shared by `res.json<T>()` / `req.json<T>()`)
// =============================================================================
namespace detail {

/// Parses `text` (whitespace around one JSON value allowed) and returns the
/// string / number / boolean it holds. A payload that is not a matching scalar
/// throws a `Thrown` carrying a message, so an `await` rejects like a JS parse
/// error would. Objects, arrays and `null` are out of scope (JSON.parse is a
/// later wave; see cp3/std/json.hpp).
std::string jsonString(const std::string& text);
double jsonNumber(const std::string& text);
bool jsonBool(const std::string& text);

}  // namespace detail

// =============================================================================
// fetch
// =============================================================================
/// The `fetch(url, init)` options an object literal can spell today. `headers`
/// would need the Map type (MISSING); `method`, `body` and `timeout` are
/// carried. `timeoutMs` of 0 means "no timeout" (JS omits it; a call that never
/// answers keeps the loop alive, exactly like a never-settled timer).
struct FetchInit {
    std::string method = "GET";
    std::string body;
    bool hasBody = false;
    double timeoutMs = 0.0;
};

/// The `FetchResponse` a resolved `fetch` fulfils with. The body is fully
/// buffered before the promise settles, so the readers never reject.
class FetchResponse {
public:
    FetchResponse() = default;
    FetchResponse(double statusCode, std::string statusText_, std::string body)
        : ok(statusCode >= 200.0 && statusCode < 300.0),
          status(statusCode),
          statusText(std::move(statusText_)),
          body_(std::move(body)) {}

    /// `res.ok` / `res.status` / `res.statusText`. `res.headers` needs the Map
    /// type (MISSING; see the wave report).
    bool ok = false;
    double status = 0.0;
    std::string statusText;

    /// `await res.text()`.
    Promise<std::string> text() const { return Promise<std::string>::resolve(body_); }

    /// `await res.arrayBuffer()`; Buffer is a string in M1, so this is the same
    /// bytes as `text()`.
    Promise<std::string> arrayBuffer() const { return Promise<std::string>::resolve(body_); }

    /// `await res.json<T>()` for T = number | string | boolean; any other T is
    /// not representable (JSON.parse is a later wave).
    template <class T>
    Promise<T> json() const {
        if constexpr (std::is_same_v<T, std::string>) {
            return Promise<std::string>::resolve(detail::jsonString(body_));
        } else if constexpr (std::is_same_v<T, double>) {
            return Promise<double>::resolve(detail::jsonNumber(body_));
        } else if constexpr (std::is_same_v<T, bool>) {
            return Promise<bool>::resolve(detail::jsonBool(body_));
        } else {
            static_assert(sizeof(T) == 0,
                          "std/network: json<T>() supports number, string and boolean");
        }
    }

private:
    std::string body_;
};

/// `fetch(url, init?)`; rejects when the URL is not `http://`, when DNS/TCP
/// fails, or when `init.timeoutMs` elapses.
Promise<FetchResponse> fetch(const std::string& url, const FetchInit& init = FetchInit());

// =============================================================================
// HTTP server (`createServer`)
// =============================================================================
class Next;

/// The `req` handed to route handlers and middleware.
class HttpRequest {
public:
    /// `req.method` (upper case, as received) and `req.path` (query string
    /// stripped). `req.params` / `req.query` / `req.headers` need the Map type
    /// (MISSING); the runtime parses them internally already.
    std::string method;
    std::string path;

    /// `await req.text()`; the body is buffered before the handler chain runs.
    Promise<std::string> text() const { return Promise<std::string>::resolve(body_); }

    /// `await req.json<T>()` for T = number | string | boolean.
    template <class T>
    Promise<T> json() const {
        if constexpr (std::is_same_v<T, std::string>) {
            return Promise<std::string>::resolve(detail::jsonString(body_));
        } else if constexpr (std::is_same_v<T, double>) {
            return Promise<double>::resolve(detail::jsonNumber(body_));
        } else if constexpr (std::is_same_v<T, bool>) {
            return Promise<bool>::resolve(detail::jsonBool(body_));
        } else {
            static_assert(sizeof(T) == 0,
                          "std/network: json<T>() supports number, string and boolean");
        }
    }

    /// Internal: the server stores the buffered body before dispatch.
    void setBody(std::string body) { body_ = std::move(body); }

private:
    std::string body_;
};

/// The `res` handed to route handlers. `status`/`header` chain; `text`, `json`,
/// `send`, `write` and `end` are terminal (the first one wins).
class HttpResponse {
public:
    HttpResponse();

    /// `res.status(code)`; chainable.
    HttpResponse& status(double code);
    /// `res.header(name, value)`; chainable, applies to the terminal write.
    HttpResponse& header(const std::string& name, const std::string& value);

    /// `res.text(data, status?)`.
    void text(const std::string& data, double status = 0.0);
    /// `res.json(data, status?)`; the payload is a scalar here (an object
    /// literal is not representable yet).
    void json(double data, double status = 0.0);
    void json(const std::string& data, double status = 0.0);
    void json(bool data, double status = 0.0);
    /// `res.send(status?)`: whatever has been buffered, empty if nothing.
    void send(double status = 0.0);
    /// `res.write(chunk)`: chunked streaming.
    void write(const std::string& chunk);
    /// `res.end()`: terminates a chunked response.
    void end();

    /// Internal: the server binds the native response for this request.
    void bindNative(void* native);
    /// True once a terminal write happened (the chain stops on it).
    bool sent() const;

private:
    std::shared_ptr<detail::ResponseImpl> impl_;
};

/// The `next` handed to `app.use` middleware: calling it continues the chain.
class Next {
public:
    Next() = default;
    /// Internal: the server binds the continuation.
    explicit Next(std::function<void()> continuation) : cont_(std::move(continuation)) {}
    void operator()() {
        if (cont_) cont_();
    }

private:
    std::function<void()> cont_;
};

/// `createServer()`. Routes and middleware run in registration order (Express
/// semantics): a handler stops the chain by responding, `next()` continues it,
/// and a chain that ends without a response answers 404.
class HttpServer {
public:
    using RouteHandler = std::function<void(HttpRequest&, HttpResponse&)>;
    using Middleware = std::function<void(HttpRequest&, HttpResponse&, Next)>;

    HttpServer();

    void get(const std::string& path, RouteHandler handler) const;
    void post(const std::string& path, RouteHandler handler) const;
    void put(const std::string& path, RouteHandler handler) const;
    void del(const std::string& path, RouteHandler handler) const;
    void use(Middleware middleware) const;
    /// `app.listen(port, callback?)`; the callback runs once the port is bound.
    void listen(double port, std::function<void()> callback = {}) const;
    /// `app.close()`: closes listen sockets and connections.
    void close() const;

private:
    std::shared_ptr<detail::ServerImpl> impl_;
};

// =============================================================================
// WebSocket
// =============================================================================
/// One class for both directions: `new WebSocket(url)` connects, while the
/// WebSocketServer connection listener receives an already-open instance.
class WebSocket {
public:
    /// `new WebSocket(url)`; `ws://` only (TLS off, D70).
    explicit WebSocket(const std::string& url);

    /// Internal: adopts an accepted server-side connection (the WebSocketServer
    /// glue is the only caller).
    static WebSocket adopt(void* native);

    /// Internal: the WebSocketServer glue reaches the shared impl of an adopted
    /// handle through this; generated code never calls it.
    static std::shared_ptr<detail::WsImpl> sharedImpl(const WebSocket& socket) {
        return socket.impl_;
    }

    /// `ws.on(event, handler)`; shapes follow the documented events:
    ///   "open"    -> () => void
    ///   "message" -> (data: string) => void
    ///   "close"   -> (code: number, reason: string) => void
    ///   "error"   -> (err: unknown) => void
    /// (One handler per event; a later registration replaces the earlier one.)
    void on(const std::string& event, std::function<void()> handler) const;
    void on(const std::string& event, std::function<void(std::string)> handler) const;
    void on(const std::string& event, std::function<void(double, std::string)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;

    /// `ws.send(data)` (string data in beta one).
    void send(const std::string& data) const;
    /// `ws.close(code?, reason?)`.
    void close(double code = 1000.0, const std::string& reason = "") const;

private:
    WebSocket() = default;  // used by adopt() only
    std::shared_ptr<detail::WsImpl> impl_;
};

// =============================================================================
// WebSocketServer
// =============================================================================
/// `new WebSocketServer({ port, path? })`.
class WebSocketServer {
public:
    WebSocketServer(double port, std::string path = "*");

    /// `wss.on("connection", (ws, req?) => void)`.
    void on(const std::string& event, std::function<void(WebSocket&)> handler) const;
    void on(const std::string& event, std::function<void(WebSocket&, HttpRequest)> handler) const;
    /// `wss.close()`.
    void close() const;

private:
    std::shared_ptr<detail::WssImpl> impl_;
};

}  // namespace cp3::runtime::network
