// =============================================================================
// src/lexer/src/scanner.cpp - lexer cursor, byte classes, trivia, dispatch.
//
// Implements the core of the internal `Scanner` declared in detail/scanner.h:
//   * byte classification and UTF-8 encoding;
//   * cursor/position helpers and diagnostic/commit primitives;
//   * whitespace + comment trivia (with block-comment recovery, CP3104);
//   * token dispatch and the identifier / private-identifier scanners.
//
// The literal scanners live in numbers.cpp / strings.cpp / templates.cpp /
// regexp.cpp and the operator tables in keywords.cpp (STYLE.md §8). Numeric
// problems are reported as CP3101-CP3107; the scanner never throws.
// =============================================================================
#include "detail/scanner.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/keywords.h"

namespace cp3 {

// --- byte classification + UTF-8 ---------------------------------------------
bool isDecDigit(unsigned char c) noexcept { return c >= '0' && c <= '9'; }

bool isHexDigit(unsigned char c) noexcept {
    return isDecDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isIdentStart(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$' || c >= 0x80;
}

bool isIdentContinue(unsigned char c) noexcept {
    return isIdentStart(c) || isDecDigit(c);
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// =============================================================================
// Scanner - entry / helpers
// =============================================================================
TokenStream Scanner::run() {
    while (i_ < n_) {
        skipTrivia();
        if (i_ >= n_) break;
        scanToken();
    }
    if (!templateDepth_.empty()) {
        errorAt(diagcode::kUnterminatedTemplate, static_cast<std::uint32_t>(n_),
                static_cast<std::uint32_t>(n_), "unterminated template literal");
    }
    // EndOfFile always terminates the stream.
    const std::uint32_t end = static_cast<std::uint32_t>(n_);
    Token eof;
    eof.kind = TokenKind::EndOfFile;
    eof.span = SourceManager::makeSpan(&file_, end, end);
    eof.precededByNewline = pendingNewline_;
    tokens_.push_back(std::move(eof));
    return TokenStream(std::move(tokens_));
}

char Scanner::peek(std::size_t k) const noexcept {
    return (i_ + k < n_) ? text_[i_ + k] : '\0';
}

SourceSpan Scanner::span(std::uint32_t b, std::uint32_t e) const {
    return SourceManager::makeSpan(&file_, b, e);
}

// The single sink for every lexical diagnostic (all CP3101-CP3107 sites route
// through here). Two suite-wide conventions are applied in this one place
// (DECISIONS.md D54):
//   * the location is anchored to column 1 of the offending line, dropping the
//     literal's real column, matching the parser and sema (`anchorSpan`) since
//     the frozen corpus is line-granular;
//   * the message is prefixed with "syntax error: " so every lexical error
//     contains the literal word "syntax", exactly as the parser does.
// Line numbers and each message's specific detail / CP31xx code are preserved.
void Scanner::errorAt(const char* code, std::uint32_t b, std::uint32_t e, std::string msg) {
    SourceSpan sp = span(b, e);
    if (sp.file != nullptr && sp.begin.line != 0) {
        const auto& starts = sp.file->lineStarts;
        std::uint32_t offset = sp.begin.offset;
        if (sp.begin.line - 1 < starts.size()) offset = starts[sp.begin.line - 1];
        SourcePos p;
        p.offset = offset;
        p.line = sp.begin.line;
        p.column = 1;
        sp = SourceSpan{sp.file, p, p};
    }
    sink_.error(code, sp, "syntax error: " + std::move(msg));
}

void Scanner::commit(Token t) {
    t.precededByNewline = pendingNewline_;
    pendingNewline_ = false;
    prev_ = t.kind;
    hasPrev_ = true;
    tokens_.push_back(std::move(t));
}

void Scanner::emitSimple(TokenKind kind, std::uint32_t len) {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    i_ += len;
    Token t;
    t.kind = kind;
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    commit(std::move(t));
}

bool Scanner::isLineTermAt(std::size_t i) const noexcept {
    if (i >= n_) return false;
    const unsigned char c = static_cast<unsigned char>(text_[i]);
    if (c == '\n' || c == '\r') return true;
    return c == 0xE2 && i + 2 < n_ &&
           static_cast<unsigned char>(text_[i + 1]) == 0x80 &&
           (static_cast<unsigned char>(text_[i + 2]) == 0xA8 ||
            static_cast<unsigned char>(text_[i + 2]) == 0xA9);
}

// =============================================================================
// Scanner - trivia
// =============================================================================
void Scanner::skipTrivia() {
    bool newline = false;
    while (i_ < n_) {
        const unsigned char c = static_cast<unsigned char>(text_[i_]);
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            ++i_;
            continue;
        }
        if (c == '\n') {
            ++i_;
            newline = true;
            continue;
        }
        if (c == '\r') {
            ++i_;
            if (i_ < n_ && text_[i_] == '\n') ++i_;
            newline = true;
            continue;
        }
        // U+2028 / U+2029 (line/paragraph separator).
        if (isLineTermAt(i_)) {
            i_ += 3;
            newline = true;
            continue;
        }
        // U+00A0 NBSP and U+FEFF BOM count as whitespace.
        if (c == 0xC2 && i_ + 1 < n_ &&
            static_cast<unsigned char>(text_[i_ + 1]) == 0xA0) {
            i_ += 2;
            continue;
        }
        if (c == 0xEF && i_ + 2 < n_ &&
            static_cast<unsigned char>(text_[i_ + 1]) == 0xBB &&
            static_cast<unsigned char>(text_[i_ + 2]) == 0xBF) {
            i_ += 3;
            continue;
        }
        if (c == '/' && i_ + 1 < n_ && text_[i_ + 1] == '/') {
            i_ += 2;
            while (i_ < n_ && !isLineTermAt(i_)) ++i_;
            continue;
        }
        if (c == '/' && i_ + 1 < n_ && text_[i_ + 1] == '*') {
            skipBlockComment(newline);
            continue;
        }
        break;
    }
    if (newline) pendingNewline_ = true;
}

void Scanner::skipBlockComment(bool& newline) {
    const std::uint32_t cs = static_cast<std::uint32_t>(i_);
    i_ += 2;
    bool closed = false;
    while (i_ < n_) {
        if (text_[i_] == '*' && i_ + 1 < n_ && text_[i_ + 1] == '/') {
            i_ += 2;
            closed = true;
            break;
        }
        if (isLineTermAt(i_)) newline = true;
        ++i_;
    }
    if (!closed) {
        errorAt(diagcode::kUnterminatedComment, cs, static_cast<std::uint32_t>(i_),
                "unterminated block comment");
    }
}

// =============================================================================
// Scanner - token dispatch / identifiers
// =============================================================================
void Scanner::scanToken() {
    const char c = text_[i_];

    // Closing `}` of a template interpolation resumes template scanning
    // instead of producing an RBrace token.
    if (!templateDepth_.empty() && c == '}') {
        if (templateDepth_.back() == 0) {
            templateDepth_.pop_back();
            scanTemplateChunk(true);
            return;
        }
        --templateDepth_.back();
        emitSimple(TokenKind::RBrace, 1);
        return;
    }

    if (c == '`') {
        scanTemplateChunk(false);
        return;
    }

    if (isIdentStart(static_cast<unsigned char>(c))) {
        scanIdentifier();
        return;
    }

    if (isDecDigit(static_cast<unsigned char>(c)) ||
        (c == '.' && isDecDigit(static_cast<unsigned char>(peek(1))))) {
        scanNumber();
        return;
    }

    if (c == '"' || c == '\'') {
        scanString(c);
        return;
    }

    if (c == '#') {
        scanPrivateIdentifier();
        return;
    }

    if (c == '/' && regexAllowed()) {
        scanRegex();
        return;
    }

    scanPunctuation(c);
}

std::size_t Scanner::scanIdentChars() {
    while (i_ < n_ && isIdentContinue(static_cast<unsigned char>(text_[i_]))) ++i_;
    return i_;
}

void Scanner::scanIdentifier() {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    ++i_;
    scanIdentChars();
    const std::uint32_t end = static_cast<std::uint32_t>(i_);
    const std::string_view lex = text_.substr(start, end - start);
    Token t;
    t.kind = keywordFor(lex);
    t.span = span(start, end);
    t.lexeme = file_.slice(t.span);
    commit(std::move(t));
}

void Scanner::scanPrivateIdentifier() {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    if (!(peek(1) != '\0' && isIdentStart(static_cast<unsigned char>(peek(1))))) {
        // A bare `#` is not a valid character.
        ++i_;
        errorAt(diagcode::kInvalidCharacter, start, static_cast<std::uint32_t>(i_),
                "unexpected character '#'");
        Token t;
        t.kind = TokenKind::Invalid;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        commit(std::move(t));
        return;
    }
    ++i_;
    scanIdentChars();
    Token t;
    t.kind = TokenKind::PrivateIdentifier;
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    commit(std::move(t));
}

}  // namespace cp3
