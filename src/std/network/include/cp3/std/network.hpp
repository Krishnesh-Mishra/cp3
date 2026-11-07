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
// Backend: libuv v1.53.0 + uWebSockets v20.80.0 (vendor/third_party/**, D70). The
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

#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/std/buffer.hpp"
#include "cp3/std/collections.hpp"
#include "cp3/std/json.hpp"
#include "cp3/std/network/tls.hpp"

namespace cp3::runtime::network {

/// `Map<string, string>` (std/collections): the documented type of the
/// header/params/query fields (docs 04 §network).
using StringMap = collections::Map<std::string, std::string>;

namespace regex_routing {
struct PathSpec;
struct RoutingOptions;
}  // namespace regex_routing

/// Implementation state. Namespace scope (not nested in the public classes) so
/// the private TUs can name it without friendship; generated code never does.
namespace detail {
struct ResponseImpl;
struct ServerImpl;
struct WssImpl;
struct WsImpl;
}  // namespace detail

class Headers;
class FormData;
class AbortSignal;

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
    // Ledger L (appended): `signal: AbortSignal.timeout(ms)` (fetch uses its
    // timeoutMs when no explicit timeout is set, or the smaller of the two)
    // and `body: FormData` (encoded by `withFormData`, see below).
    std::shared_ptr<AbortSignal> signal;
    std::shared_ptr<FormData> form;
};

/// The `FetchResponse` a resolved `fetch` fulfils with. The body is fully
/// buffered before the promise settles, so the readers never reject.
class FetchResponse {
public:
    FetchResponse() = default;
    FetchResponse(std::uint16_t statusCode, std::string statusText_,
                  std::string body, StringMap headers_)
        : ok(statusCode >= 200 && statusCode < 300),
          status(statusCode),
          statusText(std::move(statusText_)),
          headers(std::move(headers_)),
          body_(std::move(body)) {}

    /// `res.ok` / `res.status` / `res.statusText`.
    bool ok = false;
    /// The HTTP status code. Held exactly (the semantic width is `u16`); a
    /// TypeScript-mode read widens it to `number` at codegen.
    std::uint16_t status = 0;
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

/// `dns.lookup(hostname)`: resolves with the address (text form) of the
/// first result; rejects when the hostname cannot be resolved. Uses the
/// same libuv resolution the fetch client drives, off the calling thread's
/// loop, never a blocking `getaddrinfo`.
Promise<std::string> dnsResolve(const std::string& host);

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
    /// `req.cookies` (parsed from the `Cookie` header; empty when none).
    StringMap cookies;
    /// `req.signedCookies` (cookie agent): verified signed cookies; the
    /// codegen lowers the read to webserver::signedCookiesFor(req), so this
    /// field stays empty in the core runtime (parity storage only).
    StringMap signedCookies;
    /// `req.baseUrl` / `req.originalUrl` (router agent): maintained by the
    /// header-only Router's mountMiddleware ("" / full path at the top level).
    std::string baseUrl;
    std::string originalUrl;
    /// `req.ip`: the remote address as text (IPv4/IPv6, as the OS reports).
    std::string ip;
    /// `req.protocol`: the scheme text ("http" or "https"). The server fills
    /// it with the scheme the connection uses; when the server trusts the
    /// proxy (`createServer({ trustProxy: true })`), a non-empty
    /// `x-forwarded-proto` header decides instead.
    std::string protocol = "http";

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
    /// Ledger L (appended): buffered body size in bytes (bodyLimit middleware).
    std::size_t bodySize() const { return body_.size(); }
    /// Internal: the server stores the query/header maps before dispatch.
    void setQuery(StringMap map) { query = std::move(map); }
    void setHeaders(StringMap map) { headers = std::move(map); }
    /// Internal: the parsed `Cookie` header and the remote address.
    void setCookies(StringMap map) { cookies = std::move(map); }
    void setRemoteAddress(std::string address) { ip = std::move(address); }
    /// Internal: the scheme text (`req.protocol`); the server resolves the
    /// trust-proxy header before storing it.
    void setProtocol(std::string scheme) { protocol = std::move(scheme); }
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
    /// `res.redirect(url, status?)`: terminal; answers with the `Location`
    /// header and that status (302 by default).
    void redirect(const std::string& url, double status = 0.0);
    /// `res.cookie(name, value, options?)`: appends a `Set-Cookie` header
    /// and returns the response (chainable). `options` is rendered by
    /// codegen into the header value; the runtime just records it.
    HttpResponse& cookie(const std::string& setCookieValue);
    // --- webserver response helpers (webserver/response, paste §1) -------------
    HttpResponse& set(const std::string& name, const std::string& value);
    HttpResponse& set(const std::vector<std::pair<std::string, std::string>>& headers);
    HttpResponse& set(const StringMap& headers);
    HttpResponse& append(const std::string& name, const std::string& value);
    std::string get(const std::string& name) const;
    HttpResponse& type(const std::string& type);
    void format(const std::vector<std::pair<std::string, std::function<void()>>>& formats);
    void render(const std::string& view);
    void render(const std::string& view, const ::cp3::runtime::json::Value& locals);
    void download(const std::string& path, const std::string& filename = "");
    HttpResponse& attach(const std::string& filename);
    HttpResponse& links(const std::vector<std::pair<std::string, std::string>>& links);
    HttpResponse& location(const std::string& url);
    HttpResponse& vary(const std::string& field);
    HttpResponse& vary(const std::vector<std::string>& fields);
    bool headersSent() const;
    void varyImpl(const std::string& field);  // internal helper
    /// `res.sendFile(path)`: terminal; streams the file's bytes with a
    /// content type inferred from the extension; a missing file answers
    /// 404 with a short text body.
    void sendFile(const std::string& path);
    /// `res.write(chunk)`: chunked streaming; `string | Buffer` (docs 04
    /// §network), the Buffer arm writes the raw bytes.
    void write(const std::string& chunk);
    void write(const buffer::Buffer& chunk);
    /// `res.end()`: terminates a chunked response.
    void end();

