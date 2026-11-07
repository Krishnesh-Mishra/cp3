// =============================================================================
// src/std/network/runtime_server_scaled.cpp - D79 multi-loop HTTP server.
//
// OWNERSHIP: the std/network family (src/std/network/**). Compiled only when a
// program imports "std/network" (src/std/network/native.hpp).
//
// `createServer({ threads: n })` starts n event loops, one per OS thread, each
// with its own uWS App instance. Loop 0 runs on the thread that called
// `listen`; loops 1..n-1 each get an OS thread with its own libuv loop and its
// own module-state copies (the generated `cp3_init_module_state` hook replays
// the module initializers after the thread's loop exists).
//
// Connection pinning: Windows has one listener (loop 0) and hands accepted
// sockets to the loop that will own them for their whole lifetime, using uWS's
// own `addChildApp` preOpen handoff (the same path uWS's local cluster uses).
// POSIX gives every loop its own SO_REUSEPORT listener and the kernel pins each
// accepted connection to the loop that accepted it. HTTP parsing, handler
// dispatch and WebSocket upgrades all happen after that handoff/bind, so a
// connection never changes loops.
//
// The App objects are deliberately leaked (process lifetime), matching the
// single-loop server; `close()` closes every listener/connection and joins
// every worker thread, so no detached thread survives.
// =============================================================================
#include "cp3/std/network.hpp"

#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "App.h"
#include "HttpResponse.h"
#include "libusockets.h"

#include "../runtime/runtime_loop.hpp"
#include "runtime_server_internal.hpp"

namespace cp3::runtime::network {

namespace {

using detail::ServerImpl;

void (*g_moduleInitHook)() = nullptr;

/// Runtime validation for a computed `threads` value (literal values are
/// already checked by sema).
int resolveThreadCount(double threads) {
    if (!std::isfinite(threads) || threads < 1.0 ||
        threads != std::floor(threads)) {
        throw Thrown::make(
            "std/network: createServer threads must be an integer >= 1");
    }
    if (threads > 4096.0) {
        throw Thrown::make("std/network: createServer threads is too large");
    }
    return static_cast<int>(threads);
}

void recordStartupFailure(const std::shared_ptr<ServerImpl>& impl,
                          std::string message) {
    {
        std::lock_guard<std::mutex> lock(impl->startupMutex);
        if (!impl->startupFailed) {
            impl->startupFailed = true;
            impl->startupError = std::move(message);
        }
    }
    impl->startupCv.notify_all();
}

void signalReady(const std::shared_ptr<ServerImpl>& impl) {
    {
        std::lock_guard<std::mutex> lock(impl->startupMutex);
        ++impl->readyCount;
    }
    impl->startupCv.notify_all();
}

bool isStopping(const std::shared_ptr<ServerImpl>& impl) {
    std::lock_guard<std::mutex> lock(impl->startupMutex);
    return impl->stopping;
}

bool waitForChildren(const std::shared_ptr<ServerImpl>& impl) {
    std::unique_lock<std::mutex> lock(impl->startupMutex);
    impl->startupCv.wait(lock, [&] {
        return impl->startupFailed || impl->readyCount == impl->loopCount - 1;
    });
    return !impl->startupFailed;
}

/// One child loop: create its loop, replay module state, build its App, bind
/// (POSIX), retain the loop for the server, then run until close stops it.
template <bool Ssl>
void childLoop(const std::shared_ptr<ServerImpl>& impl, int index) {
    uWS::TemplatedApp<Ssl>* app = nullptr;
    bool retained = false;
    try {
        ensureLoop();
        EventLoop* loop = &currentLoop();
        {
            std::lock_guard<std::mutex> lock(impl->startupMutex);
            impl->loops[index] = loop;
        }
        if (isStopping(impl)) return;

        // D79 module state is per-loop: replay the unit's module initializers
        // on this thread before its uWS App exists.
        if (g_moduleInitHook != nullptr) g_moduleInitHook();

        const uWS::SocketContextOptions opts = Ssl ? detail::sslAppOptions(*impl)
                                                  : uWS::SocketContextOptions{};
        app = new uWS::TemplatedApp<Ssl>(opts);
        if constexpr (Ssl) {
            if (app->constructorFailed()) {
                recordStartupFailure(
                    impl,
                    "std/network: could not create the TLS context (check tls.cert / tls.key)");
                return;
            }
        }
        {
            std::lock_guard<std::mutex> lock(impl->startupMutex);
            if constexpr (Ssl) impl->sslApps[index] = app;
            else impl->apps[index] = app;
        }
        detail::installWildcardRoute<Ssl>(impl, app);
        if (isStopping(impl)) {
            app->close();
            return;
        }

#if !defined(_WIN32)
        const int port = impl->requestedPort;
        us_listen_socket_t* token = nullptr;
        app->listen(port, [&token](us_listen_socket_t* socket) { token = socket; });
        if (token == nullptr) {
            recordStartupFailure(
                impl,
                "std/network: could not listen on a worker loop (port already in use?)");
            return;
        }
#endif

        loop->retainExternal();
        retained = true;
        signalReady(impl);
        loop->run();
        loop->releaseExternal();
        retained = false;
    } catch (const std::bad_alloc&) {
        recordStartupFailure(
            impl, "std/network: out of memory while starting a worker loop");
    } catch (const std::exception& error) {
        recordStartupFailure(
            impl, std::string("std/network: worker loop failed: ") + error.what());
    } catch (...) {
        recordStartupFailure(impl, "std/network: worker loop failed");
    }
    if (retained) {
        currentLoop().releaseExternal();
    }
}

void postStopToChild(const std::shared_ptr<ServerImpl>& impl, int index) {
    EventLoop* loop = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl->startupMutex);
        if (static_cast<std::size_t>(index) >= impl->loops.size()) return;
        loop = impl->loops[index];
    }
    if (loop == nullptr) return;
    loop->postFromThread([impl, index] {
        if (impl->ssl) {
            if (impl->sslApps[index] != nullptr) impl->sslApps[index]->close();
        } else if (impl->apps[index] != nullptr) {
            impl->apps[index]->close();
        }
        currentLoop().stop();
    });
}

