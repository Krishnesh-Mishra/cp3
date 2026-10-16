// =============================================================================
// src/lexer/src/regexp.cpp — regular-expression literals and flags.
//
// Implements `Scanner::regexAllowed`, `Scanner::scanRegex` and
// `Scanner::scanRegexFlags` (declared in scanner.h). Per DECISIONS.md D52 there
// are no dedicated regex fields on `Token`: the pattern is stored in
// `stringValue` and the flag letters in `rawText`.
//
// A regex cannot span lines; an unterminated pattern reports CP3107, as do
// unknown or duplicated flags. Every problem is reported through the sink and
// the token is still emitted (STYLE.md §6).
// =============================================================================
#include "scanner.h"

#include <cstdint>
#include <string>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// True when `/` at the current position should begin a RegExp literal rather
// than a division: the previous *significant* token cannot end an expression.
// This is the standard "previous token" heuristic.
bool Scanner::regexAllowed() const noexcept {
    if (!hasPrev_) return true;
    switch (prev_) {
        case TokenKind::Identifier:
        case TokenKind::PrivateIdentifier:
        case TokenKind::NumericLiteral:
        case TokenKind::StringLiteral:
        case TokenKind::RegExpLiteral:
        case TokenKind::TemplateString:
        case TokenKind::TemplateTail:
        case TokenKind::RParen:
        case TokenKind::RBracket:
        case TokenKind::RBrace:
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus:
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
        case TokenKind::KwNull:
        case TokenKind::KwUndefined:
        case TokenKind::KwThis:
            return false;
        default:
            return true;
    }
}

void Scanner::scanRegex() {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    ++i_;  // opening '/'
    bool inClass = false;
    bool closed = false;
    bool error = false;
    while (i_ < n_) {
        const unsigned char c = static_cast<unsigned char>(text_[i_]);
        if (isLineTermAt(i_)) break;  // regex cannot span lines
        if (c == '\\') {
            if (i_ + 1 < n_ && !isLineTermAt(i_ + 1)) {
                i_ += 2;
                continue;
            }
            error = true;
            ++i_;
            break;
        }
        if (c == '[') {
            inClass = true;
            ++i_;
            continue;
        }
        if (c == ']') {
            inClass = false;
            ++i_;
            continue;
        }
        if (c == '/' && !inClass) {
            ++i_;
            closed = true;
            break;
        }
        ++i_;
    }

    const std::uint32_t patternEnd =
        closed ? static_cast<std::uint32_t>(i_ - 1) : static_cast<std::uint32_t>(i_);
    const std::uint32_t flagsStart = static_cast<std::uint32_t>(i_);
    if (closed) {
        while (i_ < n_ && isIdentContinue(static_cast<unsigned char>(text_[i_]))) ++i_;
    }
    const std::uint32_t end = static_cast<std::uint32_t>(i_);

    if (!closed) {
        error = true;
        errorAt(diagcode::kInvalidRegExp, start, end,
                "unterminated regular expression literal");
    }

    Token t;
    t.kind = TokenKind::RegExpLiteral;
    t.span = span(start, end);
    t.lexeme = file_.slice(t.span);
    // No dedicated regex fields exist on Token: pattern goes in
    // stringValue and the flag letters in rawText.
    t.stringValue = std::string(text_.substr(start + 1, patternEnd - (start + 1)));
    t.rawText = std::string(text_.substr(flagsStart, end - flagsStart));

    scanRegexFlags(flagsStart, end, error);
    (void)error;
    commit(std::move(t));
}

// Validates the flags: a fixed set, no duplicates. Each problem reports CP3107.
void Scanner::scanRegexFlags(std::uint32_t flagsStart, std::uint32_t end, bool& error) {
    bool seenFlags[256] = {false};
    for (std::uint32_t p = flagsStart; p < end; ++p) {
        const unsigned char f = static_cast<unsigned char>(text_[p]);
        const bool known = f == 'd' || f == 'g' || f == 'i' || f == 'm' ||
                           f == 's' || f == 'u' || f == 'v' || f == 'y';
        if (!known) {
            error = true;
            errorAt(diagcode::kInvalidRegExp, p, p + 1,
                    std::string("invalid regular expression flag '") +
                        static_cast<char>(f) + "'");
        } else if (seenFlags[f]) {
            error = true;
            errorAt(diagcode::kInvalidRegExp, p, p + 1,
                    std::string("duplicate regular expression flag '") +
                        static_cast<char>(f) + "'");
        }
        seenFlags[f] = true;
    }
}

}  // namespace cp3
