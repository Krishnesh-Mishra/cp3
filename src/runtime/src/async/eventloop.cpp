// =============================================================================
// cp3/src/async/eventloop.cpp - the single-threaded, per-thread event loop.
// =============================================================================
#include "cp3/async/eventloop.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace cp3::runtime {

EventLoop::EventLoop() = default;
EventLoop::~EventLoop() = default;

// --- scheduling -------------------------------------------------------------

void EventLoop::post(Task task) { microtasks_.push_back(std::move(task)); }

void EventLoop::postFromThread(Task task) {
    ExternalWake wake;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_.push_back(std::move(task));
        wake = externalWake_;  // nudge libuv too, if a source is installed
    }
    wakeCv_.notify_all();
    if (wake) wake();
}

EventLoop::TimerId EventLoop::addTimer(std::int64_t delayMs, Task task) {
    const TimerId id = nextTimerId_++;
    if (delayMs == 0) {
        // Due on the next turn. These deadlines are nondecreasing in insertion
        // order (the clock only moves forward) and every one is due at the next
        // pump, so FIFO order is exactly the (deadline, id) order of the heap.
        // The common `sleep(0)`/`setTimeout(_, 0)` path never touches the heap.
        readyPush(TimerEntry{nowMs(), id, std::move(task)});
        return id;
    }
    // A negative delay keeps its (past) deadline instead of collapsing to
    // "now": a repeating timer re-arms against a fixed schedule and its overdue
    // firings must sort ahead of later-deadlined timers by true deadline, the
    // way `uv__run_timers` heaps them (see `setInterval` below).
    const std::int64_t deadline = nowMs() + delayMs;
    heapPush(TimerEntry{deadline, id, std::move(task)});
    return id;
}

bool EventLoop::cancelTimer(TimerId id) {
    for (std::size_t index = readyHead_; index < readyTimers_.size(); ++index) {
        if (readyTimers_[index].id == id) {
            readyTimers_.erase(readyTimers_.begin() + static_cast<std::ptrdiff_t>(index));
            if (readyHead_ >= readyTimers_.size()) {
                readyTimers_.clear();
                readyHead_ = 0;
            }
            return true;
        }
    }
    return cancelFromHeap(id);
}

// --- timer heap -------------------------------------------------------------

void EventLoop::heapPush(TimerEntry entry) {
    timers_.push_back(std::move(entry));
    heapSiftUp(timers_.size() - 1);
}

void EventLoop::heapSiftUp(std::size_t index) {
    while (index > 0) {
        const std::size_t parent = (index - 1) / 2;
        if (!timerBefore(timers_[index], timers_[parent])) {
            return;
        }
        std::swap(timers_[index], timers_[parent]);
        index = parent;
    }
}

void EventLoop::heapSiftDown(std::size_t index) {
    const std::size_t size = timers_.size();
    while (true) {
        const std::size_t left = 2 * index + 1;
        if (left >= size) {
            return;
        }
        std::size_t next = left;
        const std::size_t right = left + 1;
        if (right < size && timerBefore(timers_[right], timers_[left])) {
            next = right;
        }
        if (!timerBefore(timers_[next], timers_[index])) {
            return;
        }
        std::swap(timers_[index], timers_[next]);
        index = next;
    }
}

void EventLoop::heapPopFront() {
    if (timers_.size() == 1) {
        timers_.pop_back();
        return;
    }
    timers_.front() = std::move(timers_.back());
    timers_.pop_back();
    heapSiftDown(0);
}

bool EventLoop::cancelFromHeap(TimerId id) {
    for (std::size_t index = 0; index < timers_.size(); ++index) {
        if (timers_[index].id != id) {
            continue;
        }
        const std::size_t last = timers_.size() - 1;
        if (index != last) {
            timers_[index] = std::move(timers_[last]);
            timers_.pop_back();
            // The replacement can belong above or below its new slot.
            heapSiftUp(index);
            heapSiftDown(index);
        } else {
            timers_.pop_back();
        }
        return true;
    }
    return false;
}

