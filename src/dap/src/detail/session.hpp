// =============================================================================
// src/dap/src/detail/session.hpp - one gdb session driving the debuggee.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**). PRIVATE header.
//
// GdbSession owns `gdb -q -i=mi2` (async mode) plus the program it runs, and is
// strictly single-threaded: the owner calls `pump()` regularly - also while the
// target is running - then drains `takeEvents()`. Every user-visible answer
// (frames, variables, breakpoints) is .cp3-native because the Debug build
// carries `#line` mapping into DWARF (src/codegen/src/support/line_map.cpp).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "detail/mi.hpp"
#include "detail/process.hpp"

namespace cp3::dap {

// One requested breakpoint (1-based line inside a source file).
struct BreakpointRequest {
    std::uint32_t line = 0;
};

// What gdb made of one requested breakpoint.
struct BreakpointState {
    bool verified = false;
    int gdbNumber = 0;      // gdb's breakpoint number when verified
    std::uint32_t line = 0; // the line gdb resolved to (0 = unresolved)
    std::string message;    // why it is unverified, when known
};

// One stack frame of the stopped thread, as gdb sees it.
struct FrameInfo {
    int level = 0;
    std::string function;
    std::string path;  // the .cp3 file (empty for runtime/system frames)
    std::uint32_t line = 0;
};

// One variable: a scope entry or a varobj child.
struct VariableInfo {
    std::string name;
    std::string value;
    std::string type;
    bool expandable = false;  // has children worth requesting
    int reference = 0;        // DAP variablesReference when expandable
};

enum class StopKind { Breakpoint, Step, Pause, Exception, Entry, Unknown };

// One asynchronous event for the DAP server to forward.
struct SessionEvent {
    enum class Kind { Output, Stopped, Continued, Exited, Terminated };
    Kind kind = Kind::Output;
    std::string category;  // Output: "stdout" (target) or "console" (gdb)
    std::string text;      // Output
    StopKind stop = StopKind::Unknown;  // Stopped
    int threadId = 1;                   // Stopped / Continued
    std::vector<int> breakpoints;       // Stopped(Breakpoint): gdb numbers
    int exitCode = 0;                   // Exited
};

class GdbSession {
public:
    GdbSession() = default;
    ~GdbSession();
    GdbSession(const GdbSession&) = delete;
    GdbSession& operator=(const GdbSession&) = delete;

    // Finds gdb on PATH and loads `exePath`. "" on success, else a message the
    // client can show (including the no-gdb-on-PATH case).
    std::string launch(const std::string& exePath, const std::string& cwd);

    bool running() const noexcept { return running_; }
    bool stopped() const noexcept { return stopped_; }
    bool finished() const noexcept { return finished_; }
    bool active() const { return launched_ && !finished_; }
    int mainThread() const noexcept { return mainThread_; }
    int gdbPid() const noexcept { return gdb_.pid(); }

    // Replaces the breakpoints previously set for `path` and reports each one.
    std::string setBreakpoints(const std::string& path,
                               const std::vector<BreakpointRequest>& requests,
                               std::vector<BreakpointState>& out);

    std::string start();                            // configurationDone: run
    std::string resume(const std::string& action);  // continue/next/stepIn/stepOut
    std::string pause();                            // interrupt a running target
    void terminate();  // kill gdb and the debuggee; idempotent

    std::string stackTrace(std::vector<FrameInfo>& out);
    // Scope/child handles for the DAP `scopes` request.
    int createScope(int frame, bool arguments);
    std::string variables(int reference, std::vector<VariableInfo>& out);

    void pump();  // collect gdb output; never blocks
    void takeEvents(std::vector<SessionEvent>& out);

private:
    struct Handle {
        enum class Kind { Locals, Arguments, Variable };
        Kind kind = Kind::Locals;
        int frame = 0;
        std::string expr;    // Variable: expression to create a varobj for
        std::string varobj;  // Variable: gdb's varobj name once created
    };

    struct MiResult {
        std::string cls;    // "done", "running", "error", ...
        std::string body;   // raw text after the class
        std::string error;  // transport/timeout error or the ^error message
        bool ok() const { return error.empty() && cls != "error"; }
    };

    MiResult command(const std::string& mi);
    bool send(const std::string& line);
    void servicePendingSelect();
    void drain();
    void handleLine(const std::string& line);
    void handleResult(const std::string& line);
    void handleAsync(const std::string& record, const std::string& body);
    void handleStream(const std::string& line);
    void handleStop(const MiValue& results);
    void noteExit(int exitCode);
    void pushEvent(SessionEvent event);
    std::string selectFrame(int frame);

    std::string listScope(int frame, bool arguments,
                          std::vector<VariableInfo>& out);
    std::string childrenFor(int reference, std::vector<VariableInfo>& out);
    void addVariable(const std::string& name, const std::string& value,
                     const std::string& type, int frame,
                     std::vector<VariableInfo>& out);
    int intern(Handle handle);

    ChildProcess gdb_;
    std::string buffer_;
    std::vector<SessionEvent> events_;
    std::unordered_map<std::string, std::vector<int>> breakpoints_;
    std::unordered_map<int, Handle> handles_;
    std::string resultCls_;
    std::string resultBody_;
    bool haveResult_ = false;

    int mainThread_ = 1;
    int nextReference_ = 1;
    int selectedFrame_ = -1;
    int stopGeneration_ = 0;  // bumps on every stop/exit record
    bool launched_ = false;
    bool running_ = false;
    bool stopped_ = false;
    bool finished_ = false;
    bool sawThread_ = false;
    bool interruptRequested_ = false;
    bool selectMainRequested_ = false;
    bool emitContinued_ = false;
};

}  // namespace cp3::dap
