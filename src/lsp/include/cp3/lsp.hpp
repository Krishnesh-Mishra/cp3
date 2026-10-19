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
// The intel round adds the query requests behind IntelHooks: completion, hover,
// definition, documentSymbol, workspaceSymbol, signatureHelp, formatting.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
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

// One completion candidate, transport-neutral.
struct CompletionCandidate {
    std::string label;         // insert text and filter text
    int kind = 1;              // LSP CompletionItemKind (3 function, 5 field, ...)
    std::string detail;        // type or signature line; may be empty
    std::string insertText;    // when non-empty and different from `label`
    int insertTextFormat = 1;  // 1 plain text, 2 snippet
};

// A hover answer: plain text lines (types, signatures). The server wraps them
// into a markdown code block.
struct HoverAnswer {
    std::string contents;
};

// A jump target in compiler coordinates (1-based line, 1-based BYTE column).
// Positions are relative to `path`, which may differ from the queried file
// (definition reaches local imports).
struct SourceTarget {
    std::string path;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::uint32_t endLine = 1;
    std::uint32_t endColumn = 1;
};

// One document-symbol (outline) entry in compiler coordinates.
struct SymbolEntry {
    std::string name;
    int kind = 13;  // LSP SymbolKind (12 function, 5 class, 13 variable, ...)
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::uint32_t endLine = 1;
    std::uint32_t endColumn = 1;
};

// The active call's signature at the cursor.
struct SignatureAnswer {
    std::string label;                      // full signature line
    std::vector<std::string> parameters;    // one entry per parameter
    int activeParameter = 0;                // 0-based index
};

// Transport-neutral intelligence queries. The CLI owns the frontend and the
// semantic engine; the server owns positions, documents and JSON shapes.
//
// Positions in and out use the compiler convention (1-based line, 1-based
// BYTE column); the server converts UTF-16 <-> bytes at the boundary. Every
// hook must be total: it returns empty answers for unknown/broken documents
// and never writes to stdout or exits. `readFile` reads a file from disk so
// the server can convert positions in a definition target it has not opened.
struct IntelHooks {
    std::function<std::vector<CompletionCandidate>(
        const std::string& path, const std::string& text, std::uint32_t line,
        std::uint32_t column)> completion;
    std::function<std::optional<HoverAnswer>(
        const std::string& path, const std::string& text, std::uint32_t line,
        std::uint32_t column)> hover;
    std::function<std::vector<SourceTarget>(
        const std::string& path, const std::string& text, std::uint32_t line,
        std::uint32_t column)> definition;
    std::function<std::vector<SymbolEntry>(const std::string& path,
                                           const std::string& text)>
        documentSymbols;
    std::function<std::optional<SignatureAnswer>(
        const std::string& path, const std::string& text, std::uint32_t line,
        std::uint32_t column)> signatureHelp;
    std::function<std::optional<std::string>(const std::string& path,
                                             const std::string& text)>
        format;
    std::function<std::optional<std::string>(const std::string& path)> readFile;
};

// Runs the LSP server over stdin/stdout until the client sends `exit`.
//
// Returns the process exit code: 0 after shutdown + exit, 1 for `exit`
// without a prior `shutdown` (LSP 3.17 "exit"), and 2 when the stdio transport
// fails (a malformed header block, a frame over the size cap, or a client
// stream that closed mid-frame - the stream can no longer be trusted, so the
// server stops instead of buffering blindly).
int runStdioServer(const CheckFn& check);

// The intel round: the same server plus the query providers. A hook that is
// null answers empty (the client's capability advertisement omits the
// provider, so this is a safety net, not a mode).
int runStdioServer(const CheckFn& check, const IntelHooks& intel);

}  // namespace cp3::lsp
