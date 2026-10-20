// =============================================================================
// cp3/parallel/worker_channel.hpp - one direction of a worker's typed channel.
//
// OWNERSHIP: runtime module. Part of the generated-code ABI (included by
// cp3/parallel/worker.hpp; codegen includes only the latter).
//
// `ChannelPort<P>` is the mailbox + listener registry of one direction and
// `WorkerChannel<In, Out>` owns both directions. The worker handles in
// worker.hpp are thin facades over these; keeping the channel here lets the
// public header stay within the header budget (STYLE.md sec.2).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/core/value.hpp"

namespace cp3::runtime {

namespace detail {

// A stand-in slot for a `void` channel direction (never carries a value).
struct VoidSlot {};

template <class P>
struct PortSlot {
    using type = P;
};

template <>
struct PortSlot<void> {
    using type = VoidSlot;
};

template <class P>
using port_slot_t = typename PortSlot<P>::type;

// A void direction has a nullary listener. `std::conditional_t` cannot spell
// this: both branches are formed eagerly, and `std::function<void(const void&)>`
// is ill-formed even when the void branch would be selected.
template <class P>
struct PortListener {
    using type = std::function<void(const P&)>;
};

template <>
struct PortListener<void> {
    using type = std::function<void()>;
};

template <class P>
using port_listener_t = typename PortListener<P>::type;

}  // namespace detail
// ---------------------------------------------------------------------------
// ChannelPort<P> - one direction of a worker's typed channel.
//
// A port is both a mailbox and a listener registry. Sending is thread-safe
// (that is the whole point: the other end lives on another OS thread); inbound
// traffic is drained on the `EventLoop` the port is bound to, so listeners run
// on their own thread's loop, never on the sender's.
//
// Queue-until-listener: an event with no listener is left in the queue; the
// next attach re-pokes the loop and delivers it. Message events are FIFO; an
// error event never overtakes an older message that has a listener (or vice
// versa) because both share one sequence counter.
// ---------------------------------------------------------------------------
template <class P>
class ChannelPort {
public:
    using Slot = detail::port_slot_t<P>;
    using MessageListener = detail::port_listener_t<P>;
    using ErrorListener = std::function<void(const Thrown&)>;
    using RequestPromise =
        std::conditional_t<std::is_void_v<P>, Promise<void>, Promise<P>>;

    ChannelPort() = default;
    ChannelPort(const ChannelPort&) = delete;
    ChannelPort& operator=(const ChannelPort&) = delete;

    // What to do with an error thrown by one of this port's listeners: report
    // it as an `"error"` event on the opposite direction of the channel
    // (`thread.on("error")` / `w.on("error")`), per D80. The peer port carries a
    // different payload type, so the wiring is type-erased here.
    void setErrorSink(std::function<void(const Thrown&)> sink) {
        errorSink_ = std::move(sink);
    }

    // Bind the loop that services inbound traffic. Posts one drain turn so
    // everything queued before the bind is delivered promptly, and so a queue
    // that filled while no loop was bound cannot stall.
    void bind(EventLoop& loop) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = &loop;
        }
        poke();
    }

    void onMessage(MessageListener listener) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            messageListeners_.push_back(std::move(listener));
        }
        poke();
    }

    void onError(ErrorListener listener) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            errorListeners_.push_back(std::move(listener));
        }
        poke();
    }

    // Queue a `request(v)` answer slot. The next message event fulfils the
    // oldest slot instead of reaching the message listeners.
    void addRequest(RequestPromise promise) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;  // `terminate()` already ran: the request is dropped
            }
            requests_.push_back(std::move(promise));
        }
        poke();
    }

    // Send a value: deep copy now (the sender's thread), enqueue, wake the peer
    // loop. Never blocks.
    template <class U = P, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    void send(const U& value) {
        pushMessage(Slot(clone(value)));
    }

    // Send an error event (a handler's throw on the other side).
    void sendError(const Thrown& reason) {
        pushError(reason.deepCopy());
    }

    // Drop everything queued and stop accepting sends. `terminate()` uses this;
    // pending requests are dropped (left unsettled, not rejected - a rejection
    // nobody observes would trip D43).
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        messages_.clear();
        errors_.clear();
        requests_.clear();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    // Deliver everything that has a consumer right now. Runs on the bound loop
    // thread (posted by `poke`), never on a sender's thread; listeners are
    // invoked outside the lock and a throwing one becomes an error event on the
    // sibling port - the throwing side keeps serving (D80).
    void drain() {
        for (;;) {
            bool isError = false;
            bool haveMessage = false;
            bool haveError = false;
            bool hasRequest = false;
            Slot value{};
            Thrown error;
            std::vector<MessageListener> messageListeners;
            std::vector<ErrorListener> errorListeners;
            RequestPromise request;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closed_) {
                    return;
                }
                haveMessage =
                    !messages_.empty() && (!requests_.empty() || !messageListeners_.empty());
                haveError = !errors_.empty() && !errorListeners_.empty();
                if (!haveMessage && !haveError) {
                    return;  // nothing anyone can consume; wait for an attach
                }
                // Oldest event first, whichever queue it is in.
                isError = haveError &&
                          (!haveMessage ||
                           errors_.front().seq < messages_.front().seq);
                if (isError) {
                    error = std::move(errors_.front().reason);
                    errors_.pop_front();
                    errorListeners = errorListeners_;
                } else {
                    value = std::move(messages_.front().value);
                    messages_.pop_front();
                    if (!requests_.empty()) {
                        request = std::move(requests_.front());
                        requests_.pop_front();
                        hasRequest = true;
                    } else {
                        messageListeners = messageListeners_;
                    }
                }
            }

            if (isError) {
                for (const ErrorListener& listener : errorListeners) {
                    try {
                        listener(error);
                    } catch (...) {
                        // An error listener's own throw must not ping-pong; the
                        // loop keeps serving (D80: only message handlers report).
                    }
                }
                continue;
            }

            if (hasRequest) {
                try {
                    if constexpr (std::is_void_v<P>) {
                        request.fulfill();
                    } else {
                        request.fulfill(value);
                    }
                } catch (...) {
                    reportHandlerThrow();
                }
                continue;
            }

            for (const MessageListener& listener : messageListeners) {
                try {
                    invokeMessage(listener, value);
                } catch (...) {
                    reportHandlerThrow();
                }
            }
        }
    }

