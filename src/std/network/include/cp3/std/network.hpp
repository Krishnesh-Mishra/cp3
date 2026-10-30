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
//   * Handlers run on the runtime thread. A handler may be `async`: codegen
//     lowers the arrow to a coroutine lambda (src/std/network/codegen_methods.cpp)
//     and the dispatcher waits for its promise before it continues the chain
//     or answers 404, so `await req.text()` / `await req.json<T>()` are safe.
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
#include "cp3/std/buffer.hpp"
#include "cp3/std/collections.hpp"
#include "cp3/std/json.hpp"

namespace cp3::runtime::network {

/// `Map<string, string>` (std/collections): the documented type of the
/// header/params/query fields (docs 04 §network).
using StringMap = collections::Map<std::string, std::string>;

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
/// The `fetch(url, init)` options an object literal can spell today. `method`,
/// `body` and `timeout` are carried. `timeoutMs` of 0 means "no timeout" (JS
/// omits it; a call that never answers keeps the loop alive, exactly like a
/// never-settled timer).
///
/// `headers` is the documented `Map<string, string>` (docs 04 §network); a null
/// handle (the option was omitted) writes no extra lines. buildRequest
/// (runtime_fetch.cpp) writes one `Name: value` line per entry, skipping the
/// framing-owned names (host/content-length/connection).
struct FetchInit {
    std::string method = "GET";
    std::string body;
    bool hasBody = false;
    double timeoutMs = 0.0;
    StringMap headers;
};

/// The `FetchResponse` a resolved `fetch` fulfils with. The body is fully
/// buffered before the promise settles, so the readers never reject.
class FetchResponse {
public:
    FetchResponse() = default;
    FetchResponse(double statusCode, std::string statusText_, std::string body,
                  StringMap headers_)
        : ok(statusCode >= 200.0 && statusCode < 300.0),
          status(statusCode),
          statusText(std::move(statusText_)),
          headers(std::move(headers_)),
          body_(std::move(body)) {}

    /// `res.ok` / `res.status` / `res.statusText`.
    bool ok = false;
    double status = 0.0;
    std::string statusText;
    /// `res.headers`: the response header block, names lower cased like the
    /// JS Headers object (parseHead in runtime_fetch.cpp fills it).
    StringMap headers;

    /// `await res.text()`.
    Promise<std::string> text() const { return Promise<std::string>::resolve(body_); }

    /// `await res.arrayBuffer()`: the buffered bytes as a std/buffer `Buffer`.
    Promise<buffer::Buffer> arrayBuffer() const {
        return Promise<buffer::Buffer>::resolve(buffer::from(body_));
    }

    /// `await res.json<T>()`.
    ///
    /// T = number | string | boolean parses through the scalar readers
    /// (runtime.cpp). A reified interface T (the `JSON.parse<T>` shapes) takes
    /// the builder overload below: the generated builder validates the body
    /// against T's shape and the promise fulfils with the struct.
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

