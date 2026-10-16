// =============================================================================
// cp3/lexer.cpp — STUB implementation (see cp3/lexer.hpp).
//
// Emits only EndOfFile. A real scanner replaces the body of lex() without
// touching the module boundary.
// =============================================================================
#include "cp3/lexer.hpp"

#include <cstdint>
#include <utility>

namespace cp3 {

TokenStream Lexer::lex() {
    const std::uint32_t end = static_cast<std::uint32_t>(file_.text.size());

    Token eof;
    eof.kind = TokenKind::EndOfFile;
    eof.span = SourceManager::makeSpan(&file_, end, end);

    std::vector<Token> tokens;
    tokens.push_back(std::move(eof));
    return TokenStream(std::move(tokens));
}

}  // namespace cp3
