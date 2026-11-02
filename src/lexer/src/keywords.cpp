// =============================================================================
// src/lexer/src/keywords.cpp - keyword and operator tables.
//
// Implements `keywordFor` (detail/keywords.h) and `Scanner::scanPunctuation` plus its
// per-operator helpers (declared in detail/scanner.h). Multi-byte operators use
// maximal munch: `>>>=` is tested before `>>>` (DECISIONS.md D53), and `?.` is
// only a QuestionDot when not followed by a digit. Unknown bytes report CP3101
// and recover as an `Invalid` token (`\` as `Backslash`) (STYLE.md §6).
// =============================================================================
#include "detail/keywords.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/scanner.h"

namespace cp3 {
namespace {

// Single-character punctuation, table-driven so the multi-byte operators below
// stay readable.
struct PunctToken {
    char text;
    TokenKind kind;
};

constexpr PunctToken kSimplePunctuation[] = {
    {'(', TokenKind::LParen},   {')', TokenKind::RParen},
    {'}', TokenKind::RBrace},   {'[', TokenKind::LBracket},
    {']', TokenKind::RBracket}, {';', TokenKind::Semicolon},
    {',', TokenKind::Comma},    {':', TokenKind::Colon},
    {'~', TokenKind::Tilde},    {'@', TokenKind::At},
};

}  // namespace

TokenKind keywordFor(std::string_view s) noexcept {
    static const std::unordered_map<std::string_view, TokenKind> kKeywords = {
        {"let", TokenKind::KwLet},         {"var", TokenKind::KwVar},
        {"const", TokenKind::KwConst},     {"function", TokenKind::KwFunction},
        {"return", TokenKind::KwReturn},   {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},       {"while", TokenKind::KwWhile},
        {"do", TokenKind::KwDo},           {"for", TokenKind::KwFor},
        {"of", TokenKind::KwOf},           {"in", TokenKind::KwIn},
        {"switch", TokenKind::KwSwitch},   {"case", TokenKind::KwCase},
        {"default", TokenKind::KwDefault}, {"break", TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"throw", TokenKind::KwThrow},     {"try", TokenKind::KwTry},
        {"catch", TokenKind::KwCatch},     {"finally", TokenKind::KwFinally},
        {"class", TokenKind::KwClass},     {"interface", TokenKind::KwInterface},
        {"extends", TokenKind::KwExtends}, {"implements", TokenKind::KwImplements},
        {"abstract", TokenKind::KwAbstract},
        {"static", TokenKind::KwStatic},   {"public", TokenKind::KwPublic},
        {"private", TokenKind::KwPrivate}, {"protected", TokenKind::KwProtected},
        {"enum", TokenKind::KwEnum},       {"new", TokenKind::KwNew},
        {"this", TokenKind::KwThis},       {"import", TokenKind::KwImport},
        {"export", TokenKind::KwExport},   {"from", TokenKind::KwFrom},
        {"as", TokenKind::KwAs},           {"native", TokenKind::KwNative},
        {"async", TokenKind::KwAsync},     {"await", TokenKind::KwAwait},
        {"weak", TokenKind::KwWeak},       {"ref", TokenKind::KwRef},
        {"instanceof", TokenKind::KwInstanceof},
        {"typeof", TokenKind::KwTypeof},   {"get", TokenKind::KwGet},
        {"set", TokenKind::KwSet},         {"true", TokenKind::KwTrue},
        {"false", TokenKind::KwFalse},     {"null", TokenKind::KwNull},
        {"undefined", TokenKind::KwUndefined},
        {"void", TokenKind::KwVoid},       {"unknown", TokenKind::KwUnknown},
        {"type", TokenKind::KwType},       {"any", TokenKind::KwAny},
        {"delete", TokenKind::KwDelete},   {"yield", TokenKind::KwYield},
    };
    auto it = kKeywords.find(s);
    return it == kKeywords.end() ? TokenKind::Identifier : it->second;
}

void Scanner::scanPunctuation(char c) {
    // `{` tracks interpolation nesting so a later `}` resumes the template.
    if (c == '{') {
        if (!templateDepth_.empty()) ++templateDepth_.back();
        emitSimple(TokenKind::LBrace, 1);
        return;
    }
    for (const PunctToken& p : kSimplePunctuation) {
        if (p.text == c) {
            emitSimple(p.kind, 1);
            return;
        }
    }
    switch (c) {
        case '.': scanPunctDot(); return;
        case '?': scanPunctQuestion(); return;
        case '=': scanPunctEquals(); return;
        case '!': scanPunctBang(); return;
        case '<': scanPunctLess(); return;
        case '>': scanPunctGreater(); return;
        case '+': scanPunctPlus(); return;
        case '-': scanPunctMinus(); return;
        case '*': scanPunctStar(); return;
        case '/': scanPunctSlash(); return;
        case '%': scanPunctPercent(); return;
        case '&': scanPunctAmpersand(); return;
        case '|': scanPunctPipe(); return;
        case '^': scanPunctCaret(); return;
        case '\\': scanPunctBackslash(); return;
        default: break;
    }
    scanInvalidCharacter(c);
}

void Scanner::scanPunctDot() {
    if (peek(1) == '.' && peek(2) == '.') {
        emitSimple(TokenKind::Ellipsis, 3);
    } else if (peek(1) == '.') {
        emitSimple(TokenKind::DotDot, 2);
    } else {
        emitSimple(TokenKind::Dot, 1);
    }
}

void Scanner::scanPunctQuestion() {
    if (peek(1) == '?' && peek(2) == '=') {
        emitSimple(TokenKind::QuestionQuestionAssign, 3);
    } else if (peek(1) == '?') {
        emitSimple(TokenKind::QuestionQuestion, 2);
    } else if (peek(1) == '.' && !isDecDigit(static_cast<unsigned char>(peek(2)))) {
        emitSimple(TokenKind::QuestionDot, 2);
    } else {
        emitSimple(TokenKind::Question, 1);
    }
}

void Scanner::scanPunctEquals() {
    if (peek(1) == '=' && peek(2) == '=') {
        emitSimple(TokenKind::StrictEqual, 3);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::Equal, 2);
    } else if (peek(1) == '>') {
        emitSimple(TokenKind::Arrow, 2);
    } else {
        emitSimple(TokenKind::Assign, 1);
    }
}