    /// Internal: the server binds the native response for this request.
    void bindNative(void* native, bool ssl = false);
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
    // webserver `next("route")` (middleware/errors agents): skip the current
    // route stack; the stock dispatcher treats it as a plain continuation.
    Promise<void> operator()(const std::string& /*token*/) {
        if (cont_) cont_();
        return Promise<void>::resolve();
    }
    void route() { if (cont_) cont_(); }
    // webserver `next(err)` (errors/middleware agents): thrown so the
    // dispatcher routes it to app.onError()/500 (see limitations).
    Promise<void> operator()(Thrown err) {
        throw err;
    }
    void error(Thrown err) { throw err; }

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

/// webserver routing agent: a handler list (array / variadic handlers).
using HandlerList = std::vector<RouteFn>;
/// Compose a HandlerList into one RouteFn: each handler's Next advances to
/// the following handler; the last handler's Next is the outer continuation.
inline RouteFn composeHandlerList(HandlerList handlers) {
    return [handlers = std::move(handlers)](HttpRequest& req, HttpResponse& res,
                                            Next& outer) mutable -> Promise<void> {
        auto state = std::make_shared<HandlerList>(std::move(handlers));
        auto runFrom = std::make_shared<std::function<Promise<void>(std::size_t)>>();
        *runFrom = [runFrom, state, &req, &res, &outer](std::size_t i) -> Promise<void> {
            if (res.sent()) return Promise<void>::resolve();
            if (i >= state->size()) { outer(); return Promise<void>::resolve(); }
            auto advanced = std::make_shared<bool>(false);
            auto nested = std::make_shared<Promise<void>>(Promise<void>::resolve());
            Next stepNext([runFrom, i, advanced, nested] {
                *advanced = true;
                *nested = (*runFrom)(i + 1);
            });
            Promise<void> promise = (*state)[i](req, res, stepNext);
            if (!promise.settled()) {
                return promise.then(
                    [advanced, nested]() -> Promise<void> {
                        if (*advanced) return *nested;
                        return Promise<void>::resolve();
                    },
                    [](Thrown err) -> Promise<void> { throw err; });
            }
            if (promise.rejected()) throw promise.reason();
            if (*advanced) return *nested;
            return Promise<void>::resolve();
        };
        return (*runFrom)(0);
    };
}

/// `app.param(name, fn)`: (req, res, next, value) callback.
using ParamFn = std::function<Promise<void>(HttpRequest&, HttpResponse&, Next&, std::string)>;

/// Ledger L: an error handler `(err, req, res)` as the dispatcher's uniform
/// callable, for `server.onError(...)`.
using ErrorFn = std::function<Promise<void>(Thrown, HttpRequest&, HttpResponse&)>;
template <class F>
ErrorFn makeErrorHandler(F fn) {
    return [fn = std::move(fn)](Thrown err, HttpRequest& req,
                                HttpResponse& res) mutable -> Promise<void> {
        // Errors agent: a 4-arg `(err, req, res, next)` handler is accepted;
        // its next is a no-op continuation (the error chain already ended).
        if constexpr (std::is_invocable_v<F&, Thrown, HttpRequest&, HttpResponse&, Next&>) {
            Next next;
            return callAsPromise(fn, err, req, res, next);
        } else {
            return callAsPromise(fn, err, req, res);
        }
    };
}

}  // namespace detail

/// `createServer()`. Routes and middleware run in registration order (webserver
/// semantics): a handler stops the chain by responding, `next()` continues it,
/// and a chain that ends without a response answers 404. A handler may be
/// `async`; the dispatcher waits for its promise before it continues the chain
/// or decides the 404.
class HttpServer;

