// =============================================================================
// src/std/console/runtime_pump.cpp - the async stdin pump (D71/D75): input,
// readKey, on and off.
//
// OWNERSHIP: the std/console family (src/std/console/**). Part of the
// generated-code ABI declared in cp3/std/console.hpp; the shared parcel with
// the platform readers (runtime_pump_read.cpp) is the private pump.hpp.
//
// One detached helper thread reads stdin and posts every event through
// `EventLoop::postFromThread` (the loop's cross-thread mechanism); no second
// loop is ever spun, and a blocking read never stalls pending async work. The
// pump is demand-driven - the doc's start/stop contract - reading (and leaving
// a real terminal in raw mode) while at least one `input()`, `readKey()` or
// listener is outstanding, and stopping (restoring the terminal) when the last
// one is gone, which is also what lets the loop drain and the program exit.
//
// Fan-out (D75): an event goes to a pending request AND to every listener of
// the matching event; events read with no pending request are buffered (bounded)
// so a request that starts later still sees them. EOF resolves every pending
// request with "" and fires "close" once, after which the stream is finished.
// =============================================================================
#include "cp3/std/console.hpp"
#include "pump.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"

#if !defined(_WIN32)
#  include <unistd.h>
#endif

namespace cp3::runtime::console::detail {
namespace {

enum class EventKind { Line, Key, Close };

using Listener = PumpState::Listener;

// A pipeline may send more input than any request ever reads, and the replay
// buffer must not grow without bound.
constexpr std::size_t kMaxBuffered = 1024;

void bufferLocked(std::deque<std::string>& buffer, std::string value) {
    buffer.push_back(std::move(value));
    while (buffer.size() > kMaxBuffered) buffer.pop_front();
}

std::size_t demandLocked(const PumpState& st) {
    return st.pendingLines.size() + st.pendingKeys.size() +
           st.lineListeners.size() + st.keyListeners.size() +
           st.closeListeners.size();
}

// --- loop-thread delivery ----------------------------------------------------

void dispatchLine(PumpState* st, const std::string& line) {
    std::vector<std::function<void(std::string)>> listeners;
    Promise<std::string> request;
    bool hasRequest = false;
    {
        std::lock_guard<std::mutex> lock(st->mutex);
        for (const Listener& l : st->lineListeners) listeners.push_back(l.fn);
        if (st->pendingLines.empty()) {
            bufferLocked(st->bufferedLines, line);
        } else {
            request = st->pendingLines.front();
            st->pendingLines.pop_front();
            hasRequest = true;
        }
    }
    if (hasRequest) request.fulfill(line);
    for (const auto& fn : listeners) fn(line);
    refreshDemand(*st);
}

void dispatchKey(PumpState* st, const std::string& key) {
    std::vector<std::function<void(std::string)>> listeners;
    Promise<std::string> request;
    bool hasRequest = false;
    {
        std::lock_guard<std::mutex> lock(st->mutex);
        for (const Listener& l : st->keyListeners) listeners.push_back(l.fn);
        if (st->pendingKeys.empty()) {
            bufferLocked(st->bufferedKeys, key);
        } else {
            request = st->pendingKeys.front();
            st->pendingKeys.pop_front();
            hasRequest = true;
        }
    }
    if (hasRequest) request.fulfill(key);
    for (const auto& fn : listeners) fn(key);
    refreshDemand(*st);
}

// The stream ended: every pending request resolves with "" (docs §04), the
// close listeners fire once with "", and the console stream is finished -
// listeners left registered could never fire again and must not keep the loop
// alive.
void dispatchClose(PumpState* st) {
    std::vector<Promise<std::string>> lines;
    std::vector<Promise<std::string>> keys;
    std::vector<std::function<void(std::string)>> listeners;
    {
        std::lock_guard<std::mutex> lock(st->mutex);
        if (st->eof) return;
        st->eof = true;
        lines.assign(st->pendingLines.begin(), st->pendingLines.end());
        keys.assign(st->pendingKeys.begin(), st->pendingKeys.end());
        st->pendingLines.clear();
        st->pendingKeys.clear();
        st->bufferedLines.clear();
        st->bufferedKeys.clear();
        for (const Listener& l : st->closeListeners) listeners.push_back(l.fn);
        st->lineListeners.clear();
        st->keyListeners.clear();
        st->closeListeners.clear();
    }
    for (Promise<std::string>& p : lines) p.fulfill(std::string());
    for (Promise<std::string>& p : keys) p.fulfill(std::string());
    for (const auto& fn : listeners) fn(std::string());
    refreshDemand(*st);
}

}  // namespace

PumpState& pumpState() {
    static PumpState* state = new PumpState();
    return *state;
}

void enterRawMode(PumpState& st) {
    if (st.rawMode) return;
#if defined(_WIN32)
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (in == nullptr || in == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(in, &mode)) return;
    st.savedMode = mode;
    const DWORD raw = mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    if (SetConsoleMode(in, raw)) st.rawMode = true;
#else
    if (isatty(STDIN_FILENO) == 0) return;
    termios mode {};
    if (tcgetattr(STDIN_FILENO, &mode) != 0) return;
    st.savedTermios = mode;
    mode.c_lflag &= ~(ICANON | ECHO);
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) == 0) st.rawMode = true;
#endif
}

