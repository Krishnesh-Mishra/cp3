// =============================================================================
// cp3/src/async/uv_loop.cpp - the shared per-thread libuv loop.
//
// `std/network` owns the uWebSockets side; `std/io` owns the generic fd poll
// side. Both call `uv::ensureLoop()`, so there is exactly one libuv loop per
// thread and exactly one EventLoop external source. The loop is leaked on
// purpose, like `currentLoop()`.
// =============================================================================
#include "cp3/async/uv_loop.hpp"

#include <cstdint>

#include <uv.h>

#include "cp3/async/eventloop.hpp"

namespace cp3::runtime::uv {
namespace {

struct LoopState {
    uv_loop_t loop{};
    uv_timer_t bridge{};  // one-shot bound armed around each blocking turn
    uv_async_t wake{};    // thread-safe nudge used by EventLoop::postFromThread
};

void onBridgeFire(uv_timer_t*) {}  // arming is the whole job
void onWake(uv_async_t*) {}        // waking the poll is the whole job

LoopState* createLoopState() {
    LoopState* state = new LoopState;
    if (uv_loop_init(&state->loop) != 0) {
        delete state;
        return nullptr;
    }
    if (uv_timer_init(&state->loop, &state->bridge) != 0) {
        uv_loop_close(&state->loop);
        delete state;
        return nullptr;
    }
    if (uv_async_init(&state->loop, &state->wake, onWake) != 0) {
        uv_loop_close(&state->loop);
        delete state;
        return nullptr;
    }
    currentLoop().setExternalSource(
        [state] { return uv_run(&state->loop, UV_RUN_NOWAIT) != 0; },
        [state](std::int64_t maxWaitMs) {
            if (maxWaitMs >= 0) {
                uv_timer_start(&state->bridge, onBridgeFire,
                               static_cast<std::uint64_t>(maxWaitMs), 0);
            } else {
                uv_timer_stop(&state->bridge);
            }
            uv_run(&state->loop, UV_RUN_ONCE);
            uv_timer_stop(&state->bridge);
        },
        [state] { uv_async_send(&state->wake); });
    return state;
}

LoopState* loopState() {
    static thread_local LoopState* state = createLoopState();
    return state;
}

}  // namespace

void ensureLoop() { (void)loopState(); }

void* loopHandle() {
    LoopState* state = loopState();
    return state == nullptr ? nullptr : &state->loop;
}

}  // namespace cp3::runtime::uv
