// =============================================================================
// src/dap/src/session.cpp - GdbSession: lifecycle and the MI stream (detail/session.hpp).
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// The adapter drives gdb over its machine interface (`-i=mi2` with async mode
// on, so exec commands return immediately and stop events arrive while the
// target runs). This unit owns the process, the command/result discipline and
// the asynchronous records (*stopped, *running, streams, thread notifies); the
// queries live in session_queries.cpp.
//
// Event discipline: `command()` waits for exactly one `^` record, and every
// `*stopped` / stream / notify seen on the way is turned into a SessionEvent -
// nothing is dropped, but nothing is invented either.
// =============================================================================
#include "detail/session.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

#include "detail/mi.hpp"

namespace cp3::dap {
namespace {

constexpr int kCommandTimeoutMs = 15000;

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Splits "^done,name=..." into ("done", "name=...").
void splitRecord(const std::string& line, std::string& cls, std::string& body) {
    const std::size_t comma = line.find(',', 1);
    if (comma == std::string::npos) {
        cls = line.substr(1);
        body.clear();
        return;
    }
    cls = line.substr(1, comma - 1);
    body = line.substr(comma + 1);
}

// PATH search for gdb. No shell, no extra process: the first regular file wins.
std::string findGdb() {
    const char* pathValue = std::getenv("PATH");
    if (pathValue == nullptr) return {};
    const std::string path = pathValue;
#if defined(_WIN32)
    constexpr char kSeparator = ';';
    constexpr const char* kNames[] = {"gdb.exe", "gdb"};
#else
    constexpr char kSeparator = ':';
    constexpr const char* kNames[] = {"gdb"};
#endif
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(kSeparator, start);
        const std::string dir =
            path.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        std::error_code ec;
        if (!dir.empty()) {
            for (const char* name : kNames) {
                const std::filesystem::path candidate =
                    std::filesystem::path(dir) / name;
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    return candidate.string();
                }
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

}  // namespace

GdbSession::~GdbSession() { terminate(); }

std::string GdbSession::launch(const std::string& exePath,
                               const std::string& cwd) {
    terminate();
    const std::string gdbPath = findGdb();
    if (gdbPath.empty()) {
        return "gdb was not found on PATH. Install MinGW-w64 (WinLibs) or another "
               "build of gdb, put it on PATH, then start the session again.";
    }
    std::string spawnError;
    if (!gdb_.spawn(gdbPath, {"-q", "-i=mi2"}, cwd, spawnError)) {
        return "could not start gdb: " + spawnError;
    }
    launched_ = true;
    finished_ = false;
    running_ = false;
    stopped_ = false;
    haveResult_ = false;
    interruptRequested_ = false;
    selectMainRequested_ = false;
    emitContinued_ = false;
    sawThread_ = false;
    mainThread_ = 1;
    selectedFrame_ = -1;
    handles_.clear();
    breakpoints_.clear();
    buffer_.clear();
    events_.clear();
    const char* settings[] = {"-gdb-set pagination off", "-gdb-set confirm off",
                              "-gdb-set mi-async on"};
    for (const char* setting : settings) {
        const MiResult result = command(setting);
        if (!result.ok()) {
            return std::string("gdb rejected '") + setting + "': " + result.error;
        }
    }
    const MiResult loaded = command("-file-exec-and-symbols " + miQuote(exePath));
    if (!loaded.ok()) {
        return "gdb could not load '" + exePath + "': " + loaded.error;
    }
    return {};
}

bool GdbSession::send(const std::string& line) { return gdb_.writeLine(line); }

GdbSession::MiResult GdbSession::command(const std::string& mi) {
    MiResult out;
    haveResult_ = false;
    resultCls_.clear();
    resultBody_.clear();
    if (!send(mi)) {
        out.error = "gdb is not running";
        return out;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kCommandTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        drain();
        if (haveResult_) {
            out.cls = resultCls_;
            out.body = resultBody_;
            if (out.cls == "error") {
                out.error = "gdb reported an error";
                MiValue value;
                std::size_t pos = 0;
                if (parseMiBody(resultBody_, pos, value)) {
                    const std::string message = miText(value, "msg");
                    if (!message.empty()) out.error = message;
                }
            }
            servicePendingSelect();
            return out;
        }
        if (!gdb_.alive()) {
            out.error = "gdb exited";
            finished_ = true;
            return out;
        }
        sleepMs(2);
    }
    out.error = "gdb did not answer '" + mi + "'";
    return out;
}

void GdbSession::servicePendingSelect() {
    if (!selectMainRequested_ || finished_ || !launched_) return;
    selectMainRequested_ = false;
    // On Windows gdb stops a running process through an injected helper thread;
    // selecting the inferior's main thread puts the user program's frame back
    // in front of the client. The result is deliberately ignored.
    command("-thread-select " + std::to_string(mainThread_));
}

void GdbSession::pump() {
    drain();
    servicePendingSelect();
}

void GdbSession::drain() {
    bool closed = false;
    gdb_.readAvailable(buffer_, closed);
    std::size_t newline = 0;
    while ((newline = buffer_.find('\n')) != std::string::npos) {
        std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) handleLine(line);
    }
    if (buffer_.size() > 1024u * 1024u) buffer_.clear();  // bounded on garbage
    if (closed && launched_ && !finished_) {
        finished_ = true;
        running_ = false;
        stopped_ = false;
        SessionEvent terminated;
        terminated.kind = SessionEvent::Kind::Terminated;
        pushEvent(std::move(terminated));
    }
}

void GdbSession::handleLine(const std::string& line) {
    const char prefix = line[0];
    if (prefix == '^') {
        handleResult(line);
        return;
    }
    if (prefix == '*' || prefix == '=') {
        std::string record;
        std::string body;
        splitRecord(line, record, body);
        if (prefix == '*') {
            handleAsync(record, body);
        } else if (record == "thread-created") {
            MiValue value;
            std::size_t pos = 0;
            const std::string id = parseMiBody(body, pos, value)
                                       ? miText(value, "id")
                                       : std::string();
            if (!id.empty() && !sawThread_) {
                mainThread_ = std::atoi(id.c_str());
                sawThread_ = true;
            }
        }
        return;
    }
    if (prefix == '~' || prefix == '&' || prefix == '@') {
        handleStream(line);
        return;
    }
    if (line.compare(0, 5, "(gdb)") == 0) return;  // the MI prompt
    // Anything else is the debuggee speaking on the stream it shares with gdb.
    SessionEvent event;
    event.kind = SessionEvent::Kind::Output;
    event.category = "stdout";
    event.text = line + "\n";
    pushEvent(std::move(event));
}

void GdbSession::handleResult(const std::string& line) {
    if (haveResult_) return;  // a result nobody waits for; ignore it
    std::string cls;
    std::string body;
    splitRecord(line, cls, body);
    resultCls_ = std::move(cls);
    resultBody_ = std::move(body);
    haveResult_ = true;
}

void GdbSession::handleAsync(const std::string& record, const std::string& body) {
    MiValue value;
    std::size_t pos = 0;
    const bool parsed = parseMiBody(body, pos, value);
    if (record == "stopped") {
        handleStop(parsed ? value : MiValue{});
        return;
    }
    if (record == "running") {
        running_ = true;
        stopped_ = false;
        if (emitContinued_) {
            emitContinued_ = false;
            SessionEvent event;
            event.kind = SessionEvent::Kind::Continued;
            event.threadId = mainThread_;
            pushEvent(std::move(event));
        }
        return;
    }
    // thread-exited, breakpoint-modified, library-loaded, ... are informational.
}

void GdbSession::handleStop(const MiValue& results) {
    ++stopGeneration_;
    stopped_ = true;
    running_ = false;
    selectedFrame_ = -1;
    handles_.clear();
    const std::string reason = miText(results, "reason");
    if (reason == "exited-normally") {
        noteExit(0);
        return;
    }
    if (reason == "exited") {
        const std::string code = miText(results, "exit-code");
        // gdb prints the exit status in octal.
        noteExit(code.empty() ? 0 : static_cast<int>(std::strtol(code.c_str(), nullptr, 8)));
        return;
    }
    SessionEvent event;
    event.kind = SessionEvent::Kind::Stopped;
    event.threadId = mainThread_;
    if (reason == "breakpoint-hit") {
        event.stop = StopKind::Breakpoint;
        const std::string number = miText(results, "bkptno");
        if (!number.empty()) {
            event.breakpoints.push_back(std::atoi(number.c_str()));
        }
    } else if (reason == "end-stepping-range" || reason == "function-finished") {
        event.stop = StopKind::Step;
    } else if (reason == "signal-received") {
        if (interruptRequested_) {
            event.stop = StopKind::Pause;
            selectMainRequested_ = true;
        } else {
            event.stop = StopKind::Exception;
        }
    } else if (reason == "entry") {
        event.stop = StopKind::Entry;
    } else {
        event.stop = StopKind::Unknown;
    }
    interruptRequested_ = false;
    pushEvent(std::move(event));
}

void GdbSession::noteExit(int exitCode) {
    if (finished_) return;
    finished_ = true;
    running_ = false;
    stopped_ = false;
    SessionEvent exited;
    exited.kind = SessionEvent::Kind::Exited;
    exited.exitCode = exitCode;
    pushEvent(std::move(exited));
    SessionEvent terminated;
    terminated.kind = SessionEvent::Kind::Terminated;
    pushEvent(std::move(terminated));
    gdb_.killTree(1000);  // the session is over; gdb must not linger
}

void GdbSession::handleStream(const std::string& line) {
    MiValue value;
    std::size_t pos = 1;
    if (!parseMiValue(line, pos, value) ||
        value.kind != MiValue::Kind::Scalar) {
        return;
    }
    SessionEvent event;
    event.kind = SessionEvent::Kind::Output;
    event.category = line[0] == '@' ? "stdout" : "console";
    event.text = value.text;
    pushEvent(std::move(event));
}

void GdbSession::pushEvent(SessionEvent event) {
    events_.push_back(std::move(event));
}

void GdbSession::takeEvents(std::vector<SessionEvent>& out) {
    out.insert(out.end(), events_.begin(), events_.end());
    events_.clear();
}

std::string GdbSession::start() {
    emitContinued_ = false;  // the initial run is not a `continued` event
    sawThread_ = false;
    mainThread_ = 1;
    const int stopsBefore = stopGeneration_;
    const MiResult result = command("-exec-run");
    if (!result.ok()) return result.error;
    if (stopGeneration_ != stopsBefore || finished_) {
        // The target stopped (or exited) before gdb acknowledged the run;
        // that record already set the state and pushed its event.
        return {};
    }
    running_ = true;
    stopped_ = false;
    return {};
}

std::string GdbSession::resume(const std::string& action) {
    std::string mi = "-exec-continue";
    if (action == "next") {
        mi = "-exec-next";
    } else if (action == "stepIn") {
        mi = "-exec-step";
    } else if (action == "stepOut") {
        mi = "-exec-finish";
    }
    emitContinued_ = true;
    const int stopsBefore = stopGeneration_;
    const MiResult result = command(mi);
    if (!result.ok()) {
        emitContinued_ = false;
        return result.error;
    }
    if (stopGeneration_ != stopsBefore || finished_) {
        // A step can land before its ^running acknowledgement is consumed;
        // handleStop() has already recorded the stop.
        return {};
    }
    running_ = true;
    stopped_ = false;
    interruptRequested_ = false;
    return {};
}

std::string GdbSession::pause() {
    if (!running_) return "the program is not running";
    interruptRequested_ = true;
    const MiResult result = command("-exec-interrupt");
    if (!result.ok()) {
        interruptRequested_ = false;
        return result.error;
    }
    return {};
}

void GdbSession::terminate() {
    finished_ = true;
    running_ = false;
    stopped_ = false;
    if (gdb_.alive()) {
        send("-gdb-exit");  // polite; the kill below is the guarantee
        gdb_.killTree(2000);
    }
    gdb_.killTree(0);  // closes the handles when the child already exited
    launched_ = false;
}

}  // namespace cp3::dap
