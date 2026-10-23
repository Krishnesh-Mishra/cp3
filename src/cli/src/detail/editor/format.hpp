// =============================================================================
// src/cli/src/detail/format.hpp - the conservative formatter core (DECLARATIONS).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Extracted from commands/fmt.cpp so `cp3c fmt` and the LSP
// documentFormatting provider run the identical code path: the formatting
// result must be byte-identical to what `cp3c fmt` writes.
// =============================================================================
#pragma once

#include <string>

namespace cp3::cli {

// Normalizes line endings, strips trailing whitespace on every line, re-indents
// code lines by brace depth (2 spaces per level), drops trailing blank lines
// and ends with exactly one newline. Interiors of multi-line comments and
// template literals are left byte-for-byte untouched and no non-whitespace byte
// is ever changed.
//
// The caller owns the "may I rewrite this document" question: `cmdFmt` refuses
// on lexical errors before calling this, and the LSP provider mirrors that.
std::string formatDocumentText(const std::string& input);

}  // namespace cp3::cli
