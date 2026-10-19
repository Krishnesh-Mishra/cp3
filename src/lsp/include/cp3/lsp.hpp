// =============================================================================
// src/lsp/include/cp3/lsp.hpp - the `cp3c lsp` server boundary (LSP MVP).
//
// OWNERSHIP: LSP workstream (src/lsp/**, PKG-CONTRACTS sec.2 and sec.5).
//
// The language server is a subcommand of cp3c.exe (never a second binary) and
// speaks LSP 3.17 over stdio with Content-Length framing, UTF-8. This header
// is the module's public boundary: the CLI supplies a transport-neutral file
// checker; the server owns framing, JSON-RPC, document state and diagnostics
// publishing.
//
// Protocol surface implemented this round (PKG-CONTRACTS sec.2):
//   initialize / initialized, shutdown / exit,
//   textDocument/{didOpen,didChange,didSave,didClose},
//   textDocument/publishDiagnostics.
// Explicitly out of scope: hover, completion, goto, formatting, rename.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cp3::lsp {

// One compiler diagnostic in a transport-neutral shape.
//
// Positions follow the compiler's own convention (cp3/source.hpp): 1-based
// lines and 1-based BYTE columns. The server converts them into 0-based
// UTF-16 LSP positions when it builds `publishDiagnostics`.
struct RawDiagnostic {
    std::string code;              // stable compiler code ("CP3401"); may be empty
    int severity = 1;              // LSP severity: 1 error, 2 warning, 3 information
    std::uint32_t line = 1;        // 1-based
    std::uint32_t column = 1;      // 1-based, in bytes
    std::uint32_t endLine = 0;     // 1-based; 0 = unknown (advance one column)
    std::uint32_t endColumn = 0;   // 1-based, in bytes; 0 = unknown
    std::string message;           // compiler message text
};

// Compiles `text` as the file at `path` and returns the diagnostics that
// belong to that document.
//
// Called on the server loop thread for every didOpen/didChange/didSave. It
// must not write to stdout, should not throw, and must be fast enough for the
// interactive path: the MVP runs the in-process frontend, which satisfies all
// three. Diagnostics located in other files (imports, injected preludes) are
// not part of the MVP surface and are dropped by the implementation.
using CheckFn = std::function<std::vector<RawDiagnostic>(const std::string& path,
                                                         const std::string& text)>;

// Runs the LSP server over stdin/stdout until the client sends `exit`.
//
// Returns the process exit code: 0 after shutdown + exit, 1 for `exit`
// without a prior `shutdown` (LSP 3.17 "exit"), and 2 when the stdio transport
// fails (a malformed header block, a frame over the size cap, or a client
// stream that closed mid-frame - the stream can no longer be trusted, so the
// server stops instead of buffering blindly).
int runStdioServer(const CheckFn& check);

}  // namespace cp3::lsp
