// =============================================================================
// src/std/process/runtime.cpp - argv store + the exec/spawn orchestration.
//
// OWNERSHIP: the std/process family (src/std/process/**). Compiled as a
// runtime TU by the process native row whenever a program imports
// "std/process" (src/std/process/native.hpp). The inline surface (exit, the
// environment map, stdout/stderr streams) stays in cp3/std/process.hpp; this
// unit owns what needs threading and OS handles.
//
// Threading model (D71: one event loop per thread, never a second loop):
//
//   * launch happens on the calling thread, so `spawn` can throw
//     synchronously and `exec` can reject a command that cannot start;
//   * two reader threads pump the child's stdout/stderr (they POST loop
//     tasks, they never touch the loop directly);
//   * one waiter thread joins the readers, closes the child and posts the
//     final event (exec's result / spawn's "exit");
//   * `retainExternal` before the threads start and `releaseExternal` inside
//     the final posted task keep `run()` alive until every event is
//     delivered, exactly like the `worker` builtin (cp3/parallel/worker.hpp).
// =============================================================================
#include "cp3/std/process.hpp"

#include "child.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <uv.h>

namespace cp3::runtime::process {

// The listener table plus the platform child of one live spawn. Defined at
// namespace scope (not in the anonymous namespace below) because SpawnHandle's
// pimpl members live in this namespace and the header only forward-declares
// it. Registration and write/kill may come from the caller thread while the
// loop thread delivers events, so both are mutex-guarded; `closed` marks "the
// child is gone" so late writes/kills are no-ops instead of use-after-close.
struct SpawnState {
    std::mutex mutex;
    std::vector<std::function<void(std::string)>> data;
    std::vector<std::function<void(std::string)>> error;
    std::vector<std::function<void(std::int32_t)>> exits;
    detail::ChildProcess child;
    bool closed = false;
};

namespace {

std::vector<std::string>& argvStore() {
    static std::vector<std::string> store;
    return store;
}

void deliverChunk(SpawnState& state, bool fromStderr, const std::string& chunk) {
    std::vector<std::function<void(std::string)>> handlers;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        handlers = fromStderr ? state.error : state.data;
    }
    for (auto& handler : handlers) handler(chunk);
}

void deliverExit(SpawnState& state, std::int32_t code) {
    std::vector<std::function<void(std::int32_t)>> handlers;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        handlers = state.exits;
    }
    for (auto& handler : handlers) handler(code);
}

/// Pump one pipe to EOF, posting every chunk as a loop task. Runs on a reader
/// thread; `fromStderr` selects the pipe and the event name.
void pumpChunks(const std::shared_ptr<SpawnState>& state, EventLoop& loop,
                bool fromStderr) {
    char buffer[4096];
    std::size_t got = 0;
    while (detail::readChunk(state->child, fromStderr, buffer, sizeof(buffer),
                             got)) {
        const std::string chunk(buffer, got);
        loop.postFromThread([state, fromStderr, chunk]() {
            deliverChunk(*state, fromStderr, chunk);
        });
    }
}

/// Wait for the child, join the readers, then post "exit" (and release the
/// loop's external-work hold from inside that task, so the loop cannot drain
/// between the post and its delivery). Runs on the waiter thread.
void waitAndPost(const std::shared_ptr<SpawnState>& state, EventLoop& loop,
                 std::thread& outThread, std::thread& errThread) {
    detail::waitChild(state->child);
    if (outThread.joinable()) outThread.join();
    if (errThread.joinable()) errThread.join();
    const std::int32_t code = detail::childExitCode(state->child);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        detail::closeChild(state->child);
        state->closed = true;
    }
    loop.postFromThread([state, code, &loop]() {
        deliverExit(*state, code);
        loop.releaseExternal();
    });
}

