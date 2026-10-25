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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "App.h"
#include "HttpResponse.h"

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"

namespace cp3::runtime::network::detail {

using NativeRequest = uWS::HttpRequest;
using NativeResponse = uWS::HttpResponse<false>;

struct ServerImpl {
    struct Route {
        std::string method;   // "*" = app.use (any method)
        std::string pattern;  // "*" = any path; else literal, `:name` segments
        std::function<Promise<void>(HttpRequest&, HttpResponse&, Next&)> handler;
    };

    uWS::App* app = nullptr;  // loop 0 (the listener on Windows)
    bool listening = false;
    bool closed = false;
    std::vector<Route> routes;

    // --- D79 scale mode ------------------------------------------------------
    bool scaled = false;
    int loopCount = 1;  // including loop 0
    std::vector<uWS::App*> apps;
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
/// that owns `app`.
void installWildcardRoute(const std::shared_ptr<ServerImpl>& impl, uWS::App* app);

/// D79: starts the child loops and the loop 0 listener. Throws `Thrown` on a
/// startup/bind failure after stopping every loop that already started.
void scaledListen(const std::shared_ptr<ServerImpl>& impl, double port,
                  const std::function<void()>& callback);

/// D79: stops every loop, joins the children and releases the per-loop
/// pending-work retain. Safe to call from a child handler (the work is then
/// deferred to loop 0, which owns the joins).
void scaledClose(const std::shared_ptr<ServerImpl>& impl);

}  // namespace cp3::runtime::network::detail