/// webserver routing agent: `app.route(path).get(h).post(h)` chain. Defined
/// here (not via runtime_routing.hpp) so generated code needs only
/// network.hpp; verbs register directly on the owning HttpServer.
class HttpRouteChain {
public:
    HttpRouteChain(const HttpServer* server, std::string path)
        : server_(server), path_(std::move(path)) {}
    template <class F> HttpRouteChain& get(F h) { return add("GET", std::move(h)); }
    template <class F> HttpRouteChain& post(F h) { return add("POST", std::move(h)); }
    template <class F> HttpRouteChain& put(F h) { return add("PUT", std::move(h)); }
    template <class F> HttpRouteChain& del(F h) { return add("DELETE", std::move(h)); }
    template <class F> HttpRouteChain& patch(F h) { return add("PATCH", std::move(h)); }
    template <class F> HttpRouteChain& head(F h) { return add("HEAD", std::move(h)); }
    template <class F> HttpRouteChain& options(F h) { return add("OPTIONS", std::move(h)); }
    template <class F> HttpRouteChain& all(F h) { return add("*", std::move(h)); }
    HttpRouteChain& get(detail::HandlerList hs) { return addList("GET", std::move(hs)); }
    HttpRouteChain& post(detail::HandlerList hs) { return addList("POST", std::move(hs)); }
    HttpRouteChain& put(detail::HandlerList hs) { return addList("PUT", std::move(hs)); }
    HttpRouteChain& del(detail::HandlerList hs) { return addList("DELETE", std::move(hs)); }
    HttpRouteChain& patch(detail::HandlerList hs) { return addList("PATCH", std::move(hs)); }
    HttpRouteChain& head(detail::HandlerList hs) { return addList("HEAD", std::move(hs)); }
    HttpRouteChain& options(detail::HandlerList hs) { return addList("OPTIONS", std::move(hs)); }
    HttpRouteChain& all(detail::HandlerList hs) { return addList("*", std::move(hs)); }
private:
    template <class F>
    HttpRouteChain& add(const char* method, F handler);
    HttpRouteChain& addList(const char* method, detail::HandlerList hs);
    const HttpServer* server_;
    std::string path_;
    friend class HttpServer;
};

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
    /// webserver `app.all(path, handler)` (routing agent): alias of any().
    template <class F>
    void all(const std::string& path, F handler) const { any(path, std::move(handler)); }
    // HandlerList (array / variadic) verb overloads (routing agent).
    void get(const std::string& path, detail::HandlerList hs) const { addRouteImpl("GET", path, detail::composeHandlerList(std::move(hs))); }
    void post(const std::string& path, detail::HandlerList hs) const { addRouteImpl("POST", path, detail::composeHandlerList(std::move(hs))); }
    void put(const std::string& path, detail::HandlerList hs) const { addRouteImpl("PUT", path, detail::composeHandlerList(std::move(hs))); }
    void del(const std::string& path, detail::HandlerList hs) const { addRouteImpl("DELETE", path, detail::composeHandlerList(std::move(hs))); }
    void patch(const std::string& path, detail::HandlerList hs) const { addRouteImpl("PATCH", path, detail::composeHandlerList(std::move(hs))); }
    void head(const std::string& path, detail::HandlerList hs) const { addRouteImpl("HEAD", path, detail::composeHandlerList(std::move(hs))); }
    void options(const std::string& path, detail::HandlerList hs) const { addRouteImpl("OPTIONS", path, detail::composeHandlerList(std::move(hs))); }
    void all(const std::string& path, detail::HandlerList hs) const { addRouteImpl("*", path, detail::composeHandlerList(std::move(hs))); }
    /// webserver `app.route(path)` (routing agent).
    HttpRouteChain route(const std::string& path) const { return HttpRouteChain(this, path); }
    /// webserver `app.param(name, fn)` (routing/router agents): stored on
    /// ServerImpl, run by the dispatcher before matching route handlers.
    template <class F>
    void param(const std::string& name, F handler) const {
        addParamImpl(name, [fn = std::move(handler)](HttpRequest& req, HttpResponse& res, Next& next, const std::string& value) mutable -> Promise<void> {
            using Result = std::remove_cvref_t<std::invoke_result_t<F&, HttpRequest&, HttpResponse&, Next&, const std::string&>>;
            if constexpr (std::is_void_v<Result>) { fn(req, res, next, value); return Promise<void>::resolve(); }
            else if constexpr (::cp3::runtime::detail::is_promise_v<Result>) { return fn(req, res, next, value); }
            else { (void)fn(req, res, next, value); return Promise<void>::resolve(); }
        });
    }
    /// Regex-routing agent: app settings ("case sensitive routing" /
    /// "strict routing" and any other key, stored verbatim).
    void setRoutingSetting(const std::string& key, bool value) const;
    bool getRoutingSetting(const std::string& key) const;
    /// Middleware agent: `app.use("/api", mw)` mount form.
    template <class F>
    void use(const std::string& mount, F middleware) const {
        detail::RouteFn inner = detail::makeMiddleware(std::move(middleware));
        addMiddlewareImpl([mount, inner](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
            if (mount.empty() || mount == "*" || mount == "/") return inner(req, res, next);
            const std::string& p = req.path;
            if (p != mount && p.rfind(mount + "/", 0) != 0) { next(); return Promise<void>::resolve(); }
            std::string saved = p;
            std::string savedBase = req.baseUrl;
            if (req.originalUrl.empty()) req.originalUrl = saved;
            req.baseUrl = savedBase + mount;
            req.path = p.size() <= mount.size() ? std::string("/") : p.substr(mount.size());
            Next restoreNext([&saved, &savedBase, &req, &next] { req.path = saved; req.baseUrl = savedBase; next(); });
            return inner(req, res, restoreNext).then([&saved, &savedBase, &req]() { req.path = saved; req.baseUrl = savedBase; return Promise<void>::resolve(); }, [&saved, &savedBase, &req](Thrown e) -> Promise<void> { req.path = saved; req.baseUrl = savedBase; throw e; });
        });
    }
    template <class F>
    void use(F middleware) const {
        addMiddlewareImpl(detail::makeMiddleware(std::move(middleware)));
    }
    /// webserver error middleware `app.use((err, req, res, next) => ...)`
    /// (errors/middleware agents): codegen lowers a 4-arg `use` handler to
    /// `onError` (the stock dispatcher has a single error-handler slot).
    template <class F>
    void useError(F handler) const {
        setErrorHandlerImpl(detail::makeErrorHandler(std::move(handler)));
    }
    /// Ledger L: `app.onError((err, req, res) => ...)`; invoked by the
    /// dispatcher when a handler throws/rejects and no response was sent yet.
    /// Without it the dispatcher answers 500 as before.
    template <class F>
    void onError(F handler) const {
        setErrorHandlerImpl(detail::makeErrorHandler(std::move(handler)));
    }
    /// `app.listen(port, callback?)`; the callback runs once the port is bound.
    void listen(double port, std::function<void()> callback = {}) const;
    /// `app.close()`: closes listen sockets and connections.
    void close() const;
    /// `createServer({ trustProxy })`: when on, `req.protocol` reads the
    /// `x-forwarded-proto` header (the left-most hop) instead of always
    /// reporting the connection scheme.
    void setTrustProxy(bool on) const;
    /// INTERNAL (not generated): enable TLS for this server; `listen()` then
    /// creates a uWS SSLApp. `cert`/`key` are PEM file paths; `ca` is ignored
    /// in v0.2 (client-cert mTLS is not supported) and the passphrase only
    /// decrypts an encrypted `key`. `minProtocol` must be TLSv1.2 or TLSv1.3.
    void setTlsOptions(const tls::TlsOptions& options) const;