std::shared_ptr<SpawnState> startSpawn(const std::string& cmd,
                                       const std::vector<std::string>& args) {
    auto state = std::make_shared<SpawnState>();
    std::string error;
    if (!detail::launchChild(cmd, args, state->child, error)) {
        throw Thrown::make("process.spawn: " + error);
    }
    EventLoop& loop = currentLoop();
    loop.retainExternal();
    std::thread outThread;
    std::thread errThread;
    try {
        outThread = std::thread(pumpChunks, state, std::ref(loop), false);
        errThread = std::thread(pumpChunks, state, std::ref(loop), true);
        std::thread([state, &loop, out = std::move(outThread),
                     err = std::move(errThread)]() mutable {
            waitAndPost(state, loop, out, err);
        }).detach();
    } catch (const std::system_error& ex) {
        detail::killChild(state->child);
        if (outThread.joinable()) outThread.join();
        if (errThread.joinable()) errThread.join();
        detail::closeChild(state->child);
        loop.releaseExternal();
        throw Thrown::make(std::string("process.spawn: ") + ex.what());
    }
    return state;
}

/// Read one pipe into `out`; runs on a reader thread of `exec`.
void collectChunks(detail::ChildProcess& child, std::string& out) {
    char buffer[4096];
    std::size_t got = 0;
    while (detail::readChunk(child, true, buffer, sizeof(buffer), got)) {
        out.append(buffer, got);
    }
}

void runExec(Promise<ExecResult> result, EventLoop& loop,
             const std::shared_ptr<detail::ChildProcess>& child) {
    ExecResult out;
    std::string err;
    std::thread errThread([&child, &err]() { collectChunks(*child, err); });
    char buffer[4096];
    std::size_t got = 0;
    while (detail::readChunk(*child, false, buffer, sizeof(buffer), got)) {
        out.out.append(buffer, got);
    }
    if (errThread.joinable()) errThread.join();
    out.err = std::move(err);
    detail::waitChild(*child);
    out.exitCode = detail::childExitCode(*child);
    detail::closeChild(*child);
    loop.postFromThread([result, out = std::move(out), &loop]() mutable {
        result.fulfill(std::move(out));
        loop.releaseExternal();
    });
}

}  // namespace

void initArgv(int argc, char** argvPtr) {
    std::vector<std::string>& store = argvStore();
    store.clear();
    for (int i = 0; i < argc; ++i) {
        store.push_back(argvPtr[i] != nullptr ? argvPtr[i] : std::string());
    }
}

Array<std::string> argv() {
    const std::vector<std::string>& store = argvStore();
    if (store.empty()) return makeArray(std::vector<std::string>{"cp3"});
    return makeArray(store);
}

namespace {

// Monotonic origin for `process.uptime()` and `hrtime()`. uv_hrtime() is
// nanoseconds from an arbitrary origin with an unchanged frequency, so a
// delta against this stamp is the process lifetime.
const uint64_t kProcessStartNs = uv_hrtime();

}  // namespace

std::string cwd() {
    std::vector<char> buffer(4096);
    size_t size = buffer.size();
    int rc = uv_cwd(buffer.data(), &size);
    if (rc == UV_ENOBUFS) {
        buffer.assign(size + 1, '\0');
        size = buffer.size();
        rc = uv_cwd(buffer.data(), &size);
    }
    if (rc != 0) {
        throw Thrown::make(std::string("process.cwd: ") + uv_strerror(rc));
    }
    return std::string(buffer.data(), size);
}

void chdir(const std::string& path) {
    const int rc = uv_chdir(path.c_str());
    if (rc != 0) {
        throw Thrown::make(std::string("process.chdir: ") + uv_strerror(rc));
    }
}

std::int32_t pid() { return static_cast<std::int32_t>(uv_os_getpid()); }
std::int32_t ppid() { return static_cast<std::int32_t>(uv_os_getppid()); }

double uptime() {
    const uint64_t now = uv_hrtime();
    const uint64_t delta = now >= kProcessStartNs ? now - kProcessStartNs : 0;
    return static_cast<double>(delta) / 1e9;
}

