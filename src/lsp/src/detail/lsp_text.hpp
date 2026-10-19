// =============================================================================
// src/lsp/src/detail/lsp_text.hpp - URIs, lines, and UTF-16 positions.
//
// OWNERSHIP: LSP workstream (src/lsp/**). PRIVATE header.
//
// The compiler speaks 1-based lines and 1-based BYTE columns (cp3/source.hpp);
// LSP speaks 0-based lines and 0-based UTF-16 code units. The conversions here
// are the single place that gap is closed. For pure-ASCII lines the two
// coincide; non-ASCII is handled by decoding the line prefix as UTF-8 and
// counting UTF-16 units (invalid bytes count as one unit each) - best effort,
// documented in the report.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cp3::lsp {

struct LspPosition {
    std::uint32_t line = 0;       // 0-based
    std::uint32_t character = 0;  // 0-based, UTF-16 code units
};

// Decodes a `file:` URI into a filesystem path: percent escapes decoded, an
// optional authority kept as a leading "//" (UNC), and the leading slash of a
// Windows drive path dropped ("file:///c:/x" -> "c:/x"). Any other scheme
// fails: the MVP cannot check buffers that do not map to a file.
bool uriToPath(const std::string& uri, std::string& path);

// Encodes a filesystem path as a `file:` URI (backslashes become '/', reserved
// bytes are percent-encoded, a drive letter keeps its colon). Used to answer
// definition/workspaceSymbol locations.
std::string pathToUri(const std::string& path);

// The text of a 1-based line without its EOL, or an empty view when the
// document has no such line.
std::string_view lineOf(const std::string& text, std::uint32_t line);

// Converts one compiler position into an LSP position, clamped into the
// document (a line past the end clamps to the last line; a column past the
// line end clamps to the line end).
LspPosition toLspPosition(const std::string& text, std::uint32_t line,
                          std::uint32_t column);

// `start` advanced by one character within its line (used when a diagnostic
// carries only a start position); stays put at the end of the line.
LspPosition advanceOne(const std::string& text, LspPosition start);

// The position one past the last character of the document (the end of the
// final line), used by whole-document edits.
LspPosition endPosition(const std::string& text);

// The reverse direction: the 1-based BYTE column in `text` for an LSP position
// (0-based line, 0-based UTF-16 character), clamped into the document. A
// character that falls inside a multi-unit code point resolves to that code
// point's first byte, never into the middle of one.
std::uint32_t toByteColumn(const std::string& text, std::uint32_t line,
                           std::uint32_t character);

}  // namespace cp3::lsp
