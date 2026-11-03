// =============================================================================
// cp3/src/std/io/runtime.cpp - `std/io` reactor primitives.
//
// OWNERSHIP: the std/io family (src/std/io/**). Selected only when a program
// imports "std/io" (src/std/io/native.hpp).
//
// One fd state per descriptor, on the shared per-thread libuv loop
// (cp3/async/uv_loop.hpp). Several waiters may share one descriptor: a single
// uv_poll handle subscribes to the union of their directions and settles each
// waiter when its direction is ready. Every waiter retains the runtime loop,
// so the program does not exit while a wait is pending.
// =============================================================================
#include "cp3/std/io.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <uv.h>

#include "cp3/async/uv_loop.hpp"

namespace cp3::runtime::io {
namespace {

struct Waiter {
    bool read = false;
    Promise<void> promise;
};

struct FdState {
    uv_poll_t poll{};
    std::uintptr_t fd = 0;
    std::vector<Waiter> waiters;
    int events = 0;
    bool started = false;
    bool closing = false;
};

std::unordered_map<std::uintptr_t, FdState*>& states() {
    // Thread-local: each thread owns one EventLoop and one libuv loop. The map
    // is intentionally leaked for the same reason as `currentLoop()`: its
    // destructor must not run during thread teardown.
    static thread_local auto* table =
        new std::unordered_map<std::uintptr_t, FdState*>();
    return *table;
}

// A closed descriptor does not reliably wake every poller (notably IOCP and
// epoll), so one shared timer checks the registered descriptors while any wait
// is pending. This is one timer for all waiters, never one per descriptor.
thread_local EventLoop::TimerId watcherId = 0;
thread_local bool watcherActive = false;

void checkClosedFds();

bool fdIsOpen(std::uintptr_t fd) {
#if defined(_WIN32)
    int type = 0;
    int length = sizeof(type);
    if (getsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_TYPE,
                   reinterpret_cast<char*>(&type), &length) == 0) {
        return true;
    }
    return WSAGetLastError() != WSAENOTSOCK;
#else
    errno = 0;
    if (fcntl(static_cast<int>(fd), F_GETFD) != -1) return true;
    return errno != EBADF;
#endif
}

void ensureWatcher() {
    if (watcherActive) return;
    watcherActive = true;
    watcherId = currentLoop().addTimer(100, checkClosedFds);
}

void stopWatcher() {
    if (!watcherActive) return;
    currentLoop().cancelTimer(watcherId);
    watcherActive = false;
    watcherId = 0;
}

void rejectAll(FdState* state, std::string message);

void checkClosedFds() {
    watcherActive = false;
    watcherId = 0;
    if (states().empty()) return;

    std::vector<FdState*> closed;
    for (const auto& entry : states()) {
        if (!fdIsOpen(entry.first)) closed.push_back(entry.second);
    }
    for (FdState* state : closed) {
        rejectAll(state, "std/io: fd closed while waiting");
    }

    if (!states().empty()) {
        watcherActive = true;
        watcherId = currentLoop().addTimer(100, checkClosedFds);
    }
}

bool parseFd(double value, std::uintptr_t& out) {
    if (!(value >= 0.0) || value > 9007199254740991.0) return false;
    if (std::trunc(value) != value) return false;
    out = static_cast<std::uintptr_t>(value);
    return true;
}

void closeState(FdState* state) {
    if (state->closing) return;
    state->closing = true;
    const auto it = states().find(state->fd);
    if (it != states().end() && it->second == state) states().erase(it);
    uv_poll_stop(&state->poll);
    uv_close(reinterpret_cast<uv_handle_t*>(&state->poll), [](uv_handle_t* handle) {
        delete static_cast<FdState*>(handle->data);
    });
    if (states().empty()) stopWatcher();
}

void rejectAll(FdState* state, std::string message) {
    std::vector<Waiter> waiters = std::move(state->waiters);
    state->waiters.clear();
    for (Waiter& waiter : waiters) {
        currentLoop().releaseExternal();
        waiter.promise.rejectWith(Thrown::make(message));
    }
    closeState(state);
}

void onPoll(uv_poll_t* handle, int status, int events);

void startPoll(FdState* state) {
    if (state->closing) return;
    int events = 0;
    for (const Waiter& waiter : state->waiters) {
        events |= waiter.read ? UV_READABLE : UV_WRITABLE;
    }
    if (events == 0) {
        closeState(state);
        return;
    }
    if (state->started && events == state->events) return;
    const int rc = uv_poll_start(&state->poll, events, onPoll);
    if (rc != 0) {
        rejectAll(state, std::string("std/io: could not poll fd: ") + uv_strerror(rc));
        return;
    }
    state->started = true;
    state->events = events;
}

void onPoll(uv_poll_t* handle, int status, int events) {
    FdState* state = static_cast<FdState*>(handle->data);
    if (state == nullptr || state->closing) return;

    if (status < 0) {
        rejectAll(state, std::string("std/io: fd poll failed: ") + uv_strerror(status));
        return;
    }
    if ((events & UV_DISCONNECT) != 0) {
        rejectAll(state, "std/io: fd hung up while waiting");
        return;
    }

    std::vector<Waiter> ready;
    std::vector<Waiter> remaining;
    ready.reserve(state->waiters.size());
    for (Waiter& waiter : state->waiters) {
        const bool hit = waiter.read ? (events & UV_READABLE) != 0
                                     : (events & UV_WRITABLE) != 0;
        if (hit) {
            ready.push_back(std::move(waiter));
        } else {
            remaining.push_back(std::move(waiter));
        }
    }
    state->waiters = std::move(remaining);

    for (Waiter& waiter : ready) {
        currentLoop().releaseExternal();
        waiter.promise.fulfill();
    }

    if (state->waiters.empty()) {
        closeState(state);
    } else {
        startPoll(state);
    }
}

Promise<void> waitFd(double fdNumber, bool read) {
    std::uintptr_t fd = 0;
    if (!parseFd(fdNumber, fd)) {
        return Promise<void>::reject(Thrown::make(std::string("std/io: invalid fd")));
    }

    uv::ensureLoop();
    void* loop = uv::loopHandle();
    if (loop == nullptr) {
        return Promise<void>::reject(
            Thrown::make(std::string("std/io: libuv loop is unavailable")));
    }

    FdState* state = nullptr;
    const auto found = states().find(fd);
    if (found != states().end()) state = found->second;

    if (state == nullptr) {
        state = new FdState;
        state->fd = fd;
        int rc = 0;
#if defined(_WIN32)
        rc = uv_poll_init_socket(static_cast<uv_loop_t*>(loop), &state->poll,
                                 static_cast<uv_os_sock_t>(fd));
#else
        rc = uv_poll_init(static_cast<uv_loop_t*>(loop), &state->poll,
                          static_cast<int>(fd));
#endif
        if (rc != 0) {
            delete state;
            return Promise<void>::reject(
                Thrown::make(std::string("std/io: invalid fd: ") + uv_strerror(rc)));
        }
        state->poll.data = state;
        states()[fd] = state;
    }

    Promise<void> promise = Promise<void>::pending();
    state->waiters.push_back(Waiter{read, promise});
    currentLoop().retainExternal();
    ensureWatcher();
    startPoll(state);
    return promise;
}

}  // namespace

Promise<void> waitReadable(double fd) { return waitFd(fd, true); }
Promise<void> waitWritable(double fd) { return waitFd(fd, false); }

Promise<void> pollUntilDone(std::function<double()> getSocket,
                            std::function<double()> checkProgress) {
    while (true) {
        const double state = checkProgress();
        if (state == POLL_DONE) co_return;
        if (state == POLL_FAILED) {
            throw Thrown::make(std::string("std/io: pollUntilDone failed"));
        }
        if (state == POLL_READING) {
            co_await awaitValue(waitReadable(getSocket()));
            continue;
        }
        if (state == POLL_WRITING) {
            co_await awaitValue(waitWritable(getSocket()));
            continue;
        }
        throw Thrown::make(
            std::string("std/io: pollUntilDone returned an unknown state"));
    }
}

}  // namespace cp3::runtime::io
