// =============================================================================
// src/lsp/src/detail/lsp_intel.hpp - query providers (wire shape) declarations.
//
// OWNERSHIP: LSP workstream (src/lsp/**). PRIVATE header.
//
// The server owns documents, positions and JSON shapes; the CLI supplies the
// semantic answers through IntelHooks (cp3/lsp.hpp). This unit is the middle
// layer: it converts request positions (0-based line, UTF-16 character) into
// the compiler's byte coordinates, calls the hooks, and packages the answers
// as the exact LSP result objects. Every function is total: an unknown
// document, a missing hook or a throwing hook answers empty, never an error.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/lsp.hpp"

#include "detail/lsp_json.hpp"

namespace cp3::lsp {

// One open document, borrowed from the server's store for the duration of a
// request. A document whose URI did not map to a file has an empty path.
struct DocumentView {
    const std::string* uri = nullptr;
    const std::string* path = nullptr;
    const std::string* text = nullptr;
};

// Completion result: { isIncomplete: false, items: [...] }. An unknown
// document answers an empty item list (a well-formed answer, not an error).
Json completionResult(const DocumentView& document, std::uint32_t line,
                      std::uint32_t character, const IntelHooks& intel);

// Hover result: { contents } or null when there is nothing to say.
Json hoverResult(const DocumentView& document, std::uint32_t line,
                 std::uint32_t character, const IntelHooks& intel);

// Definition result: Location[] (possibly empty; std imports answer empty
// instead of a wrong location).
Json definitionResult(const DocumentView& document, std::uint32_t line,
                      std::uint32_t character, const IntelHooks& intel);

// DocumentSymbol result: DocumentSymbol[].
Json documentSymbolResult(const DocumentView& document, const IntelHooks& intel);

// SignatureHelp result: { signatures, activeSignature, activeParameter } or
// null when the cursor is not inside an argument list.
Json signatureHelpResult(const DocumentView& document, std::uint32_t line,
                         std::uint32_t character, const IntelHooks& intel);

// Formatting result: a single whole-document TextEdit, or an empty list when
// the document is already formatted (the client then keeps its buffer).
Json formattingResult(const DocumentView& document, const IntelHooks& intel);

// WorkspaceSymbol result: SymbolInformation[] gathered from the open
// documents whose names contain `query` (case-insensitive; empty = all).
Json workspaceSymbolResult(const std::string& query,
                           const std::vector<DocumentView>& documents,
                           const IntelHooks& intel);

}  // namespace cp3::lsp
