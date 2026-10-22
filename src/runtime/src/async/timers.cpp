// =============================================================================
// cp3/src/async/timers.cpp - the timer builtins of cp3/async/eventloop.hpp.
//
// OWNERSHIP: runtime module. `setTimeout`/`setInterval`/`clearTimeout`/
// `clearInterval` are core builtins because they map directly onto the loop
// (docs/language-spec/14-async.md). The loop itself lives in eventloop.cpp;
// this TU is the builtin surface, including the repeating-timer bookkeeping.
// =============================================================================
#include <map>
#include <memory>
#include <mutex>
#include <utility>

#include "cp3/async/eventloop.hpp"

namespace cp3::runtime {

TimerHandle setTimeout(Task fn, double delayMs) {
    return currentLoop().addTimer(static_cast<std::int64_t>(delayMs < 0 ? 0 : delayMs),
                                  std::move(fn));
}

void clearTimeout(TimerHandle handle) { currentLoop().cancelTimer(handle); }

namespace {

struct IntervalState {
    EventLoop* loop = nullptr;
    Task fn;
    std::int64_t periodMs = 1;
    std::int64_t nextDeadline = 0;
    EventLoop::TimerId current = 0;
    bool active = true;
    std::function<void()> arm;
};

std::mutex intervalMutex;
std::map<TimerHandle, std::shared_ptr<IntervalState>> intervals;
TimerHandle nextInterval = 1;

}  // namespace

TimerHandle setInterval(Task fn, double intervalMs) {
    EventLoop& loop = currentLoop();
    auto state = std::make_shared<IntervalState>();
    state->loop = &loop;
    state->fn = std::move(fn);
    // libuv/Node clamp a timer's period to at least 1 ms; a zero period would
    // re-arm on a never-advancing deadline and spin without yielding.
    state->periodMs = static_cast<std::int64_t>(intervalMs) < 1
                          ? 1
                          : static_cast<std::int64_t>(intervalMs);
    state->nextDeadline = EventLoop::nowMs() + state->periodMs;
    state->arm = [state]() {
        // Fixed schedule: every firing is due `periodMs` after the previous
        // *deadline*, not after the previous firing, so a late wake-up (coarse
        // OS timer granularity, a busy loop) never shifts the phase. `addTimer`
        // keeps the overdue deadline, so missed slots fire back-to-back in the
        // current timer phase, ahead of later-deadlined timers - the no-drift
        // catch-up `uv__run_timers` gives a repeating timer.
        const std::int64_t delay = state->nextDeadline - EventLoop::nowMs();
        state->current = state->loop->addTimer(delay, [state]() {
            if (!state->active) {
                return;
            }
            state->nextDeadline += state->periodMs;
            state->fn();
            if (state->active) {
                state->arm();
            }
        });
    };

    std::lock_guard<std::mutex> lock(intervalMutex);
    const TimerHandle handle = nextInterval++;
    intervals.emplace(handle, state);
    state->arm();
    return handle;
}

void clearInterval(TimerHandle handle) {
    std::shared_ptr<IntervalState> state;
    {
        std::lock_guard<std::mutex> lock(intervalMutex);
        auto it = intervals.find(handle);
        if (it == intervals.end()) {
            return;
        }
        state = it->second;
        intervals.erase(it);
    }
    state->active = false;
    if (state->current != 0) {
        state->loop->cancelTimer(state->current);
    }
}

}  // namespace cp3::runtime
