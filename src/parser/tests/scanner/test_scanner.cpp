// =============================================================================
// src/parser/tests/test_scanner.cpp - scanner core: trivia, dispatch, and
// punctuation. Test-only.
// =============================================================================
#include "test_scanner.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "../harness/test_text.h"

namespace cp3test {

using cp3::SourceManager;

namespace {

using enum TokenKind;

const std::unordered_map<std::string, TokenKind>& keywordTable() {
    static const std::unordered_map<std::string, TokenKind> k = {
        {"let", KwLet}, {"var", KwVar}, {"const", KwConst},
        {"function", KwFunction}, {"return", KwReturn},
        {"if", KwIf}, {"else", KwElse},
        {"while", KwWhile}, {"do", KwDo}, {"for", KwFor},
        {"of", KwOf}, {"in", KwIn},
        {"switch", KwSwitch}, {"case", KwCase}, {"default", KwDefault},
        {"break", KwBreak}, {"continue", KwContinue},
        {"throw", KwThrow}, {"try", KwTry}, {"catch", KwCatch}, {"finally", KwFinally},
        {"class", KwClass}, {"interface", KwInterface},
        {"extends", KwExtends}, {"implements", KwImplements},
        {"abstract", KwAbstract}, {"static", KwStatic},
        {"public", KwPublic}, {"private", KwPrivate}, {"protected", KwProtected},
        {"enum", KwEnum}, {"new", KwNew}, {"this", KwThis},
        {"import", KwImport}, {"export", KwExport}, {"from", KwFrom},
        {"as", KwAs}, {"native", KwNative}, {"async", KwAsync}, {"await", KwAwait},
        {"weak", KwWeak}, {"ref", KwRef}, {"typeof", KwTypeof},
        {"get", KwGet}, {"set", KwSet},
        {"true", KwTrue}, {"false", KwFalse}, {"null", KwNull}, {"undefined", KwUndefined},
        {"void", KwVoid}, {"unknown", KwUnknown}, {"type", KwType},
        {"any", KwAny}, {"delete", KwDelete}, {"yield", KwYield},
    };
    return k;
}

// Multi-character operators, longest first (order is significant).
struct PunctOp {
    const char* text;
    TokenKind kind;
};

const PunctOp kPunctOps[] = {
    {"...", Ellipsis}, {">>>", UnsignedShiftRight}, {">>=", ShiftRightAssign},
    {"===", StrictEqual}, {"!==", StrictNotEqual}, {"**=", StarStarAssign},
    {"<<=", ShiftLeftAssign}, {"&&=", AndAndAssign}, {"||=", OrOrAssign},
    {"?\?=", QuestionQuestionAssign}, {">>", ShiftRight}, {"<<", ShiftLeft},
    {"<=", LessEqual}, {">=", GreaterEqual}, {"==", Equal}, {"!=", NotEqual},
    {"&&", AndAnd}, {"||", OrOr}, {"??", QuestionQuestion}, {"?.", QuestionDot},
    {"=>", Arrow}, {"++", PlusPlus}, {"--", MinusMinus}, {"+=", PlusAssign},
    {"-=", MinusAssign}, {"*=", StarAssign}, {"/=", SlashAssign},
    {"%=", PercentAssign}, {"&=", AmpersandAssign}, {"|=", PipeAssign},
    {"^=", CaretAssign}, {"**", StarStar}, {"..", DotDot},
};

}  // namespace

TokenStream TestScanner::scan() {
    while (p_ < src_.size()) {
        if (!lexOne()) break;
    }
    Token eof;
    eof.kind = EndOfFile;
    eof.span = SourceManager::makeSpan(&file_, static_cast<std::uint32_t>(src_.size()),
                                       static_cast<std::uint32_t>(src_.size()));
    toks_.push_back(std::move(eof));
    return TokenStream(std::move(toks_));
}

char TestScanner::cur() const { return p_ < src_.size() ? src_[p_] : '\0'; }

char TestScanner::at(std::size_t k) const {
    return (p_ + k) < src_.size() ? src_[p_ + k] : '\0';
}

bool TestScanner::eof() const { return p_ >= src_.size(); }

SourceSpan TestScanner::span(std::size_t b, std::size_t e) const {
    return SourceManager::makeSpan(&file_, static_cast<std::uint32_t>(b),
                                   static_cast<std::uint32_t>(e));
}

void TestScanner::emit(TokenKind k, std::size_t b, std::size_t e) {
    Token t;
    t.kind = k;
    t.span = span(b, e);
    t.lexeme = std::string_view(src_).substr(b, e - b);
    t.precededByNewline = pendingNl_;
    pendingNl_ = false;
    toks_.push_back(std::move(t));
}

// Returns the previously emitted significant token kind (EndOfFile if none).
TokenKind TestScanner::lastKind() const {
    for (auto it = toks_.rbegin(); it != toks_.rend(); ++it) {
        if (it->kind != LineComment && it->kind != BlockComment) return it->kind;
    }
    return EndOfFile;
}

bool TestScanner::prevIsValue() const {
    switch (lastKind()) {
        case Identifier:
        case NumericLiteral:
        case StringLiteral:
        case TemplateString:
        case TemplateTail:
        case RegExpLiteral:
        case RParen:
        case RBracket:
        case RBrace:
        case KwTrue:
        case KwFalse:
        case KwNull:
        case KwUndefined:
        case KwThis:
        case PlusPlus:
        case MinusMinus:
            return true;
        default:
            return false;
    }
}

void TestScanner::skipTrivia() {
    for (;;) {
        char c = cur();
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r') {
            ++p_;
        } else if (c == '\n') {
            pendingNl_ = true;
            ++p_;
        } else if (c == '/' && at(1) == '/') {
            while (!eof() && cur() != '\n') ++p_;
        } else if (c == '/' && at(1) == '*') {
            p_ += 2;
            while (!eof() && !(cur() == '*' && at(1) == '/')) ++p_;
            if (!eof()) p_ += 2;
        } else {
            return;
        }
    }
}

