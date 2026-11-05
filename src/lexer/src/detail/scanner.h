// =============================================================================
// src/lexer/src/detail/scanner.h - PRIVATE lexer implementation header.
//
// Declares `Scanner`, the internal cursor/state machine that turns one
// SourceFile into a TokenStream, plus the byte-classification and UTF-8 helpers
// shared across the lexer's translation units:
//
//   lexer.cpp      frozen `Lexer::lex` entry point
//   scanner.cpp    cursor, source positions, byte classes, UTF-8, trivia,
//                  token dispatch, identifiers, error recovery  (this header)
//   numbers.cpp    numeric literals
//   strings.cpp    string literals and escape sequences
//   templates.cpp  template literals (lexed flat - DECISIONS.md D7)
//   regexp.cpp     regular-webserverion literals and flags (DECISIONS.md D52)
//   keywords.cpp   keyword / operator tables
//
// The frozen public boundary is cp3/lexer.hpp (class `Lexer`); everything here
// is implementation detail and is never included by another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/diag/diagnostic.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/token.hpp"

namespace cp3 {

// --- byte classification + UTF-8 (scanner.cpp) -------------------------------
// Byte-oriented: cp3 source is UTF-8 and any byte >= 0x80 is treated as an
// identifier character, which is a pragmatic and safe choice for a byte-column
// lexer.
bool isDecDigit(unsigned char c) noexcept;
bool isHexDigit(unsigned char c) noexcept;
bool isIdentStart(unsigned char c) noexcept;
bool isIdentContinue(unsigned char c) noexcept;
// Appends code point `cp` to `out` as UTF-8.
void appendUtf8(std::string& out, std::uint32_t cp);

// -----------------------------------------------------------------------------
// Scanner - all lexer state. `run()` produces the token stream; `Lexer::lex`
// (lexer.cpp) is its only caller.
// -----------------------------------------------------------------------------
class Scanner {
public:
    Scanner(const SourceFile& file, DiagnosticSink& sink) noexcept
        : file_(file), sink_(sink), text_(file.text),
          n_(static_cast<std::size_t>(file.text.size())) {}

    TokenStream run();

private:
    // --- cursor / positions / recovery (scanner.cpp) -------------------------
    char peek(std::size_t k = 0) const noexcept;
    SourceSpan span(std::uint32_t b, std::uint32_t e) const;
    void errorAt(const char* code, std::uint32_t b, std::uint32_t e, std::string msg);
    void commit(Token t);
    void emitSimple(TokenKind kind, std::uint32_t len);
    bool isLineTermAt(std::size_t i) const noexcept;

    // --- trivia (scanner.cpp) ------------------------------------------------
    void skipTrivia();
    void skipBlockComment(bool& newline);

    // --- token dispatch / identifiers (scanner.cpp) --------------------------
    void scanToken();
    void scanIdentifier();
    void scanPrivateIdentifier();
    std::size_t scanIdentChars();

    // --- numeric literals (numbers.cpp) --------------------------------------
    bool scanDigitRun(bool (*isDigit)(unsigned char), std::string& clean, bool& sepError);
    void scanNumber();
    void scanRadixNumber(bool& error, double& value);
    void scanDecimalNumber(bool& error, double& value);

    // --- strings / escapes (strings.cpp) -------------------------------------
    void scanEscape(std::string& out, bool inTemplate);
    bool scanSimpleEscape(std::string& out, unsigned char c, bool inTemplate);
    void scanNumericEscape(std::string& out, unsigned char c, std::uint32_t escStart);
    void scanHexEscape(std::string& out, std::uint32_t escStart);
    void scanUnicodeEscape(std::string& out, std::uint32_t escStart);
    void scanString(char quote);

    // --- templates (templates.cpp) -------------------------------------------
    void scanTemplateChunk(bool continuing);

    // --- regular webserverions (regexp.cpp) ------------------------------------
    bool regexAllowed() const noexcept;
    void scanRegex();
    void scanRegexFlags(std::uint32_t flagsStart, std::uint32_t end, bool& error);

    // --- punctuation / operators (keywords.cpp) ------------------------------
    void scanPunctuation(char c);
    void scanPunctDot();
    void scanPunctQuestion();
    void scanPunctEquals();
    void scanPunctBang();
    void scanPunctLess();
    void scanPunctGreater();
    void scanPunctPlus();
    void scanPunctMinus();
    void scanPunctStar();
    void scanPunctSlash();
    void scanPunctPercent();
    void scanPunctAmpersand();
    void scanPunctPipe();
    void scanPunctCaret();
    void scanPunctBackslash();
    void scanInvalidCharacter(char c);

    // --- state ---------------------------------------------------------------
    const SourceFile& file_;
    DiagnosticSink& sink_;
    std::string_view text_;
    std::size_t n_;
    std::size_t i_ = 0;

    std::vector<Token> tokens_;
    std::vector<int> templateDepth_;  // brace depth per open `${ ... }`
    TokenKind prev_ = TokenKind::Unknown;
    bool hasPrev_ = false;
    bool pendingNewline_ = false;
};

}  // namespace cp3