private:
    struct MessageEvent {
        std::uint64_t seq = 0;
        Slot value{};
    };

    struct ErrorEvent {
        std::uint64_t seq = 0;
        Thrown reason;
    };

    static void invokeMessage(const MessageListener& listener, const Slot& value) {
        if constexpr (std::is_void_v<P>) {
            (void)value;
            listener();
        } else {
            listener(value);
        }
    }

    void pushMessage(Slot value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            messages_.push_back(MessageEvent{nextSeq_++, std::move(value)});
        }
        poke();
    }

    void pushError(Thrown reason) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            errors_.push_back(ErrorEvent{nextSeq_++, std::move(reason)});
        }
        poke();
    }

    // Turn a listener's C++ exception into an error event on the sibling port.
    void reportHandlerThrow() {
        if (errorSink_) {
            errorSink_(Thrown::fromCurrentException());
        }
    }

    // Nudge the bound loop to run a drain turn. Safe from any thread.
    void poke() {
        EventLoop* loop = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop = loop_;
        }
        if (loop != nullptr) {
            loop->postFromThread([this]() { drain(); });
        }
    }

    mutable std::mutex mutex_;
    EventLoop* loop_ = nullptr;
    std::function<void(const Thrown&)> errorSink_;
    bool closed_ = false;
    std::uint64_t nextSeq_ = 0;
    std::deque<MessageEvent> messages_;
    std::deque<ErrorEvent> errors_;
    std::vector<MessageListener> messageListeners_;
    std::vector<ErrorListener> errorListeners_;
    std::deque<RequestPromise> requests_;
};

// ---------------------------------------------------------------------------
// WorkerChannel<In, Out> - the two directions of one worker's channel.
// ---------------------------------------------------------------------------
template <class In, class Out>
class WorkerChannel {
public:
    WorkerChannel() {
        toWorker_.setErrorSink([this](const Thrown& reason) { toMain_.sendError(reason); });
        toMain_.setErrorSink([this](const Thrown& reason) { toWorker_.sendError(reason); });
    }

    WorkerChannel(const WorkerChannel&) = delete;
    WorkerChannel& operator=(const WorkerChannel&) = delete;

    ChannelPort<In>& toWorker() { return toWorker_; }
    ChannelPort<Out>& toMain() { return toMain_; }

    // Main-thread side of the wiring (done before the thread starts).
    void bindMain(EventLoop& loop) { toMain_.bind(loop); }

    // Worker-thread startup. Retains the worker's own loop as an external
    // producer so `run()` stays alive (and never exits) while messages can
    // still arrive. Returns false when `terminate()` already ran: the thread
    // must exit without running the entry or the loop.
    bool bindWorker(EventLoop& loop) {
        toWorker_.bind(loop);
        std::lock_guard<std::mutex> lock(stateMutex_);
        workerLoop_ = &loop;
        if (stopRequested_) {
            return false;
        }
        loop.retainExternal();
        workerLoopRetained_ = true;
        return true;
    }

    // Worker-thread exit: hand back the loop retain if it still holds one.
    void workerExiting() noexcept {
        EventLoop* loop = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (workerLoopRetained_) {
                loop = workerLoop_;
                workerLoopRetained_ = false;
            }
        }
        if (loop != nullptr) {
            loop->releaseExternal();
        }
    }

    // `w.terminate()`: stop the worker loop, drop queued traffic and pending
    // requests. Idempotent; returns true only for the first call.
    bool requestStop() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (stopRequested_) {
            return false;
        }
        stopRequested_ = true;
        if (workerLoopRetained_) {
            workerLoopRetained_ = false;
            workerLoop_->releaseExternal();
            workerLoop_->stop();
        }
        toWorker_.close();
        toMain_.close();
        return true;
    }

private:
    mutable std::mutex stateMutex_;
    EventLoop* workerLoop_ = nullptr;
    bool workerLoopRetained_ = false;
    bool stopRequested_ = false;
    ChannelPort<In> toWorker_;
    ChannelPort<Out> toMain_;
};

}  // namespace cp3::runtime