/// Stops every child loop and joins its thread. Idempotent; used by both the
/// startup-failure path and `close()`.
void stopChildren(const std::shared_ptr<ServerImpl>& impl) {
    {
        std::lock_guard<std::mutex> lock(impl->startupMutex);
        impl->stopping = true;
    }
    for (int i = 1; i < impl->loopCount; ++i) postStopToChild(impl, i);
    for (std::thread& worker : impl->workers) {
        if (worker.joinable()) worker.join();
    }
}

/// Runs on loop 0 (the main thread): close the listener, stop the children and
/// release loop 0's pending-work retain.
void shutdownOnMain(const std::shared_ptr<ServerImpl>& impl) {
    if (impl->closed) return;
    impl->closed = true;
    const bool wasListening = impl->listening;
    impl->listening = false;
    if (impl->app != nullptr) impl->app->close();
    if (impl->sslApp != nullptr) impl->sslApp->close();
    stopChildren(impl);
    if (wasListening) releaseNetwork();
}

}  // namespace

void setModuleInitHook(void (*hook)()) { g_moduleInitHook = hook; }

// =============================================================================
// HttpServer - scaled construction / listen / close
// =============================================================================
HttpServer::HttpServer(double threads) : impl_(std::make_shared<detail::ServerImpl>()) {
    const int count = resolveThreadCount(threads);
    if (count == 1) {
        // `threads: 1` is exactly the single-loop server. The uWS App flavor
        // is chosen at listen() time, once TLS material is known.
        ensureLoop();
        return;
    }
    impl_->scaled = true;
    impl_->loopCount = count;
    impl_->mainThread = std::this_thread::get_id();
    impl_->apps.assign(static_cast<std::size_t>(count), nullptr);
    impl_->sslApps.assign(static_cast<std::size_t>(count), nullptr);
    impl_->loops.assign(static_cast<std::size_t>(count), nullptr);
}

