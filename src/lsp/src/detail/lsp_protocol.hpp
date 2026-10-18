// =============================================================================
// src/lsp/src/detail/lsp_protocol.hpp - JSON-RPC message construction.
//
// OWNERSHIP: LSP workstream (src/lsp/**). PRIVATE header.
//
// Keeps server.cpp about state and dispatch: everything that knows the exact
// LSP wire shape (capabilities object, diagnostics object, envelopes) is built
// here. Positions are converted from compiler coordinates in this unit.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/lsp.hpp"

#include "detail/lsp_json.hpp"

namespace cp3::lsp {

// The `initialize` result: capabilities { textDocumentSync: 1 } (full sync)
// plus serverInfo { name, version } (PKG-CONTRACTS sec.2).
Json initializeResult();

// Params of textDocument/publishDiagnostics. `text` is the checked document's
// current content, used to convert compiler byte columns to UTF-16.
Json publishDiagnosticsParams(const std::string& uri,
                              const std::vector<RawDiagnostic>& diagnostics,
                              const std::string& text);

// {"jsonrpc":"2.0","id":<id>,"result":<result>}
Json responseMessage(const Json& id, Json result);

// {"jsonrpc":"2.0","id":<id>,"error":{"code":<code>,"message":<message>}}
Json errorMessage(const Json& id, int code, std::string message);

}  // namespace cp3::lsp
