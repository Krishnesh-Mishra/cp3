// =============================================================================
// src/lexer/src/detail/keywords.h - PRIVATE keyword-table declarations.
//
// The identifier->keyword classification used by the lexer's identifier
// scanner (scanner.cpp). Kept separate from `detail/scanner.h` so the table can change
// without touching the scanner state machine (STYLE.md §3/§8).
//
// This header is implementation detail and is never included by another module.
// =============================================================================
#pragma once

#include <string_view>

#include "cp3/core/token.hpp"

namespace cp3 {

// Maps an identifier lexeme to its keyword kind, or `TokenKind::Identifier`
// when it is not a keyword.
TokenKind keywordFor(std::string_view s) noexcept;

}  // namespace cp3