MemoryUsage memoryUsage() {
    MemoryUsage report;
    size_t rss = 0;
    if (uv_resident_set_memory(&rss) == 0) {
        report.rss = static_cast<double>(rss);
    }
    return report;
}

std::tuple<double, double> hrtime() {
    const uint64_t ns = uv_hrtime();
    return {static_cast<double>(ns / 1000000000ULL),
            static_cast<double>(ns % 1000000000ULL)};
}

SpawnHandle::~SpawnHandle() = default;

void SpawnHandle::addChunkListener(const std::string& event,
                                   std::function<void(std::string)> listener) {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (event == "stderr") {
        state_->error.push_back(std::move(listener));
    } else {
        state_->data.push_back(std::move(listener));
    }
}

void SpawnHandle::addExitListener(std::function<void(std::int32_t)> listener) {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->exits.push_back(std::move(listener));
}

void SpawnHandle::write(const std::string& input) {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed) return;
    detail::writeChild(state_->child, input);
}

void SpawnHandle::kill() {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed) return;
    detail::killChild(state_->child);
}

// -----------------------------------------------------------------------------
// spawn: the public entry point (it was accidentally dropped in 412e3c98)
// -----------------------------------------------------------------------------
SpawnHandle spawn(const std::string& cmd, const std::vector<std::string>& args) {
    return SpawnHandle(startSpawn(cmd, args));
}

// -----------------------------------------------------------------------------
// stdout / stderr streams
// -----------------------------------------------------------------------------
// Each surface gets its own stream object; the stream's constructor chooses
// the target C++ stream, so `stdout.write` and `stderr.write` reach the right
// place with no `this`-pointer tricks.
namespace {
    class Stream : public process::StdioStream {
    public:
        explicit Stream(std::ostream* out) : out_(out) {}
        void write(const std::string& text) override {
            if (out_) {
                *out_ << text;
                out_->flush();
            }
        }
    private:
        std::ostream* out_;
    };
    Stream& kStdout() { static Stream s(&std::cout); return s; }
    Stream& kStderr() { static Stream s(&std::cerr); return s; }
}  // namespace

process::StdioStream& processStdout() { return kStdout(); }
process::StdioStream& processStderr() { return kStderr(); }

}  // namespace cp3::runtime::process

namespace cp3::runtime::process {

Promise<ExecResult> exec(const std::string& cmd,
                         const std::vector<std::string>& args) {
    auto child = std::make_shared<detail::ChildProcess>();
    std::string error;
    if (!detail::launchChild(cmd, args, *child, error)) {
        return Promise<ExecResult>::reject(
            Thrown::make(std::string("process.exec: " + error)));
    }
    // `exec` never feeds stdin; close the parent's end so a child that reads
    // stdin sees EOF instead of waiting forever.
    detail::closeChildStdin(*child);
    EventLoop& loop = currentLoop();
    Promise<ExecResult> result = Promise<ExecResult>::pending();
    loop.retainExternal();
    try {
        std::thread(runExec, result, std::ref(loop), child).detach();
    } catch (const std::system_error& ex) {
        detail::killChild(*child);
        detail::closeChild(*child);
        loop.releaseExternal();
        return Promise<ExecResult>::reject(
            Thrown::make(std::string("process.exec: ") + ex.what()));
    }
    return result;
}

}  // namespace cp3::runtime::process

// =============================================================================
// Lifecycle + stdin (ledger L, appended; see cp3/std/process.hpp).
// =============================================================================
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <deque>

