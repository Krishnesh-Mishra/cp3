// =============================================================================
// src/std/fs/runtime_watch.cpp - the `fs.watch` implementation (cp3/std/fs.hpp).
//
// OWNERSHIP: the std/fs family (src/std/fs/**). Append-only addition for ledger
// H; the pre-existing runtime.cpp (sync/async/streams) is untouched. Selected
// with the fs native row (src/std/fs/native.hpp, D68) and linked only when a
// program imports "std/fs".
//
// Backend: libuv `uv_fs_event` on the one shared per-thread loop
// (cp3/async/uv_loop.hpp, the same loop std/network and std/io drive through
// the EventLoop external source). The watcher retains the runtime loop
// (pending work) until `close()` (or destruction), so the program cannot exit
// while a watch is live - the §14 exit rule, unchanged. Every uv event is
// posted as a microtask onto the runtime loop, so the user callback runs in
// event-loop order, never inside the uv callback.
//
// Lifecycle (mirrors the streams): the FsWatcher holds a shared_ptr to the
// state; the uv handle's `data` points at the same state. `close()` stops the
// handle and releases the pending-work retain exactly once; the uv callback
// holds a shared_ptr so an event already queued cannot dangle after close.
// Failures to START watching (e.g. a missing path) throw `Thrown` from
// `watch(...)`; runtime event errors have no listener surface and are dropped
// (documented on the header).
// =============================================================================
#include "cp3/std/fs.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <uv.h>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/uv_loop.hpp"

namespace cp3::runtime::fs {

namespace detail {

struct FsWatcherImpl : std::enable_shared_from_this<FsWatcherImpl> {
    FsWatcherImpl(std::string p, std::function<void(std::string, std::string)> c)
        : path(std::move(p)), callback(std::move(c)) {}

    std::string path;
    std::function<void(std::string, std::string)> callback;
    uv_fs_event_t handle{};
    bool started = false;
    bool closed = false;
};

}  // namespace detail

namespace {

/// The libuv fs_event callback (runs on the uv loop). Posts the user
/// callback as a microtask on the runtime loop; the state is captured by
/// shared_ptr so an event queued before `close()` stays valid.
void onFsEvent(uv_fs_event_t* handle, const char* filename, int events,
               int status) {
    auto* state = static_cast<detail::FsWatcherImpl*>(handle->data);
    if (state == nullptr || status != 0) return;  // dropped: no error surface
    if (state->closed) return;
    // shared_from_this(): the queued microtask keeps the state alive; `close()`
    // (via the FsWatcher destructor) is what ends delivery, and the queued
    // task re-checks `closed` before invoking the callback.
    std::shared_ptr<detail::FsWatcherImpl> self;
    try {
        self = state->shared_from_this();
    } catch (...) {
        return;
    }
    std::string eventType;
    if (events & UV_RENAME) eventType = "rename";
    if (events & UV_CHANGE) eventType = "change";  // change wins when both
    std::string name = filename != nullptr ? std::string(filename) : std::string();
    currentLoop().post([self, eventType = std::move(eventType),
                        name = std::move(name)]() mutable {
        if (self->closed) return;
        if (self->callback) self->callback(std::move(eventType), std::move(name));
    });
}

}  // namespace

FsWatcher::FsWatcher(std::shared_ptr<detail::FsWatcherImpl> impl)
    : impl_(std::move(impl)) {}

FsWatcher::~FsWatcher() { close(); }

void FsWatcher::close() const {
    auto* state = impl_.get();
    if (state == nullptr || state->closed) return;
    state->closed = true;
    if (state->started) {
        uv_fs_event_stop(&state->handle);
        state->started = false;
    }
    currentLoop().releaseExternal();
}

FsWatcher watch(const std::string& path,
                std::function<void(std::string, std::string)> callback) {
    uv::ensureLoop();
    void* loop = uv::loopHandle();
    if (loop == nullptr) throw Thrown::make("EIO: fs.watch: no event loop");
    auto state = std::make_shared<detail::FsWatcherImpl>(path, std::move(callback));
    state->handle.data = state.get();
    const int rc = uv_fs_event_init(static_cast<uv_loop_t*>(loop), &state->handle);
    if (rc != 0) throw Thrown::make(std::string("EIO: fs.watch init failed, '") + path + "'");
    const int startRc = uv_fs_event_start(&state->handle, onFsEvent, path.c_str(), 0);
    if (startRc != 0) {
        uv_fs_event_stop(&state->handle);
        throw Thrown::make(std::string("ENOENT: no such file or directory, watch '") +
                           path + "'");
    }
    state->started = true;
    currentLoop().retainExternal();
    return FsWatcher(state);
}

}  // namespace cp3::runtime::fs