void Scanner::scanPunctBang() {
    if (peek(1) == '=' && peek(2) == '=') {
        emitSimple(TokenKind::StrictNotEqual, 3);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::NotEqual, 2);
    } else {
        emitSimple(TokenKind::Bang, 1);
    }
}

void Scanner::scanPunctLess() {
    if (peek(1) == '<' && peek(2) == '=') {
        emitSimple(TokenKind::ShiftLeftAssign, 3);
    } else if (peek(1) == '<') {
        emitSimple(TokenKind::ShiftLeft, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::LessEqual, 2);
    } else {
        emitSimple(TokenKind::Less, 1);
    }
}

void Scanner::scanPunctGreater() {
    // Maximal munch: `>>>=` is tested before `>>>` so the four-byte sequence
    // wins (DECISIONS D53).
    if (peek(1) == '>' && peek(2) == '>' && peek(3) == '=') {
        emitSimple(TokenKind::UnsignedShiftRightAssign, 4);
    } else if (peek(1) == '>' && peek(2) == '>') {
        emitSimple(TokenKind::UnsignedShiftRight, 3);
    } else if (peek(1) == '>' && peek(2) == '=') {
        emitSimple(TokenKind::ShiftRightAssign, 3);
    } else if (peek(1) == '>') {
        emitSimple(TokenKind::ShiftRight, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::GreaterEqual, 2);
    } else {
        emitSimple(TokenKind::Greater, 1);
    }
}

void Scanner::scanPunctPlus() {
    if (peek(1) == '+') {
        emitSimple(TokenKind::PlusPlus, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::PlusAssign, 2);
    } else {
        emitSimple(TokenKind::Plus, 1);
    }
}

void Scanner::scanPunctMinus() {
    if (peek(1) == '-') {
        emitSimple(TokenKind::MinusMinus, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::MinusAssign, 2);
    } else {
        emitSimple(TokenKind::Minus, 1);
    }
}

void Scanner::scanPunctStar() {
    if (peek(1) == '*' && peek(2) == '=') {
        emitSimple(TokenKind::StarStarAssign, 3);
    } else if (peek(1) == '*') {
        emitSimple(TokenKind::StarStar, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::StarAssign, 2);
    } else {
        emitSimple(TokenKind::Star, 1);
    }
}

void Scanner::scanPunctSlash() {
    if (peek(1) == '=') {
        emitSimple(TokenKind::SlashAssign, 2);
    } else {
        emitSimple(TokenKind::Slash, 1);
    }
}

void Scanner::scanPunctPercent() {
    if (peek(1) == '=') {
        emitSimple(TokenKind::PercentAssign, 2);
    } else {
        emitSimple(TokenKind::Percent, 1);
    }
}

void Scanner::scanPunctAmpersand() {
    if (peek(1) == '&' && peek(2) == '=') {
        emitSimple(TokenKind::AndAndAssign, 3);
    } else if (peek(1) == '&') {
        emitSimple(TokenKind::AndAnd, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::AmpersandAssign, 2);
    } else {
        emitSimple(TokenKind::Ampersand, 1);
    }
}

void Scanner::scanPunctPipe() {
    if (peek(1) == '|' && peek(2) == '=') {
        emitSimple(TokenKind::OrOrAssign, 3);
    } else if (peek(1) == '|') {
        emitSimple(TokenKind::OrOr, 2);
    } else if (peek(1) == '=') {
        emitSimple(TokenKind::PipeAssign, 2);
    } else {
        emitSimple(TokenKind::Pipe, 1);
    }
}

void Scanner::scanPunctCaret() {
    if (peek(1) == '=') {
        emitSimple(TokenKind::CaretAssign, 2);
    } else {
        emitSimple(TokenKind::Caret, 1);
    }
}

void Scanner::scanPunctBackslash() {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    ++i_;
    errorAt(diagcode::kInvalidCharacter, start, static_cast<std::uint32_t>(i_),
            "unexpected '\\' outside a string or template literal");
    Token t;
    t.kind = TokenKind::Backslash;
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    commit(std::move(t));
}

// Any other byte is an invalid character; consume one byte and recover.
void Scanner::scanInvalidCharacter(char c) {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    ++i_;
    errorAt(diagcode::kInvalidCharacter, start, static_cast<std::uint32_t>(i_),
            std::string("unexpected character '") + c + "'");
    Token t;
    t.kind = TokenKind::Invalid;
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    commit(std::move(t));
}

}  // namespace cp3