namespace cp3::runtime::process {

namespace {

// ---------------------------------------------------------------------------
// Lifecycle registry. Registration happens on the caller (loop) thread; the
// signal path snapshots under the mutex and invokes on the loop thread, so no
// listener ever runs inside the OS signal handler itself.
// ---------------------------------------------------------------------------
struct LifecycleState {
    std::mutex mutex;
    std::vector<ExitListener> exit;
    std::vector<std::pair<std::string, SignalListener>> signals;
    std::vector<RejectionListener> rejections;
    std::atomic<bool> hooksRan{false};
    std::atomic<bool> exitArmed{false};
    std::atomic<bool> signalsInstalled{false};
    std::atomic<bool> handlingSignal{false};
};

LifecycleState& lifecycle() {
    // Leaked on purpose: static-destructor order must never matter - the
    // atexit handler can run after every other static is gone.
    static LifecycleState* state = new LifecycleState();
    return *state;
}

std::vector<ExitListener> snapshotExit() {
    std::lock_guard<std::mutex> lock(lifecycle().mutex);
    return lifecycle().exit;
}

void armExitHook() {
    LifecycleState& st = lifecycle();
    bool expected = false;
    if (st.exitArmed.compare_exchange_strong(expected, true)) {
        std::atexit([]() { runExitHooks(exitCode()); });
    }
}

void onOsSignal(int sig) {
    const char* name = sig == SIGINT ? "SIGINT" : "SIGTERM";
    const std::int32_t code = sig == SIGINT ? 130 : 143;
    // Async-signal-safety: only atomic stores + the loop's thread-safe post
    // (mutex + condition variable). No allocation, no listener invocation.
    LifecycleState& st = lifecycle();
    bool expected = false;
    if (!st.handlingSignal.compare_exchange_strong(expected, true)) return;
    // Capture state by reference: the registry is process-lifetime.
    try {
        currentLoop().postFromThread([name, code]() {
            handleSignal(name, code);
        });
    } catch (...) {
        st.handlingSignal.store(false);
    }
}

}  // namespace

void addExitListener(ExitListener listener) {
    armExitHook();
    std::lock_guard<std::mutex> lock(lifecycle().mutex);
    lifecycle().exit.push_back(std::move(listener));
}

void addShutdownHook(ExitListener listener) { addExitListener(std::move(listener)); }

void addSignalListener(const std::string& signal, SignalListener listener) {
    installSignalHandlers();
    armExitHook();
    std::lock_guard<std::mutex> lock(lifecycle().mutex);
    lifecycle().signals.emplace_back(signal, std::move(listener));
}

void addRejectionListener(RejectionListener listener) {
    std::lock_guard<std::mutex> lock(lifecycle().mutex);
    lifecycle().rejections.push_back(std::move(listener));
}

bool exitHooksRan() { return lifecycle().hooksRan.load(); }

void runExitHooks(std::int32_t code) {
    LifecycleState& st = lifecycle();
    bool expected = false;
    if (!st.hooksRan.compare_exchange_strong(expected, true)) return;
    for (auto& hook : snapshotExit()) {
        try {
            hook(code);
        } catch (...) {
            // A throwing hook must not stop the remaining hooks or the exit.
        }
    }
}

void installSignalHandlers() {
    LifecycleState& st = lifecycle();
    bool expected = false;
    if (!st.signalsInstalled.compare_exchange_strong(expected, true)) return;
    std::signal(SIGINT, onOsSignal);
    std::signal(SIGTERM, onOsSignal);
}

void handleSignal(const std::string& signal, std::int32_t code) {
    LifecycleState& st = lifecycle();
    std::vector<SignalListener> handlers;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        for (auto& entry : st.signals) {
            if (entry.first == signal) handlers.push_back(entry.second);
        }
    }
    for (auto& handler : handlers) {
        try {
            handler(signal);
        } catch (...) {
        }
    }
    // Graceful exit: exit hooks run, then the process ends with 128+signum.
    runExitHooks(code);
    st.handlingSignal.store(false);
    exit(code);
}

