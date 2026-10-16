// =============================================================================
// cp3/src/eventloop.cpp — the single-threaded, per-thread event loop.
// =============================================================================
#include "cp3/eventloop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace cp3::runtime {

EventLoop::EventLoop() = default;
EventLoop::~EventLoop() = default;

// --- scheduling -------------------------------------------------------------

void EventLoop::post(Task task) { microtasks_.push_back(std::move(task)); }

void EventLoop::postFromThread(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_.push_back(std::move(task));
    }
    wakeCv_.notify_all();
}

EventLoop::TimerId EventLoop::addTimer(std::int64_t delayMs, Task task) {
    const TimerId id = nextTimerId_++;
    const std::int64_t deadline = nowMs() + (delayMs > 0 ? delayMs : 0);
    timers_.emplace(std::make_pair(deadline, id), std::move(task));
    return id;
}

bool EventLoop::cancelTimer(TimerId id) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->first.second == id) {
            timers_.erase(it);
            return true;
        }
    }
    return false;
}

void EventLoop::setDrainHook(std::function<void()> hook) { drainHook_ = std::move(hook); }

void EventLoop::retainExternal() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++external_;
    }
    wakeCv_.notify_all();
}

void EventLoop::releaseExternal() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (external_ > 0) {
            --external_;
        }
    }
    wakeCv_.notify_all();
}

bool EventLoop::hasExternalWork() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return external_ != 0;
}

// --- driving ----------------------------------------------------------------

namespace {

// Move every cross-thread task into the loop-thread queue.
void takeIncomingLocked(std::mutex& mutex, std::deque<EventLoop::Task>& incoming,
                        std::deque<EventLoop::Task>& ready, bool& any) {
    std::deque<EventLoop::Task> local;
    {
        std::lock_guard<std::mutex> lock(mutex);
        local.swap(incoming);
    }
    if (!local.empty()) {
        any = true;
        while (!local.empty()) {
            ready.push_back(std::move(local.front()));
            local.pop_front();
        }
    }
}

}  // namespace

std::size_t EventLoop::drainMicrotasks() {
    std::size_t drained = 0;
    while (!microtasks_.empty()) {
        Task task = std::move(microtasks_.front());
        microtasks_.pop_front();
        task();
        ++drained;
    }
    return drained;
}

bool EventLoop::runOnce() {
    bool didWork = false;

    if (drainMicrotasks() > 0) {
        didWork = true;
    }

    bool incoming = false;
    takeIncomingLocked(mutex_, incoming_, readyTasks_, incoming);
    if (incoming) {
        didWork = true;
    }

    if (!readyTasks_.empty()) {
        std::deque<Task> batch;
        batch.swap(readyTasks_);
        while (!batch.empty()) {
            Task task = std::move(batch.front());
            batch.pop_front();
            task();
        }
        didWork = true;
    }

    const std::int64_t now = nowMs();
    std::vector<Task> due;
    while (!timers_.empty() && timers_.begin()->first.first <= now) {
        due.push_back(std::move(timers_.begin()->second));
        timers_.erase(timers_.begin());
    }
    for (Task& task : due) {
        task();
        didWork = true;
    }

    if (drainMicrotasks() > 0) {
        didWork = true;
    }
    return didWork;
}

std::size_t EventLoop::pending() const noexcept {
    std::size_t queued = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queued = incoming_.size() + external_;
    }
    return microtasks_.size() + readyTasks_.size() + timers_.size() + queued;
}

std::size_t EventLoop::microtaskCount() const noexcept { return microtasks_.size(); }

std::int64_t EventLoop::nowMs() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void EventLoop::stop() noexcept {
    stopped_ = true;
    wakeCv_.notify_all();
}

bool EventLoop::pumpReadyTasks() {
    bool incoming = false;
    takeIncomingLocked(mutex_, incoming_, readyTasks_, incoming);
    if (readyTasks_.empty()) {
        return false;
    }
    std::deque<Task> batch;
    batch.swap(readyTasks_);
    while (!batch.empty()) {
        Task task = std::move(batch.front());
        batch.pop_front();
        task();
        drainMicrotasks();
    }
    return true;
}

