// =============================================================================
// src/std/network/runtime_server_internal.hpp - PRIVATE HTTP server state.
//
// OWNERSHIP: the std/network family (src/std/network/**). Shared by
// runtime_server.cpp (the single-loop server + dispatch) and
// runtime_server_scaled.cpp (D79 per-loop apps); not part of the generated-code
// contract.
//
// The uWS App is deliberately leaked (its lifetime is the process, like every
// uWS context in this runtime); the routes are kept alive by the wildcard
// handler capturing the shared ServerImpl.
// =============================================================================
#pragma once

#include <condition_variable>
#include <cstddef>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "App.h"
#include "HttpResponse.h"

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"

namespace cp3::runtime::network::detail {

using NativeRequest = uWS::HttpRequest;
using NativeResponse = uWS::HttpResponse<false>;   // plain App (the default)
// SSL response type for uWS::SSLApp (TLS): a DIFFERENT C++ type with the same
// member layout - code is templated on it, never virtual.
template <bool Ssl>
using NativeRes = uWS::HttpResponse<Ssl>;

/// Runs `f(res)` against the concrete uWS::HttpResponse stored behind `native`.
/// Both branches instantiate the same lambda body with the two sibling types
/// (HttpResponse<false> / <true>); there is no virtual dispatch, no extra call
/// on the hot path beyond one predictable branch per native call.
template <typename F>
decltype(auto) withNativeResponse(void* native, bool ssl, F&& f) {
    if (ssl) {
        return std::forward<F>(f)(static_cast<uWS::HttpResponse<true>*>(native));
    }
    return std::forward<F>(f)(static_cast<uWS::HttpResponse<false>*>(native));
}

struct ServerImpl {
    struct Route {
        std::string method;   // "*" = app.use (any method)
        std::string pattern;  // "*" = any path; else literal, `:name` segments
        std::function<Promise<void>(HttpRequest&, HttpResponse&, Next&)> handler;
    };

    uWS::App* app = nullptr;        // loop 0, plain HTTP (the listener on Windows)
    uWS::SSLApp* sslApp = nullptr;  // loop 0, TLS (mutually exclusive with `app`)
    bool ssl = false;               // createServer({ tls }) -> SSLApp path
    bool listening = false;
    bool closed = false;
    std::vector<Route> routes;

    // TLS material for the per-loop SSLApp instances (strings owned here; the
    // uWS contexts consume them synchronously at creation time).
    std::string sslKeyFile;
    std::string sslCertFile;
    std::string sslCaFile;
    std::string sslPassphrase;

    // --- D79 scale mode ------------------------------------------------------
    bool scaled = false;
    int loopCount = 1;  // including loop 0
    std::vector<uWS::App*> apps;
    std::vector<uWS::SSLApp*> sslApps;
    std::vector<std::thread> workers;
    std::vector<EventLoop*> loops;  // one per slot, filled by the slot thread
    std::thread::id mainThread;
    EventLoop* mainLoop = nullptr;

    // Startup barrier: children record failure, then readiness.
    std::mutex startupMutex;
    std::condition_variable startupCv;
    int readyCount = 0;
    bool startupFailed = false;
    std::string startupError;
    bool stopping = false;

    // POSIX port 0: loop 0 resolves the ephemeral port before any child
    // starts, then every loop binds the resolved port with SO_REUSEPORT.
    int requestedPort = 0;

    // Ledger L (appended): `app.onError(...)` handler, or null for the
    // default 500 response.
    ErrorFn errorHandler;
    // `createServer({ trustProxy })`: `req.protocol` then reads the
    // `x-forwarded-proto` header instead of the connection scheme.
    bool trustProxy = false;
    // webserver routing agent: `app.param(name, fn)` callbacks.
    std::vector<std::pair<std::string, ParamFn>> paramHandlers;
    // Regex-routing agent: app settings ("case sensitive routing", etc).
    std::map<std::string, bool> routingSettings;
};

/// The buffered response bound to one request.
struct ResponseImpl {
    void* native = nullptr;  // uWS::HttpResponse<false|true>* (see ServerImpl::ssl)
    bool ssl = false;        // response native handle is an SSL response
    double status = 200.0;
    bool headStarted = false;
    bool finished = false;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

/// SocketContextOptions for one SSLApp from the server's TLS material. The
/// char* members are consumed SYNCHRONOUSLY at uWS/uSockets context creation
/// (see libuSockets openssl.c), so building them from the stored strings is
/// safe. TLS floor is TLS1.2 inside uSockets; cert/key are PEM file paths.
inline uWS::SocketContextOptions sslAppOptions(const ServerImpl& impl) {
    uWS::SocketContextOptions options{};
    options.key_file_name = impl.sslKeyFile.c_str();
    options.cert_file_name = impl.sslCertFile.c_str();
    if (!impl.sslPassphrase.empty()) options.passphrase = impl.sslPassphrase.c_str();
    if (!impl.sslCaFile.empty()) options.ca_file_name = impl.sslCaFile.c_str();
    return options;
}

/// One request's route-chain cursor and async-handler bookkeeping.
struct RequestContext {
    ServerImpl* server = nullptr;
    HttpRequest request;
    HttpResponse response;
    std::size_t index = 0;
    std::size_t pending = 0;  // async handlers whose promise has not settled
    bool done = false;        // answered, 404, 500 or aborted
};

/// One request in flight. The uWS callbacks own it until the body is complete;
/// pending handler continuations keep it alive after that, so an async handler
/// can outlive the native callback that started it.
struct PendingRequest {
    std::shared_ptr<ServerImpl> server;
    RequestContext context;
    std::string body;
    bool aborted = false;
};

/// Runs the route chain once the request body is complete
/// (runtime_server_dispatch.cpp).
void dispatch(const std::shared_ptr<PendingRequest>& pending);

/// Registers the single wildcard route on one loop's App. `serveRequest` is
/// loop-local: the connection and everything it dispatches stay on the thread
/// that owns `app`. Templated on the app flavor (plain uWS::App vs SSLApp);
/// the two instantiations are emitted by runtime_server.cpp (a distinct
/// C++ type per SSL-ness - no virtual dispatch on the request path).
template <bool Ssl>
void installWildcardRoute(const std::shared_ptr<ServerImpl>& impl,
                          uWS::TemplatedApp<Ssl>* app);

/// D79: starts the child loops and the loop 0 listener. Throws `Thrown` on a
/// startup/bind failure after stopping every loop that already started.
void scaledListen(const std::shared_ptr<ServerImpl>& impl, double port,
                  const std::function<void()>& callback);

/// D79: stops every loop, joins the children and releases the per-loop
/// pending-work retain. Safe to call from a child handler (the work is then
/// deferred to loop 0, which owns the joins).
void scaledClose(const std::shared_ptr<ServerImpl>& impl);

}  // namespace cp3::runtime::network::detail