bool emitUnhandledRejection(const std::string& reason) {
    std::vector<RejectionListener> handlers;
    {
        std::lock_guard<std::mutex> lock(lifecycle().mutex);
        handlers = lifecycle().rejections;
    }
    if (handlers.empty()) return false;
    for (auto& handler : handlers) {
        try {
            handler(reason);
        } catch (...) {
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// stdin pump. One reader thread block-reads fd 0 (std::fread, portable on
// pipes and terminals) and posts drain tasks; all consumer state lives under
// the mutex and delivery happens on the loop thread.
// ---------------------------------------------------------------------------
namespace {

struct StdinState {
    std::mutex mutex;
    EventLoop* loop = nullptr;
    bool started = false;
    bool retained = false;
    bool eof = false;
    bool closed = false;
    std::string raw;                       // every byte read, for readAll
    std::deque<std::string> lines;         // complete lines not yet consumed
    std::string partial;                   // bytes after the last '\n'
    bool hasPartial = false;               // partial holds a final unterminated line
    std::deque<Promise<std::string>> pendingLines;
    std::deque<Promise<std::string>> pendingAll;
    std::deque<Promise<Array<std::string>>> pendingLinesAll;
    std::vector<std::function<void(std::string)>> lineListeners;
    std::vector<std::function<void(std::string)>> closeListeners;
};

StdinState& stdinState() {
    static StdinState* state = new StdinState();
    return *state;
}

void drainStdin() {
    StdinState& st = stdinState();
    std::vector<std::string> newLines;
    std::vector<Promise<std::string>> linePromises;
    std::vector<Promise<std::string>> allPromises;
    std::vector<Promise<Array<std::string>>> linesAllPromises;
    std::vector<std::function<void(std::string)>> lineListeners;
    std::vector<std::function<void(std::string)>> closeListeners;
    std::string allText;
    std::vector<std::string> allLines;
    bool eof = false;
    bool release = false;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        newLines.assign(st.lines.begin(), st.lines.end());
        st.lines.clear();
        eof = st.eof;
        lineListeners = st.lineListeners;
        while (!st.pendingLines.empty() && !newLines.empty()) {
            linePromises.push_back(std::move(st.pendingLines.front()));
            st.pendingLines.pop_front();
        }
        if (eof) {
            allText = st.raw;
            std::string cur;
            for (char ch : st.raw) {
                if (ch == '\n') {
                    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                    allLines.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(ch);
                }
            }
            if (!cur.empty()) {
                if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                allLines.push_back(cur);
            }
            while (!st.pendingAll.empty()) {
                allPromises.push_back(std::move(st.pendingAll.front()));
                st.pendingAll.pop_front();
            }
            while (!st.pendingLinesAll.empty()) {
                linesAllPromises.push_back(std::move(st.pendingLinesAll.front()));
                st.pendingLinesAll.pop_front();
            }
            while (!st.pendingLines.empty()) {
                linePromises.push_back(std::move(st.pendingLines.front()));
                st.pendingLines.pop_front();
            }
            closeListeners = st.closeListeners;
            if (st.retained) {
                st.retained = false;
                release = true;
            }
        }
    }
    // Pending readLine promises consume the drained lines in order.
    std::size_t consumed = 0;
    for (auto& promise : linePromises) {
        if (consumed < newLines.size()) {
            promise.fulfill(newLines[consumed++]);
        } else {
            promise.fulfill(std::string());
        }
    }
    // Unconsumed lines stay queued for later readLine calls.
    if (consumed < newLines.size()) {
        std::lock_guard<std::mutex> lock(st.mutex);
        for (std::size_t i = consumed; i < newLines.size(); ++i) {
            st.lines.push_back(newLines[i]);
        }
    }
    for (auto& promise : allPromises) promise.fulfill(allText);
    for (auto& promise : linesAllPromises) {
        promise.fulfill(makeArray<std::string>(allLines));
    }
    // Line listeners are an observation tap over every drained line.
    for (auto& line : newLines) {
        for (auto& listener : lineListeners) {
            try { listener(line); } catch (...) {}
        }
    }
    if (eof) {
        for (auto& listener : closeListeners) {
            try { listener(std::string()); } catch (...) {}
        }
        if (release && st.loop != nullptr) st.loop->releaseExternal();
    }
}

void stdinReaderMain() {
    StdinState& st = stdinState();
    char buffer[4096];
    for (;;) {
        std::size_t got = std::fread(buffer, 1, sizeof(buffer), stdin);
        bool done = got == 0;
        {
            std::lock_guard<std::mutex> lock(st.mutex);
            if (got > 0) {
                st.raw.append(buffer, got);
                st.partial.append(buffer, got);
                std::string::size_type pos = 0;
                for (;;) {
                    std::string::size_type nl = st.partial.find('\n', pos);
                    if (nl == std::string::npos) break;
                    std::string line = st.partial.substr(pos, nl - pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    st.lines.push_back(line);
                    pos = nl + 1;
                }
                st.partial.erase(0, pos);
                st.hasPartial = !st.partial.empty();
            }
            if (done) {
                st.eof = true;
                if (st.hasPartial) {
                    std::string tail = st.partial;
                    if (!tail.empty() && tail.back() == '\r') tail.pop_back();
                    st.lines.push_back(tail);
                    st.partial.clear();
                    st.hasPartial = false;
                }
            }
        }
        if (st.loop != nullptr) {
            st.loop->postFromThread([]() { drainStdin(); });
        }
        if (done) return;
    }
}

void startStdinReader() {
    StdinState& st = stdinState();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.started) return;
    st.started = true;
    st.loop = &currentLoop();
    st.loop->retainExternal();
    st.retained = true;
    try {
        std::thread(stdinReaderMain).detach();
    } catch (...) {
        st.started = false;
        st.retained = false;
        st.loop->releaseExternal();
        st.eof = true;
        st.loop->postFromThread([]() { drainStdin(); });
    }
}

}  // namespace

Promise<std::string> stdinReadAll() {
    startStdinReader();
    StdinState& st = stdinState();
    Promise<std::string> promise = Promise<std::string>::pending();
    bool done = false;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        if (st.eof) {
            done = true;
            text = st.raw;
        } else {
            st.pendingAll.push_back(promise);
        }
    }
    if (done) promise.fulfill(text);
    return promise;
}

Promise<Array<std::string>> stdinReadLines() {
    startStdinReader();
    StdinState& st = stdinState();
    Promise<Array<std::string>> promise = Promise<Array<std::string>>::pending();
    bool done = false;
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        if (st.eof) {
            done = true;
            std::string cur;
            for (char ch : st.raw) {
                if (ch == '\n') {
                    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                    lines.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(ch);
                }
            }
            if (!cur.empty()) {
                if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                lines.push_back(cur);
            }
        } else {
            st.pendingLinesAll.push_back(promise);
        }
    }
    if (done) promise.fulfill(makeArray<std::string>(lines));
    return promise;
}