private:
    void addRouteImpl(const char* method, const std::string& path,
                      detail::RouteFn handler) const;
    void addMiddlewareImpl(detail::RouteFn handler) const;
    void addParamImpl(const std::string& name, detail::ParamFn handler) const;
    void setErrorHandlerImpl(detail::ErrorFn handler) const;  // Ledger L
    friend void addRegexRoute(const HttpServer&, const char*, const regex_routing::PathSpec&, const regex_routing::RoutingOptions&, detail::RouteFn);
    friend void addRegexRouteWithOptions(const HttpServer&, const char*, const regex_routing::PathSpec&, const regex_routing::RoutingOptions&, detail::RouteFn);
    friend class HttpRouteChain;
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
/// `new WebSocketServer({ port, path?, tls? })`. With `tls` (a `TlsOptions`
/// value from `tls({...})` or an inline literal) the server serves TLS
/// (PEM file `cert`/`key` required); without it, plain ws://.
class WebSocketServer {
public:
    WebSocketServer(double port, std::string path = "*", tls::TlsOptions tlsOptions = {});

    /// `wss.on("connection", (ws, req?) => void)`.
    void on(const std::string& event, std::function<void(WebSocket&)> handler) const;
    void on(const std::string& event, std::function<void(WebSocket&, HttpRequest)> handler) const;
    /// `wss.close()`.
    void close() const;

private:
    std::shared_ptr<detail::WssImpl> impl_;
};

// =============================================================================
// Ledger L: Headers / FormData / AbortSignal / Request / Response
// =============================================================================
/// Case-insensitive header collection (Fetch `Headers`). Names are matched
/// case-insensitively; `get` returns "" for a missing name.
class Headers {
public:
    Headers() = default;
    explicit Headers(const StringMap& map);
    explicit Headers(std::vector<std::pair<std::string, std::string>> pairs);
    std::string get(const std::string& name) const;
    bool has(const std::string& name) const;
    void set(const std::string& name, const std::string& value);
    void append(const std::string& name, const std::string& value);
    void remove(const std::string& name);  // JS `delete`
    StringMap toMap() const;
    const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }
private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

/// Multipart form fields (Fetch `FormData`). Values are text/bytes; files are
/// out of scope (no Blob/File surface).
class FormData {
public:
    FormData() = default;
    void append(const std::string& name, const std::string& value);
    void append(const std::string& name, const buffer::Buffer& value);
    std::string get(const std::string& name) const;
    bool has(const std::string& name) const;
    void set(const std::string& name, const std::string& value);
    void remove(const std::string& name);  // JS `delete`
    std::string encode(const std::string& boundary) const;
    const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }
private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

/// `AbortSignal.timeout(ms)`: carries the timeout a `fetch` should apply.
/// The fetch transport already has a libuv timeout, so the signal timeout is
/// honoured through it; `aborted` flips only when fetch rejects on timeout
/// (the shared signal object is not mutated after fetch returns).
class AbortSignal {
public:
    AbortSignal() = default;
    static AbortSignal timeout(double ms) {
        AbortSignal s; s.timeoutMs = ms; return s;
    }
    bool aborted = false;
    double timeoutMs = 0.0;
};

/// Encodes `form` as multipart/form-data into `init.body`, sets its
/// Content-Type header and returns the init (fetch `body: FormData`).
FetchInit withFormData(FetchInit init, const FormData& form);

struct CorsOptions {
    std::string origin = "*";
    std::string methods = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
    std::string headers;
    bool credentials = false;
    double maxAge = 0.0;
};

struct RequestInit {
    std::string method = "GET";
    std::string body;
    bool hasBody = false;
    StringMap headers;
    std::shared_ptr<Headers> headersObj;
    std::shared_ptr<FormData> form;
    std::shared_ptr<AbortSignal> signal;
};

/// Fetch `Request`: `new Request(url, init?)`, usable as `fetch(request)`.
class Request {
public:
    Request() = default;
    explicit Request(std::string url);
    Request(std::string url, RequestInit init);
    std::string url;
    std::string method = "GET";
    Headers headers;
    std::string body;
    bool hasBody = false;
    std::shared_ptr<FormData> form;
    std::shared_ptr<AbortSignal> signal;
    Promise<std::string> text() const { return Promise<std::string>::resolve(body); }
    Promise<buffer::Buffer> arrayBuffer() const { return Promise<buffer::Buffer>::resolve(buffer::from(body)); }
    template <class T>
    Promise<T> json() const {
        if constexpr (std::is_same_v<T, std::string>) {
            return Promise<std::string>::resolve(detail::jsonString(body));
        } else if constexpr (std::is_same_v<T, double>) {
            return Promise<double>::resolve(detail::jsonNumber(body));
        } else if constexpr (std::is_same_v<T, bool>) {
            return Promise<bool>::resolve(detail::jsonBool(body));
        } else {
            static_assert(sizeof(T) == 0, "std/network: json<T>() supports number, string and boolean");
        }
    }
    template <class T, class F>
    Promise<T> json(F build) const {
        return Promise<T>::resolve(::cp3::runtime::json::parseRecord(body, build));
    }
};