void leaveRawMode(PumpState& st) {
    if (!st.rawMode) return;
    st.rawMode = false;
#if defined(_WIN32)
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in != nullptr && in != INVALID_HANDLE_VALUE) {
        SetConsoleMode(in, st.savedMode);
    }
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &st.savedTermios);
#endif
}

void refreshDemand(PumpState& st) {
    bool retain = false;
    bool release = false;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        const bool wanted = demandLocked(st) != 0;
        if (wanted && !st.retained) {
            st.retained = true;
            retain = true;
            enterRawMode(st);
        } else if (!wanted && st.retained) {
            st.retained = false;
            release = true;
            leaveRawMode(st);
        }
    }
    if (st.loop == nullptr) return;
    if (retain) st.loop->retainExternal();
    if (release) st.loop->releaseExternal();
}

void postLine(PumpState* st, std::string line) {
    st->loop->postFromThread(
        [st, line = std::move(line)]() { dispatchLine(st, line); });
}

void postKey(PumpState* st, std::string key) {
    st->loop->postFromThread(
        [st, key = std::move(key)]() { dispatchKey(st, key); });
}

void postEnd(PumpState* st) {
    if (st->loop == nullptr) return;
    st->loop->postFromThread([st]() { dispatchClose(st); });
}

bool waitForDemand(PumpState& st) {
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(st.mutex);
            if (st.eof) return false;
            if (demandLocked(st) != 0) return true;
        }
        // Short enough to be invisible, long enough to never spin hot.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

namespace {

// Called on the requester's (loop) thread before the first demand is stored.
void startPump(PumpState& st) {
    bool start = false;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        if (st.loop == nullptr) st.loop = &currentLoop();
        if (!st.started) {
            st.started = true;
            start = true;
        }
    }
    if (start) std::thread(readerMain, &st).detach();
}

std::optional<EventKind> parseEvent(const std::string& event) {
    if (event == "line") return EventKind::Line;
    if (event == "keypress") return EventKind::Key;
    if (event == "close") return EventKind::Close;
    return std::nullopt;  // unknown events never register
}

std::vector<Listener>& listenersFor(PumpState& st, EventKind kind) {
    if (kind == EventKind::Line) return st.lineListeners;
    if (kind == EventKind::Key) return st.keyListeners;
    return st.closeListeners;
}

}  // namespace

// The shared body of input()/readKey(): a buffered event resolves immediately,
// otherwise the request waits for the pump (or for "" at EOF). Lives in the
// detail namespace because the public members (below, in
// cp3::runtime::console) are thin wrappers over it.
Promise<std::string> request(bool line, const std::string& prompt) {
    if (line && !prompt.empty()) {
        std::cout << prompt;  // in the caller's own output stream
        std::cout.flush();
    }
    PumpState& st = pumpState();
    startPump(st);
    Promise<std::string> promise = Promise<std::string>::pending();
    bool immediate = false;
    std::string buffered;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        std::deque<std::string>& ready =
            line ? st.bufferedLines : st.bufferedKeys;
        std::deque<Promise<std::string>>& waiting =
            line ? st.pendingLines : st.pendingKeys;
        if (st.eof) {
            immediate = true;  // EOF resolves with ""
        } else if (ready.empty()) {
            waiting.push_back(promise);
        } else {
            buffered = std::move(ready.front());
            ready.pop_front();
            immediate = true;
        }
    }
    if (immediate) promise.fulfill(std::move(buffered));
    refreshDemand(st);
    return promise;
}

void addListener(const std::string& event,
                 std::function<void(std::string)> listener,
                 const void* typeTag) {
    const std::optional<EventKind> kind = parseEvent(event);
    if (!kind) return;
    PumpState& st = pumpState();
    startPump(st);
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        finished = st.eof;
        if (!finished) {
            listenersFor(st, *kind).push_back({typeTag, std::move(listener)});
        }
    }
    if (finished) {
        // The stream is over: "close" still fires once on the next loop turn;
        // a line/keypress registration could never fire and must not keep the
        // program alive, so it is dropped.
        if (*kind == EventKind::Close) {
            st.loop->post([fn = std::move(listener)]() { fn(std::string()); });
        }
        return;
    }
    refreshDemand(st);
}

void removeListener(const std::string& event, const void* typeTag) {
    const std::optional<EventKind> kind = parseEvent(event);
    if (!kind) return;
    PumpState& st = pumpState();
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        std::vector<Listener>& listeners = listenersFor(st, *kind);
        for (auto it = listeners.begin(); it != listeners.end(); ++it) {
            if (it->typeTag == typeTag) {
                listeners.erase(it);
                break;
            }
        }
    }
    refreshDemand(st);
}

}  // namespace cp3::runtime::console::detail

namespace cp3::runtime::console {

Promise<std::string> input(const std::string& prompt) {
    return detail::request(true, prompt);
}

Promise<std::string> readKey() { return detail::request(false, std::string()); }

}  // namespace cp3::runtime::console