Promise<std::string> stdinReadLine() {
    startStdinReader();
    StdinState& st = stdinState();
    Promise<std::string> promise = Promise<std::string>::pending();
    bool done = false;
    std::string line;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        if (!st.lines.empty()) {
            done = true;
            line = st.lines.front();
            st.lines.pop_front();
        } else if (st.eof) {
            done = true;
        } else {
            st.pendingLines.push_back(promise);
        }
    }
    if (done) promise.fulfill(line);
    return promise;
}

void stdinAddLineListener(std::function<void(std::string)> listener) {
    startStdinReader();
    std::lock_guard<std::mutex> lock(stdinState().mutex);
    stdinState().lineListeners.push_back(std::move(listener));
}

void stdinAddCloseListener(std::function<void(std::string)> listener) {
    startStdinReader();
    std::lock_guard<std::mutex> lock(stdinState().mutex);
    stdinState().closeListeners.push_back(std::move(listener));
}

void stdinClose() {
    std::lock_guard<std::mutex> lock(stdinState().mutex);
    stdinState().closed = true;
}

void StdinStream::addLineListener(std::function<void(std::string)> listener) {
    stdinAddLineListener(std::move(listener));
}

void StdinStream::addCloseListener(std::function<void(std::string)> listener) {
    stdinAddCloseListener(std::move(listener));
}

StdinStream& processStdin() {
    static StdinStream stream;
    return stream;
}

}  // namespace cp3::runtime::process