    /// The reified form (`res.json<User>()`): the generated builder parses this
    /// response's buffered body (the same `parseRecord` `JSON.parse<T>` uses)
    /// and the explicit T names the struct.
    template <class T, class F>
    Promise<T> json(F build) const {
        return Promise<T>::resolve(::cp3::runtime::json::parseRecord(body_, build));
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
    /// stripped; the server splits `?` off before dispatch).
    ///
    /// The three Map-shaped request fields (docs 04 §network); the server fills
    /// them in onRequest (runtime_server.cpp) from `req->getQuery()`, the
    /// header block and the matched `:name` route params.
    ///   params: Map<string, string>   route params, e.g. :id
    ///   query:  Map<string, string>   query string, e.g. ?key=value
    ///   headers: Map<string, string>
    ///
    /// SEAM(readstream/fs): `body: ReadStream` is the raw request as a stream
    /// (the same interface as fs.readFileStream). The server still buffers the
    /// body before the chain runs and `text()`/`json<T>()` read that buffer;
    /// exposing `body` needs an fs-owned ReadStream over a buffer (the landed
    /// fs ReadStream is file-backed) plus a streaming request pipeline, so the
    /// read stays a codegen skip (src/std/network/codegen_methods.cpp).
    std::string method;
    std::string path;
    StringMap params;
    StringMap query;
    StringMap headers;

    /// `await req.text()`; the body is buffered before the handler chain runs.
    Promise<std::string> text() const { return Promise<std::string>::resolve(body_); }

    /// `await req.json<T>()` for T = number | string | boolean; a reified
    /// interface T takes the builder overload below.
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

    /// The reified form (`req.json<User>()`); see FetchResponse::json(F).
    template <class T, class F>
    Promise<T> json(F build) const {
        return Promise<T>::resolve(::cp3::runtime::json::parseRecord(body_, build));
    }

    /// Internal: the server stores the buffered body before dispatch.
    void setBody(std::string body) { body_ = std::move(body); }
    /// Internal: the server stores the query/header maps before dispatch.
    void setQuery(StringMap map) { query = std::move(map); }
    void setHeaders(StringMap map) { headers = std::move(map); }
    /// Internal: the matched route's `:name` parameters (one Map per match, so
    /// a chain that runs through several routes never sees stale params).
    void setParams(StringMap map) { params = std::move(map); }

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
    /// `res.json(data, status?)`: a scalar payload uses the dedicated
    /// overloads; an array/object literal lowers to the json dynamic value
    /// tree (`networkJsonValueText`, src/std/network/codegen_methods.cpp) and
    /// answers here.
    void json(double data, double status = 0.0);
    void json(const std::string& data, double status = 0.0);
    void json(bool data, double status = 0.0);
    void json(const ::cp3::runtime::json::Value& data, double status = 0.0);
    /// `res.send(status?)`: whatever has been buffered, empty if nothing.
    void send(double status = 0.0);
    /// `res.write(chunk)`: chunked streaming; `string | Buffer` (docs 04
    /// §network), the Buffer arm writes the raw bytes.
    void write(const std::string& chunk);
    void write(const buffer::Buffer& chunk);
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
    /// Starts the remaining middleware/route chain and returns a settled
    /// promise so an async middleware may write `await next()`.
    Promise<void> operator()() {
        if (cont_) cont_();
        return Promise<void>::resolve();
    }

private:
    std::function<void()> cont_;
};

namespace detail {

/// The uniform internal handler: a route or middleware call that may settle
/// asynchronously. A synchronous callable is wrapped in an already-fulfilled
/// `Promise<void>`, so the dispatcher waits on both forms the same way.
using RouteFn = std::function<Promise<void>(HttpRequest&, HttpResponse&, Next&)>;

/// Invoke one user handler and turn its result into `Promise<void>`.
template <class F, class... Args>
Promise<void> callAsPromise(F& fn, Args&... args) {
    using Result = std::remove_cvref_t<std::invoke_result_t<F&, Args&...>>;
    if constexpr (std::is_void_v<Result>) {
        fn(args...);
        return Promise<void>::resolve();
    } else if constexpr (::cp3::runtime::detail::is_promise_v<Result>) {
        if constexpr (std::is_void_v<typename Result::value_type>) {
            return fn(args...);
        } else {
            // A `Promise<T>` handler is outside the documented
            // `void | Promise<void>` shape; waiting for it is the JS reading.
            return fn(args...).then([](auto&&) {});
        }
    } else {
        (void)fn(args...);
        return Promise<void>::resolve();
    }
}

/// A route handler `(req, res)` as the dispatcher's uniform callable.
template <class F>
RouteFn makeRoute(F fn) {
    return [fn = std::move(fn)](HttpRequest& req, HttpResponse& res,
                                Next&) mutable -> Promise<void> {
        return callAsPromise(fn, req, res);
    };
}

/// A middleware `(req, res, next)` as the dispatcher's uniform callable.
template <class F>
RouteFn makeMiddleware(F fn) {
    return [fn = std::move(fn)](HttpRequest& req, HttpResponse& res,
                                Next& next) mutable -> Promise<void> {
        return callAsPromise(fn, req, res, next);
    };
}

}  // namespace detail

/// `createServer()`. Routes and middleware run in registration order (Express
/// semantics): a handler stops the chain by responding, `next()` continues it,
/// and a chain that ends without a response answers 404. A handler may be
/// `async`; the dispatcher waits for its promise before it continues the chain
/// or decides the 404.
class HttpServer {
public:
    using RouteHandler = std::function<void(HttpRequest&, HttpResponse&)>;
    using Middleware = std::function<Promise<void>(HttpRequest&, HttpResponse&, Next)>;