/// Fetch `Response`: `new Response(body?, init?)` for constructing a response
/// value (e.g. a fetch fallback); `fetch` itself still returns FetchResponse.
class Response {
public:
    Response() = default;
    explicit Response(std::string body);
    Response(std::string body, double status, Headers headers);
    bool ok = false;
    std::uint16_t status = 200;
    std::string statusText;
    Headers headers;
    std::string url;
    std::string body;
    Promise<std::string> text() const { return Promise<std::string>::resolve(body); }
    Promise<buffer::Buffer> arrayBuffer() const { return Promise<buffer::Buffer>::resolve(buffer::from(body)); }
    template <class T>
    Promise<T> json() const {
        if constexpr (std::is_same_v<T, std::string>) {
            return Promise<std::string>::resolve(detail::jsonString(body));
        } else if constexpr (std::is_same_v<T, double>) {
            return Promise<double>::resolve(detail::jsonNumber(body));
        } else if constexpr (std::is_same_v<T, bool>) {
            return Promise<bool>::resolve(detail::jsonBool(body));
        } else {
            static_assert(sizeof(T) == 0, "std/network: json<T>() supports number, string and boolean");
        }
    }
    template <class T, class F>
    Promise<T> json(F build) const {
        return Promise<T>::resolve(::cp3::runtime::json::parseRecord(body, build));
    }
};

/// `fetch(request)`: the Request's url/method/headers/body/form/signal drive
/// the same transport as `fetch(url, init)`.
Promise<FetchResponse> fetch(const Request& request);

// =============================================================================
// Ledger L: webserver-style middleware factories (for `app.use(...)`)
// =============================================================================
using MiddlewareFn = detail::RouteFn;
/// Serves regular files under `dir` for GET/HEAD requests whose path maps to
/// an existing file (traversal outside `dir` is refused); other requests
/// continue the chain with `next()`.
MiddlewareFn makeStaticMiddleware(std::string dir);
/// Sets CORS response headers; answers OPTIONS preflights without `next()`.
MiddlewareFn makeCorsMiddleware(CorsOptions options = CorsOptions());
/// Rejects with 413 when the buffered request body exceeds `limit` bytes.
MiddlewareFn makeBodyLimitMiddleware(double limit);

/// Regex-routing agent: PathSpec route registration (defined in
/// runtime_webserver_regex.cpp; PathSpec/RoutingOptions are defined by the
/// included regex_routing.hpp below).
void addRegexRoute(const HttpServer& server, const char* method,
                   const regex_routing::PathSpec& spec,
                   const regex_routing::RoutingOptions& options,
                   detail::RouteFn handler);
void addRegexRouteWithOptions(const HttpServer& server, const char* method,
                              const regex_routing::PathSpec& spec,
                              const regex_routing::RoutingOptions& options,
                              detail::RouteFn handler);

/// Merged `createServer({...})` configuration (the v0.2 config surface:
/// config/ + config-static/ + config-apps, one entry point). Every flag is
/// opt-in: an absent key leaves the plain `createServer()` behaviour.
struct CreateServerConfig {
    double threads = 0.0;            // 0 = single loop (D79 scaling)
    bool tls = false;                // enabled; cert/key REQUIRED (fails closed)
    tls::TlsOptions tlsOptions{};    // populated when `tls` carried an object
    bool tlsOptionsSet = false;      // true when the object form was passed
    bool hasCors = false;
    CorsOptions cors;
    bool hasStatic = false;
    std::string staticDir;
    bool hasBodyLimit = false;
    double bodyLimit = 0.0;          // bytes, 413 over the limit
    bool trustProxy = false;
    bool hasViews = false;
    std::string viewsDir;
    bool hasOnError = false;
    detail::ErrorFn onError;
    // Reserved schema keys (validated, not yet enforced): maxConnections,
    // idleTimeout, maxWriteBuffer, timeout, json, urlencoded, timeouts.
};
/// `tls({...})`: builds server TLS options for `createServer({ tls })`.
/// `cert`/`key` are PEM file paths; `ca` is ignored in v0.2; `passphrase`
/// decrypts an encrypted `key`; `minProtocol` is "TLSv1.2" (default) or
/// "TLSv1.3". Throws a TLSError for anything else or for an unknown key.
tls::TlsOptions makeTlsOptions(const std::string& cert, const std::string& key,
                               const std::string& ca = "",
                               const std::string& passphrase = "",
                               const std::string& minProtocol = "TLSv1.2");

/// `createServer(options)`: builds the server from the merged config. Middleware
/// is installed bodyLimit -> cors -> static (preflights and the 413 gate run
/// before static); `tls` enables an SSLApp server (PEM file cert/key required);
/// an empty or unloadable TLS material throws before any socket exists.
HttpServer createServerWithConfig(const CreateServerConfig& config);

/// View-layer config for res.render (webserver/response).
void setwebserverResponseViewDir(std::string dir);
void setwebserverResponseViewRenderer(
    std::function<std::string(const std::string& view, const std::string& localsJson)> renderer);

}  // namespace cp3::runtime::network

// HttpRouteChain verb definitions (need HttpServer's private addRouteImpl).
namespace cp3::runtime::network {
template <class F>
HttpRouteChain& HttpRouteChain::add(const char* method, F handler) {
    if (server_) server_->addRouteImpl(method, path_, detail::makeRoute(std::move(handler)));
    return *this;
}
inline HttpRouteChain& HttpRouteChain::addList(const char* method, detail::HandlerList hs) {
    if (server_) server_->addRouteImpl(method, path_, detail::composeHandlerList(std::move(hs)));
    return *this;
}
}  // namespace cp3::runtime::network