namespace detail {

namespace {

int validatedPort(double portValue) {
    if (!std::isfinite(portValue) || portValue < 0.0 || portValue > 65535.0 ||
        portValue != std::floor(portValue)) {
        throw Thrown::make(
            "std/network: listen port must be an integer in 0..65535");
    }
    return static_cast<int>(portValue);
}

#if !defined(_WIN32)
void resolveEphemeralPort(const std::shared_ptr<ServerImpl>& impl) {
    // Bind a throwaway app at port 0, read the kernel-assigned port, then
    // close that listener. The real apps bind the resolved port with
    // SO_REUSEPORT. The probe app is leaked like every uWS app here.
    auto* probeApp = new uWS::App();
    us_listen_socket_t* probe = nullptr;
    probeApp->listen(0, [&probe](us_listen_socket_t* socket) {
        probe = socket;
    });
    if (probe == nullptr) {
        throw Thrown::make(
            "std/network: could not listen (port already in use?)");
    }
    const int resolved =
        us_socket_local_port(0, reinterpret_cast<us_socket_t*>(probe));
    us_listen_socket_close(0, probe);
    if (resolved <= 0) {
        throw Thrown::make(
            "std/network: could not resolve the ephemeral listen port");
    }
    impl->requestedPort = resolved;
}
#endif

void startScaledLoops(const std::shared_ptr<ServerImpl>& impl) {
    ensureLoop();
    impl->mainLoop = &currentLoop();
    impl->mainThread = std::this_thread::get_id();
    retainNetwork();
    if (impl->ssl) {
        impl->sslApp = new uWS::SSLApp(detail::sslAppOptions(*impl));
        if (impl->sslApp->constructorFailed()) {
            releaseNetwork();
            throw Thrown::make(
                "std/network: could not create the TLS context (check tls.cert / tls.key)");
        }
        detail::installWildcardRoute<true>(impl, impl->sslApp);
    } else {
        impl->app = new uWS::App();
        detail::installWildcardRoute<false>(impl, impl->app);
    }

    impl->apps.assign(static_cast<std::size_t>(impl->loopCount), nullptr);
    impl->sslApps.assign(static_cast<std::size_t>(impl->loopCount), nullptr);
    impl->loops.assign(static_cast<std::size_t>(impl->loopCount), nullptr);
    if (impl->ssl) impl->sslApps[0] = impl->sslApp;
    else impl->apps[0] = impl->app;
    impl->loops[0] = impl->mainLoop;

#if !defined(_WIN32)
    // Resolve before starting workers: children bind immediately and signal
    // ready, so waiting for them here cannot deadlock on a port-0 gate.
    if (impl->requestedPort == 0) resolveEphemeralPort(impl);
#endif

    try {
        for (int i = 1; i < impl->loopCount; ++i) {
            if (impl->ssl) impl->workers.emplace_back(childLoop<true>, impl, i);
            else impl->workers.emplace_back(childLoop<false>, impl, i);
        }
    } catch (const std::exception& error) {
        recordStartupFailure(
            impl, std::string("std/network: could not start a worker thread: ") +
                      error.what());
    }

    if (!waitForChildren(impl)) {
        throw Thrown::make("std/network: " + impl->startupError);
    }
}

#if defined(_WIN32)
void listenScaledWindows(const std::shared_ptr<ServerImpl>& impl, int port) {
    // One shared listener. uWS's addChildApp preOpen hook round-robins each
    // accepted socket into the owning app's loop, where adoptSocket builds
    // the connection. Loop 0 is in the rotation too, so all n loops serve.
    us_listen_socket_t* token = nullptr;
    if (impl->ssl) {
        impl->sslApp->addChildApp(impl->sslApp);
        for (int i = 1; i < impl->loopCount; ++i) {
            if (impl->sslApps[i] != nullptr) impl->sslApp->addChildApp(impl->sslApps[i]);
        }
        impl->sslApp->listen(port, [&token](us_listen_socket_t* socket) {
            token = socket;
        });
    } else {
        impl->app->addChildApp(impl->app);
        for (int i = 1; i < impl->loopCount; ++i) {
            if (impl->apps[i] != nullptr) impl->app->addChildApp(impl->apps[i]);
        }
        impl->app->listen(port, [&token](us_listen_socket_t* socket) {
            token = socket;
        });
    }
    if (token == nullptr) {
        throw Thrown::make(
            "std/network: could not listen (port already in use?)");
    }
}
#else
void listenScaledPosix(const std::shared_ptr<ServerImpl>& impl) {
    us_listen_socket_t* token = nullptr;
    if (impl->ssl) {
        impl->sslApp->listen(impl->requestedPort,
                             [&token](us_listen_socket_t* socket) {
                                 token = socket;
                             });
    } else {
        impl->app->listen(impl->requestedPort,
                          [&token](us_listen_socket_t* socket) {
                              token = socket;
                          });
    }
    if (token == nullptr) {
        throw Thrown::make(
            "std/network: could not listen (port already in use?)");
    }
}
#endif

}  // namespace

void scaledListen(const std::shared_ptr<ServerImpl>& impl, double portValue,
                  const std::function<void()>& callback) {
    if (!impl || !impl->scaled || impl->listening || impl->closed) return;
    const int port = validatedPort(portValue);
    impl->listening = true;
    impl->requestedPort = port;

    try {
        startScaledLoops(impl);
#if defined(_WIN32)
        listenScaledWindows(impl, port);
#else
        listenScaledPosix(impl);
#endif
        if (callback) callback();
    } catch (...) {
        if (impl->app != nullptr) impl->app->close();
        if (impl->sslApp != nullptr) impl->sslApp->close();
        stopChildren(impl);
        if (impl->listening) {
            impl->listening = false;
            releaseNetwork();
        }
        impl->closed = true;
        throw;
    }
}

void scaledClose(const std::shared_ptr<ServerImpl>& impl) {
    if (!impl) return;
    // A handler runs on a child loop: never join from a child thread. Defer
    // the whole shutdown (including the joins) to loop 0.
    if (impl->mainThread != std::this_thread::get_id()) {
        EventLoop* mainLoop = impl->mainLoop;
        if (mainLoop != nullptr) {
            mainLoop->postFromThread([impl] { shutdownOnMain(impl); });
        }
        return;
    }
    shutdownOnMain(impl);
}

}  // namespace detail
}  // namespace cp3::runtime::network