    HttpServer();
    /// D79: `createServer({ threads: n })`. An integer n >= 1 starts n event
    /// loops, one per OS thread, each with its own uWS app instance and its own
    /// module-state copies (thread_local). `n == 1` is exactly the no-arg
    /// single-loop server; an invalid runtime value (0, negative, fractional,
    /// NaN) throws a `Thrown`. The no-argument construction path is unchanged.
    explicit HttpServer(double threads);

    /// Route registration accepts a sync handler (`void`) or an async one
    /// (`Promise<void>`); `detail::makeRoute` makes the return uniform.
    template <class F>
    void get(const std::string& path, F handler) const {
        addRouteImpl("GET", path, detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void post(const std::string& path, F handler) const {
        addRouteImpl("POST", path, detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void put(const std::string& path, F handler) const {
        addRouteImpl("PUT", path, detail::makeRoute(std::move(handler)));
    }
    /// `app.delete(...)`; codegen maps the cp3 `delete` onto this `del` member
    /// (`delete` is a C++ keyword). The docs' beta-one list is the four verbs
    /// get/post/put/delete; patch/head/options/any exist because the sema table
    /// already names them and a route that lowers must have a real member.
    template <class F>
    void del(const std::string& path, F handler) const {
        addRouteImpl("DELETE", path, detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void patch(const std::string& path, F handler) const {
        addRouteImpl("PATCH", path, detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void head(const std::string& path, F handler) const {
        addRouteImpl("HEAD", path, detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void options(const std::string& path, F handler) const {
        addRouteImpl("OPTIONS", path, detail::makeRoute(std::move(handler)));
    }
    /// `app.any(path, handler)`: every method (the wildcard method row).
    template <class F>
    void any(const std::string& path, F handler) const {
        addRouteImpl("*", path, detail::makeRoute(std::move(handler)));
    }
    template <class F>
    void use(F middleware) const {
        addMiddlewareImpl(detail::makeMiddleware(std::move(middleware)));
    }
    /// `app.listen(port, callback?)`; the callback runs once the port is bound.
    void listen(double port, std::function<void()> callback = {}) const;
    /// `app.close()`: closes listen sockets and connections.
    void close() const;

private:
    void addRouteImpl(const char* method, const std::string& path,
                      detail::RouteFn handler) const;
    void addMiddlewareImpl(detail::RouteFn handler) const;
    std::shared_ptr<detail::ServerImpl> impl_;
};

// =============================================================================
// Per-loop module state (D79)
// =============================================================================
/// Generated `main` registers `cp3_init_module_state` here when the unit calls
/// `createServer({ threads })`. Each child loop invokes it after its event loop
/// exists and before its uWS App is constructed, replaying every module-scope
/// runtime initializer onto that thread's `thread_local` copy. Programs without
/// the option never call this, so their lowering is unchanged.
void setModuleInitHook(void (*hook)());

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
    ///   "message" -> (data: string | Buffer) => void
    ///   "close"   -> (code: number, reason: string) => void
    ///   "error"   -> (err: unknown) => void
    /// (One handler per event; a later registration replaces the earlier one.)
    /// A text frame reaches the string handler, a binary frame
    /// (uWS::OpCode::BINARY / client opcode 0x2) the Buffer handler; both share
    /// the "message" slot.
    void on(const std::string& event, std::function<void()> handler) const;
    void on(const std::string& event, std::function<void(std::string)> handler) const;
    void on(const std::string& event, std::function<void(const buffer::Buffer&)> handler) const;
    void on(const std::string& event, std::function<void(double, std::string)> handler) const;
    void on(const std::string& event, std::function<void(Thrown)> handler) const;

    /// `ws.send(data)` (docs 04 §network's `string | Buffer`): the string path
    /// sends a text frame, the Buffer path a binary frame.
    void send(const std::string& data) const;
    void send(const buffer::Buffer& data) const;
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
