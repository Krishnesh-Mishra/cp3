// =============================================================================
// src/dap/include/cp3/dap.hpp - the `cp3c dap` server boundary (DAP MVP).
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// The debug adapter is a subcommand of cp3c.exe (like `cp3c lsp`, never a
// second binary) and speaks the Debug Adapter Protocol over stdio with
// Content-Length framing, UTF-8. This header is the module's public boundary:
// the CLI supplies a transport-neutral build callback (the in-process pipeline
// with the Debug profile); the server owns framing, the session state machine,
// gdb (driven through its MI2 interpreter) and the DAP request/response/event
// surface.
//
// Protocol surface implemented this round:
//   initialize, launch, setBreakpoints, configurationDone,
//   continue, next, stepIn, stepOut, pause,
//   threads, stackTrace, scopes, variables,
//   disconnect, terminate.
// The debugee runs locally (no ports, no network). gdb is taken from PATH and
// is deliberately not bundled; a missing gdb fails `launch` with a message
// that says exactly that.
// =============================================================================
#pragma once

#include <functional>
#include <string>

namespace cp3::dap {

// One build attempt by the CLI glue: compiles `program` (a .cp3 path) with the
// Debug profile and returns the executable to debug. `log` carries the
// rendered compiler output either way (the server forwards it as `output`
// events).
struct BuildOutcome {
    bool built = false;
    std::string exePath;  // the produced executable, when `built`
    std::string log;      // compiler diagnostics / driver messages
};

// Compiles a .cp3 program for debugging. Must not throw; failures are reported
// through `BuildOutcome`.
using BuildFn = std::function<BuildOutcome(const std::string& program,
                                           const std::string& cwd)>;

// Runs the DAP server over stdin/stdout until the client disconnects or the
// session terminates. Returns the process exit code: 0 for a clean session,
// 2 when the stdio transport itself failed (a malformed frame or a client
// stream that closed mid-frame - the stream can no longer be trusted).
int runStdioServer(const BuildFn& build);

}  // namespace cp3::dap