// =============================================================================
// webserver namespace (router / routing / errors / regex-routing agents)
// =============================================================================
namespace cp3::runtime::network::webserver {

struct RouterOptions {
    bool caseSensitive = false;
    bool strict = false;
    bool mergeParams = false;
};
class Router;
struct RouterLayer {
    std::string method;
    std::string pattern;
    detail::RouteFn handler;
    std::shared_ptr<std::vector<RouterLayer>> child;
};
using RouterStack = std::vector<RouterLayer>;
/// Chainable object returned by Router::route(path) (router agent).
class RouterRouteChain {
public:
    RouterRouteChain(std::shared_ptr<RouterStack> stack, std::string path)
        : stack_(std::move(stack)), path_(std::move(path)) {}
    template <class F> RouterRouteChain& get(F h) { return add("GET", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& post(F h) { return add("POST", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& put(F h) { return add("PUT", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& del(F h) { return add("DELETE", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& patch(F h) { return add("PATCH", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& head(F h) { return add("HEAD", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& options(F h) { return add("OPTIONS", detail::makeRoute(std::move(h))); }
    template <class F> RouterRouteChain& all(F h) { return add("*", detail::makeRoute(std::move(h))); }
    RouterRouteChain& get(detail::HandlerList hs) { return add("GET", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& post(detail::HandlerList hs) { return add("POST", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& put(detail::HandlerList hs) { return add("PUT", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& del(detail::HandlerList hs) { return add("DELETE", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& patch(detail::HandlerList hs) { return add("PATCH", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& head(detail::HandlerList hs) { return add("HEAD", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& options(detail::HandlerList hs) { return add("OPTIONS", detail::composeHandlerList(std::move(hs))); }
    RouterRouteChain& all(detail::HandlerList hs) { return add("*", detail::composeHandlerList(std::move(hs))); }
private:
    RouterRouteChain& add(const char* m, detail::RouteFn h) { stack_->push_back(RouterLayer{m, path_, std::move(h), nullptr}); return *this; }
    std::shared_ptr<RouterStack> stack_;
    std::string path_;
};

/// Minimal header-only Router (router/routing agents): own stack, mountable
/// via handler()/mountMiddleware(). Deliberately separate from
/// webserver/router/include/.../router.hpp (which generated code does not
/// include); see limitations in the wiring report.
class Router {
public:
    Router() = default;
    explicit Router(RouterOptions options) : options_(options) {}
    template <class F> void get(const std::string& p, F h) const { addRouteImpl("GET", p, detail::makeRoute(std::move(h))); }
    template <class F> void post(const std::string& p, F h) const { addRouteImpl("POST", p, detail::makeRoute(std::move(h))); }
    template <class F> void put(const std::string& p, F h) const { addRouteImpl("PUT", p, detail::makeRoute(std::move(h))); }
    template <class F> void del(const std::string& p, F h) const { addRouteImpl("DELETE", p, detail::makeRoute(std::move(h))); }
    template <class F> void patch(const std::string& p, F h) const { addRouteImpl("PATCH", p, detail::makeRoute(std::move(h))); }
    template <class F> void head(const std::string& p, F h) const { addRouteImpl("HEAD", p, detail::makeRoute(std::move(h))); }
    template <class F> void options(const std::string& p, F h) const { addRouteImpl("OPTIONS", p, detail::makeRoute(std::move(h))); }
    template <class F> void all(const std::string& p, F h) const { addRouteImpl("*", p, detail::makeRoute(std::move(h))); }
    void get(const std::string& p, detail::HandlerList hs) const { addRouteImpl("GET", p, detail::composeHandlerList(std::move(hs))); }
    void post(const std::string& p, detail::HandlerList hs) const { addRouteImpl("POST", p, detail::composeHandlerList(std::move(hs))); }
    void put(const std::string& p, detail::HandlerList hs) const { addRouteImpl("PUT", p, detail::composeHandlerList(std::move(hs))); }
    void del(const std::string& p, detail::HandlerList hs) const { addRouteImpl("DELETE", p, detail::composeHandlerList(std::move(hs))); }
    void patch(const std::string& p, detail::HandlerList hs) const { addRouteImpl("PATCH", p, detail::composeHandlerList(std::move(hs))); }
    void head(const std::string& p, detail::HandlerList hs) const { addRouteImpl("HEAD", p, detail::composeHandlerList(std::move(hs))); }
    void options(const std::string& p, detail::HandlerList hs) const { addRouteImpl("OPTIONS", p, detail::composeHandlerList(std::move(hs))); }
    void all(const std::string& p, detail::HandlerList hs) const { addRouteImpl("*", p, detail::composeHandlerList(std::move(hs))); }
    template <class F> void use(F mw) const { addMwImpl("*", detail::makeMiddleware(std::move(mw))); }
    template <class F> void use(const std::string& p, F mw) const { addMwImpl(p, detail::makeMiddleware(std::move(mw))); }
    void use(const std::string& p, const Router& sub) const { addMountImpl(p, sub.stack_); }
    void use(const Router& sub) const { addMountImpl("/", sub.stack_); }
    template <class F> void param(const std::string& name, F h) const {
        params_.emplace_back(name, [fn = std::move(h)](HttpRequest& req, HttpResponse& res, Next& next, const std::string& v) mutable -> Promise<void> {
            using Result = std::remove_cvref_t<std::invoke_result_t<F&, HttpRequest&, HttpResponse&, Next&, const std::string&>>;
            if constexpr (std::is_void_v<Result>) { fn(req, res, next, v); return Promise<void>::resolve(); }
            else if constexpr (::cp3::runtime::detail::is_promise_v<Result>) { return fn(req, res, next, v); }
            else { (void)fn(req, res, next, v); return Promise<void>::resolve(); }
        });
    }
    RouterRouteChain route(const std::string& p) const { return RouterRouteChain(stack_, p); }
    Promise<void> handle(HttpRequest& req, HttpResponse& res, Next& next) const { return dispatch(req, res, next); }
    detail::RouteFn handler() const {
        auto self = stack_; auto params = params_; auto opts = options_;
        return [self, params, opts](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
            return dispatchStack(*self, params, opts, req, res, next);
        };
    }
    detail::RouteFn mountMiddleware(const std::string& mount) const {
        auto self = stack_; auto params = params_; auto opts = options_;
        return [self, params, opts, mount](HttpRequest& req, HttpResponse& res, Next& next) mutable -> Promise<void> {
            if (!mount.empty() && mount != "/" && req.path != mount && req.path.rfind(mount + "/", 0) != 0) { next(); return Promise<void>::resolve(); }
            std::string saved = req.path;
            std::string savedBase = req.baseUrl;
            if (req.originalUrl.empty()) req.originalUrl = saved;
            if (!mount.empty() && mount != "/") req.baseUrl = savedBase + mount;
            req.path = req.path.size() <= mount.size() ? std::string("/") : req.path.substr(mount.size());
            Next restoreNext([&saved, &savedBase, &req, &next] { req.path = saved; req.baseUrl = savedBase; next(); });
            return dispatchStack(*self, params, opts, req, res, restoreNext).then([&saved, &savedBase, &req]() { req.path = saved; req.baseUrl = savedBase; return Promise<void>::resolve(); }, [&saved, &savedBase, &req](Thrown e) -> Promise<void> { req.path = saved; req.baseUrl = savedBase; throw e; });
        };
    }
    const RouterOptions& options() const { return options_; }
private:
    using Stack = RouterStack;
    using Layer = RouterLayer;
    void addRouteImpl(const char* m, const std::string& p, detail::RouteFn h) const { stack_->push_back(Layer{m, p, std::move(h), nullptr}); }
    void addMwImpl(const std::string& p, detail::RouteFn h) const { stack_->push_back(Layer{"*", p, std::move(h), nullptr}); }
    void addMountImpl(const std::string& p, std::shared_ptr<Stack> child) const { stack_->push_back(Layer{"*", p, nullptr, std::move(child)}); }
    static bool matchPattern(const std::string& pat, const std::string& path, bool strict, std::vector<std::pair<std::string,std::string>>* caps) {
        auto split = [](const std::string& s) { std::vector<std::string> parts; std::size_t st = 0; for (;;) { auto sl = s.find('/', st); if (sl == std::string::npos) { parts.push_back(s.substr(st)); return parts; } parts.push_back(s.substr(st, sl - st)); st = sl + 1; } };
        if (pat == "*" || pat == "/*") return true;
        auto pSegs = split(pat); auto sSegs = split(path);
        if (!strict && pSegs.size() > 2 && pSegs.back().empty()) pSegs.pop_back();
        if (!strict && sSegs.size() > 2 && sSegs.back().empty()) sSegs.pop_back();
        std::size_t pi = 0, si = 0;
        while (pi < pSegs.size()) {
            const std::string& seg = pSegs[pi]; bool last = pi + 1 == pSegs.size();
            if (seg == "*") {
                if (last) { if (si >= sSegs.size()) return false; if (caps) { std::string rest = sSegs[si]; for (std::size_t k = si + 1; k < sSegs.size(); ++k) { rest += "/"; rest += sSegs[k]; } caps->emplace_back("*", rest); } return true; }
                if (si >= sSegs.size()) return false; if (caps) caps->emplace_back("*", sSegs[si]); ++pi; ++si; continue;
            }
            if (si >= sSegs.size()) return false;
            if (!seg.empty() && seg.front() == ':') { std::string name = seg.substr(1); bool opt = !name.empty() && name.back() == '?'; if (opt && name.back() == '?') name.pop_back(); if (opt) { /* greedy only */ } if (caps) caps->emplace_back(name, sSegs[si]); ++pi; ++si; continue; }
            std::string a = seg, b = sSegs[si];
            if (!options_caseSensitive_) { for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (a != b) return false;
            ++pi; ++si;
        }
        return si == sSegs.size();
    }
    static bool options_caseSensitive_;
    Promise<void> dispatch(HttpRequest& req, HttpResponse& res, Next& next) const { return dispatchStack(*stack_, params_, options_, req, res, next); }
    static Promise<void> dispatchStack(const Stack& stack, const std::vector<std::pair<std::string, detail::ParamFn>>& params, const RouterOptions& opts, HttpRequest& req, HttpResponse& res, Next& next) {
        (void)opts;
        for (const Layer& layer : stack) {
            if (layer.method != "*" && layer.method != req.method) continue;
            if (layer.child) {
                // nested mount: match prefix, strip, dispatch child
                const std::string& pat = layer.pattern;
                if (pat.empty() || pat == "/" || pat == "*") { /* fallthrough to child */ }
                else if (req.path != pat && req.path.rfind(pat + "/", 0) != 0) continue;
                std::string saved = req.path;
                if (!pat.empty() && pat != "/" && pat != "*") req.path = req.path.size() <= pat.size() ? std::string("/") : req.path.substr(pat.size());
                Next restoreNext([&saved, &req, &next] { req.path = saved; next(); });
                return dispatchStack(*layer.child, {}, opts, req, res, restoreNext);
            }
            std::vector<std::pair<std::string,std::string>> caps;
            if (!matchPattern(layer.pattern == "*" ? "*" : layer.pattern, req.path, opts.strict, &caps)) continue;
            if (layer.method != "*" || layer.pattern != "*") {
                StringMap merged = collections::makeMap<std::string, std::string>();
                for (auto& [n, v] : caps) merged->set(n, v);
                req.setParams(merged);
                // param callbacks for captured names, in capture order
                std::vector<detail::ParamFn> steps; std::vector<std::string> vals;
                for (auto& [n, v] : caps) for (auto& [pn, pf] : params) if (pn == n) { steps.push_back(pf); vals.push_back(v); }
                if (!steps.empty()) {
                    detail::HandlerList chain;
                    for (std::size_t i = 0; i < steps.size(); ++i) {
                        auto pf = steps[i]; std::string v = vals[i];
                        chain.push_back([pf, v](HttpRequest& r, HttpResponse& s, Next& n) mutable -> Promise<void> { return pf(r, s, n, v); });
                    }
                    chain.push_back([layer](HttpRequest& r, HttpResponse& s, Next& n) mutable -> Promise<void> { return layer.handler(r, s, n); });
                    return detail::composeHandlerList(chain)(req, res, next);
                }
            }
            return layer.handler(req, res, next);
        }
        next();
        return Promise<void>::resolve();
    }
    mutable std::shared_ptr<Stack> stack_ = std::make_shared<Stack>();
    mutable std::vector<std::pair<std::string, detail::ParamFn>> params_;
    RouterOptions options_;
};
inline bool Router::options_caseSensitive_ = false;
inline Router makeRouter() { return Router(); }
inline Router makeRouter(RouterOptions o) { return Router(o); }
inline Router makeRouter(bool mergeParams, bool strict) { RouterOptions o; o.mergeParams = mergeParams; o.strict = strict; return Router(o); }

/// webserverApp (routing agent): HttpServer + Router, lazy install on listen.
class webserverApp {
public:
    webserverApp() = default;
    explicit webserverApp(RouterOptions o) : options_(o) {}
    template <class F> void get(const std::string& p, F h) { router_.get(p, std::move(h)); }
    template <class F> void post(const std::string& p, F h) { router_.post(p, std::move(h)); }
    template <class F> void put(const std::string& p, F h) { router_.put(p, std::move(h)); }
    template <class F> void del(const std::string& p, F h) { router_.del(p, std::move(h)); }
    template <class F> void patch(const std::string& p, F h) { router_.patch(p, std::move(h)); }
    template <class F> void head(const std::string& p, F h) { router_.head(p, std::move(h)); }
    template <class F> void options(const std::string& p, F h) { router_.options(p, std::move(h)); }
    template <class F> void all(const std::string& p, F h) { router_.all(p, std::move(h)); }
    void get(const std::string& p, detail::HandlerList hs) { router_.get(p, std::move(hs)); }
    void post(const std::string& p, detail::HandlerList hs) { router_.post(p, std::move(hs)); }
    void put(const std::string& p, detail::HandlerList hs) { router_.put(p, std::move(hs)); }
    void del(const std::string& p, detail::HandlerList hs) { router_.del(p, std::move(hs)); }
    void patch(const std::string& p, detail::HandlerList hs) { router_.patch(p, std::move(hs)); }
    void head(const std::string& p, detail::HandlerList hs) { router_.head(p, std::move(hs)); }
    void options(const std::string& p, detail::HandlerList hs) { router_.options(p, std::move(hs)); }
    void all(const std::string& p, detail::HandlerList hs) { router_.all(p, std::move(hs)); }
    template <class F> void use(F mw) { router_.use(std::move(mw)); }
    void use(Router& child) { router_.use(child); }
    void use(const std::string& p, Router& child) { router_.use(p, child); }
    template <class F> void param(const std::string& n, F h) { router_.param(n, std::move(h)); }
    RouterRouteChain route(const std::string& p) { return router_.route(p); }
    HttpServer& server() { return server_; }
    Router& router() { return router_; }
    void listen(double port, std::function<void()> cb = {}) { install(); server_.listen(port, std::move(cb)); }
    void close() const { server_.close(); }
    template <class F> void onError(F h) const { server_.onError(std::move(h)); }
private:
    void install() { if (installed_) return; installed_ = true; server_.use(router_.handler()); }
    HttpServer server_;
    Router router_;
    RouterOptions options_;
    bool installed_ = false;
};
inline webserverApp makewebserverApp() { return webserverApp(); }
inline webserverApp makewebserverApp(RouterOptions o) { return webserverApp(o); }

namespace errors {
struct HttpError {
    int status = 500;
    bool expose = false;
    std::string message;
    std::string code;
    std::string stack;
    HttpError() = default;
    HttpError(int s, std::string m, bool e = false, std::string c = {}) : status(s), expose(e), message(std::move(m)), code(std::move(c)) {}
    int statusCode() const { return status; }
};
inline HttpError httpError(int status, std::string message, std::string code = {}) {
    return HttpError(status, std::move(message), status < 500, std::move(code));
}
}  // namespace errors

}  // namespace cp3::runtime::network::webserver

// Agent headers included at the bottom: they need the full network surface
// above and are safe to include here (pragma once; their own include of
// network.hpp is a no-op while this header is being processed).
#include "cp3/std/network/webserver_cookie.hpp"
#include "cp3/std/network/webserver/body_parse.hpp"
#include "regex_routing.hpp"
