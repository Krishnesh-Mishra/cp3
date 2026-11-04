// =============================================================================
// src/std/process/runtime.cpp - argv store + the exec/spawn orchestration.
//
// OWNERSHIP: the std/process family (src/std/process/**). Compiled as a
// runtime TU by the process native row whenever a program imports
// "std/process" (src/std/process/native.hpp). The inline surface (exit, the
// environment map) stays in cp3/std/process.hpp; this unit owns what needs
// threading and OS handles.
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
    std::vector<std::function<void(double)>> exits;
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

void deliverExit(SpawnState& state, double code) {
    std::vector<std::function<void(double)>> handlers;
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
    const double code = detail::childExitCode(state->child);
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

double pid() { return static_cast<double>(uv_os_getpid()); }
double ppid() { return static_cast<double>(uv_os_getppid()); }

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

void SpawnHandle::addExitListener(std::function<void(double)> listener) {
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

SpawnHandle spawn(const std::string& cmd, const std::vector<std::string>& args) {
    return SpawnHandle(startSpawn(cmd, args));
}

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