bool EventLoop::pumpDueTimers() {
    const std::int64_t now = nowMs();
    if (timers_.empty() || timers_.begin()->first.first > now) {
        return false;
    }
    std::vector<Task> due;
    while (!timers_.empty() && timers_.begin()->first.first <= now) {
        due.push_back(std::move(timers_.begin()->second));
        timers_.erase(timers_.begin());
    }
    for (Task& task : due) {
        task();
        drainMicrotasks();
    }
    return true;
}

bool EventLoop::waitForNext() {
    if (!timers_.empty()) {
        const std::int64_t deadline = timers_.begin()->first.first;
        const std::int64_t now = nowMs();
        if (deadline <= now) {
            return true;  // already due; pump it on the next turn
        }
        // Wait for the deadline, or wake early on a cross-thread post.
        std::unique_lock<std::mutex> lock(mutex_);
        wakeCv_.wait_for(lock, std::chrono::milliseconds(deadline - now),
                         [this] { return !incoming_.empty() || stopped_; });
        return true;
    }
    if (hasExternalWork()) {
        // No local work, but a worker is still in flight: wait for it to post its
        // result (or for `stop`). Without this the process would exit before
        // concurrent work completed.
        std::unique_lock<std::mutex> lock(mutex_);
        wakeCv_.wait(lock,
                     [this] { return !incoming_.empty() || stopped_ || external_ == 0; });
        return true;
    }
    return false;  // nothing queued, no timers, no producers: done
}

void EventLoop::run() {
    if (inRun_) {
        // A nested `run()` would be a codegen bug; just make progress.
        drainMicrotasks();
        return;
    }
    inRun_ = true;

    while (true) {
        drainMicrotasks();
        if (pumpReadyTasks()) {
            continue;
        }
        if (stopped_) {
            break;
        }
        if (pumpDueTimers()) {
            continue;
        }
        if (waitForNext()) {
            continue;
        }
        break;
    }

    drainMicrotasks();
    inRun_ = false;

    if (drainHook_) {
        drainHook_();
    }
}

// --- the per-thread loop ----------------------------------------------------

EventLoop& currentLoop() {
    // The loop is intentionally leaked: it is process-lived, one per thread, and
    // a `thread_local` *destructor* here is unsafe. A detached `worker` thread is
    // still tearing down its own loop while the process/other threads run, and
    // MinGW's TLS-destructor path corrupts the heap in that window (reproduced:
    // a `thread_local EventLoop` with a non-trivial destructor crashes at thread
    // exit). A pointer `thread_local` has no destructor to run, so nothing is
    // torn down at exit; the bounded per-thread allocation is reclaimed when the
    // process (or thread) ends.
    static thread_local EventLoop* loop = new EventLoop();
    return *loop;
}

void runEventLoop() { currentLoop().run(); }

// --- timer builtins ---------------------------------------------------------

TimerHandle setTimeout(EventLoop::Task fn, double delayMs) {
    return currentLoop().addTimer(static_cast<std::int64_t>(delayMs < 0 ? 0 : delayMs),
                                  std::move(fn));
}

void clearTimeout(TimerHandle handle) { currentLoop().cancelTimer(handle); }

namespace {

struct IntervalState {
    EventLoop* loop = nullptr;
    EventLoop::Task fn;
    double periodMs = 0;
    EventLoop::TimerId current = 0;
    bool active = true;
    std::function<void()> arm;
};

std::mutex intervalMutex;
std::map<TimerHandle, std::shared_ptr<IntervalState>> intervals;
TimerHandle nextInterval = 1;

}  // namespace

TimerHandle setInterval(EventLoop::Task fn, double intervalMs) {
    EventLoop& loop = currentLoop();
    auto state = std::make_shared<IntervalState>();
    state->loop = &loop;
    state->fn = std::move(fn);
    state->periodMs = intervalMs > 0 ? intervalMs : 0;
    state->arm = [state]() {
        state->current = state->loop->addTimer(static_cast<std::int64_t>(state->periodMs),
                                               [state]() {
                                                   if (!state->active) {
                                                       return;
                                                   }
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