bool EventLoop::nextDueIsReady(std::int64_t now) const noexcept {
    if (readyEmpty()) {
        return false;
    }
    if (timers_.empty() || timers_.front().deadline > now) {
        return true;
    }
    // Equal deadlines fire in insertion order across both containers.
    return timerBefore(readyTimers_[readyHead_], timers_.front());
}

bool EventLoop::popNextDue(std::int64_t now, Task& out) {
    if (nextDueIsReady(now)) {
        out = std::move(readyTimers_[readyHead_].task);
        readyPopFront();
        return true;
    }
    if (!timers_.empty() && timers_.front().deadline <= now) {
        out = std::move(timers_.front().task);
        heapPopFront();
        return true;
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

void EventLoop::setExternalSource(ExternalPoll poll, ExternalWait waitTurn,
                                  ExternalWake wake) {
    std::lock_guard<std::mutex> lock(mutex_);
    externalPoll_ = std::move(poll);
    externalWait_ = std::move(waitTurn);
    externalWake_ = std::move(wake);
}

// --- driving ----------------------------------------------------------------

namespace {

// Move every cross-thread task into the loop-thread queue.
void takeIncomingLocked(std::mutex& mutex, std::deque<Task>& incoming,
                        std::deque<Task>& ready, bool& any) {
    std::deque<Task> local;
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
    while (!microtasksEmpty()) {
        Task task = std::move(microtasks_[microtaskHead_]);
        ++microtaskHead_;
        task();
        ++drained;
    }
    // The whole queue was consumed: keep the buffer (its peak size is the next
    // burst's capacity) but reset the head so the next push starts at 0.
    microtasks_.clear();
    microtaskHead_ = 0;
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
    Task task;
    while (popNextDue(now, task)) {
        due.push_back(std::move(task));
    }
    for (Task& ready : due) {
        ready();
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
    return (microtasks_.size() - microtaskHead_) + readyTasks_.size() +
           (readyTimers_.size() - readyHead_) + timers_.size() + queued;
}

std::size_t EventLoop::microtaskCount() const noexcept {
    return microtasks_.size() - microtaskHead_;
}

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
    bool didWork = false;
    Task task;
    // Re-check "due" after every fired timer, the shape of libuv's
    // `uv__run_timers`: a repeating timer re-armed on its fixed schedule can
    // still be overdue (the loop woke late), and its catch-up firing must
    // happen in this same phase, ahead of later-deadlined timers that are also
    // due (e.g. an `await sleep(...)` that expired while the loop was blocked).
    while (popNextDue(nowMs(), task)) {
        task();
        drainMicrotasks();
        didWork = true;
    }
    return didWork;
}

bool EventLoop::waitForNext() {
    if (!readyEmpty()) {
        return true;  // zero-delay timers are due now; pump them next turn
    }
    // The nearest runtime timer deadline (unset when there are no timers).
    std::int64_t timerWait = -1;
    if (!timers_.empty()) {
        timerWait = timers_.front().deadline - nowMs();
        if (timerWait <= 0) {
            return true;  // already due; pump it on the next turn
        }
    }

    ExternalPoll poll;
    ExternalWait waitTurn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        poll = externalPoll_;
        waitTurn = externalWait_;
    }

    if (poll) {
        // A std/network event source is installed: one non-blocking libuv
        // turn. Its callbacks may queue microtasks/timers on this loop.
        poll();
        if (!microtasksEmpty() || !readyTasks_.empty()) {
            return true;  // work arrived: run it on the next turn
        }
    }

    if (waitTurn && hasExternalWork()) {
        // An in-flight fetch, a listening server or an open socket IS pending
        // work (spec §14): block inside the source for one turn, bounded by
        // the nearest runtime timer so a cp3 timer still fires on time.
        waitTurn(timerWait);
        return true;
    }

    if (timerWait >= 0) {
        // Wait for the deadline, or wake early on a cross-thread post.
        std::unique_lock<std::mutex> lock(mutex_);
        wakeCv_.wait_for(lock, std::chrono::milliseconds(timerWait),
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

// The timer builtins (`setTimeout`/`setInterval`/`clearTimeout`/
// `clearInterval`) live in timers.cpp.

}  // namespace cp3::runtime