bool TestScanner::lexOne() {
    skipTrivia();
    if (eof()) return false;

    char c = cur();
    if (isIdentStart(c)) { lexIdentifier(); return true; }
    if (isDigit(c) || (c == '.' && isDigit(at(1)))) { lexNumber(); return true; }
    if (c == '"' || c == '\'') { lexString(); return true; }
    if (c == '`') { lexTemplateOpen(); return true; }
    if (c == '/' && !prevIsValue() && at(1) != '=') { lexRegExp(); return true; }

    // `}` may close a `${ ... }` template substitution.
    if (c == '}' && !tmplStack_.empty() && braceDepth_ == tmplStack_.back()) {
        tmplStack_.pop_back();
        lexTemplateContinuation();
        return true;
    }

    lexPunctuation();
    return true;
}

void TestScanner::lexIdentifier() {
    std::size_t b = p_;
    while (isIdentPart(cur())) ++p_;
    std::string text = src_.substr(b, p_ - b);
    auto it = keywordTable().find(text);
    emit(it != keywordTable().end() ? it->second : Identifier, b, p_);
}

void TestScanner::lexPunctuation() {
    std::size_t b = p_;
    for (const PunctOp& op : kPunctOps) {
        std::size_t n = std::char_traits<char>::length(op.text);
        if (src_.compare(b, n, op.text) == 0) {
            p_ += n;
            emit(op.kind, b, p_);
            return;
        }
    }
    lexSingleCharPunctuation();
}

void TestScanner::lexSingleCharPunctuation() {
    std::size_t b = p_;
    switch (cur()) {
        case '(': ++p_; emit(LParen, b, p_); return;
        case ')': ++p_; emit(RParen, b, p_); return;
        case '{': ++p_; ++braceDepth_; emit(LBrace, b, p_); return;
        case '}':
            ++p_;
            if (braceDepth_ > 0) --braceDepth_;
            emit(RBrace, b, p_);
            return;
        case '[': ++p_; emit(LBracket, b, p_); return;
        case ']': ++p_; emit(RBracket, b, p_); return;
        case ';': ++p_; emit(Semicolon, b, p_); return;
        case ',': ++p_; emit(Comma, b, p_); return;
        case '.': ++p_; emit(Dot, b, p_); return;
        case ':': ++p_; emit(Colon, b, p_); return;
        case '?': ++p_; emit(Question, b, p_); return;
        case '+': ++p_; emit(Plus, b, p_); return;
        case '-': ++p_; emit(Minus, b, p_); return;
        case '*': ++p_; emit(Star, b, p_); return;
        case '/': ++p_; emit(Slash, b, p_); return;
        case '%': ++p_; emit(Percent, b, p_); return;
        case '=': ++p_; emit(Assign, b, p_); return;
        case '<': ++p_; emit(Less, b, p_); return;
        case '>': ++p_; emit(Greater, b, p_); return;
        case '!': ++p_; emit(Bang, b, p_); return;
        case '&': ++p_; emit(Ampersand, b, p_); return;
        case '|': ++p_; emit(Pipe, b, p_); return;
        case '^': ++p_; emit(Caret, b, p_); return;
        case '~': ++p_; emit(Tilde, b, p_); return;
        case '@': ++p_; emit(At, b, p_); return;
        case '\\': ++p_; emit(Backslash, b, p_); return;
        default:
            ++p_;
            emit(Unknown, b, p_);
            return;
    }
}

}  // namespace cp3test
