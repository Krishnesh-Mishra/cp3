// =============================================================================
// src/parser/tests/test_scanner_literals.cpp - scanner literal/trivia lexing:
// numbers, strings, template chunks, and regular expressions. Test-only.
// =============================================================================
#include "test_scanner.h"

#include <cstdlib>
#include <string>

#include "../harness/test_text.h"

namespace cp3test {
namespace {

using enum TokenKind;

}  // namespace

double TestScanner::parseNumberText(std::string s) const {
    std::string clean;
    for (char ch : s) if (ch != '_') clean.push_back(ch);
    if (clean.empty()) return 0.0;
    if (clean.size() > 2 && clean[0] == '0' &&
        (clean[1] == 'x' || clean[1] == 'X')) {
        return static_cast<double>(std::strtoull(clean.c_str() + 2, nullptr, 16));
    }
    if (clean.size() > 2 && clean[0] == '0' &&
        (clean[1] == 'b' || clean[1] == 'B')) {
        return static_cast<double>(std::strtoull(clean.c_str() + 2, nullptr, 2));
    }
    if (clean.size() > 2 && clean[0] == '0' &&
        (clean[1] == 'o' || clean[1] == 'O')) {
        return static_cast<double>(std::strtoull(clean.c_str() + 2, nullptr, 8));
    }
    return std::strtod(clean.c_str(), nullptr);
}

void TestScanner::lexNumber() {
    std::size_t b = p_;
    if (cur() == '0' && (at(1) == 'x' || at(1) == 'X' || at(1) == 'b' ||
                         at(1) == 'B' || at(1) == 'o' || at(1) == 'O')) {
        p_ += 2;
        while (isHex(cur()) || cur() == '_') ++p_;
    } else {
        while (isDigit(cur()) || cur() == '_') ++p_;
        if (cur() == '.' && isDigit(at(1))) {
            ++p_;
            while (isDigit(cur()) || cur() == '_') ++p_;
        }
        if (cur() == 'e' || cur() == 'E') {
            std::size_t save = p_;
            ++p_;
            if (cur() == '+' || cur() == '-') ++p_;
            if (isDigit(cur())) {
                while (isDigit(cur()) || cur() == '_') ++p_;
            } else {
                p_ = save;
            }
        }
    }
    Token t;
    t.kind = NumericLiteral;
    t.span = span(b, p_);
    t.lexeme = std::string_view(src_).substr(b, p_ - b);
    t.numberValue = parseNumberText(std::string(t.lexeme));
    t.precededByNewline = pendingNl_;
    pendingNl_ = false;
    toks_.push_back(std::move(t));
}

// Decode the common escapes; unknown escapes pass the char through.
std::string TestScanner::cook(const std::string& raw) const {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) { out.push_back(c); continue; }
        char n = raw[++i];
        switch (n) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'v': out.push_back('\v'); break;
            case '0': out.push_back('\0'); break;
            case '\\': out.push_back('\\'); break;
            case '\'': out.push_back('\''); break;
            case '"': out.push_back('"'); break;
            case '`': out.push_back('`'); break;
            case '$': out.push_back('$'); break;
            case 'x': {
                // \xHH (best effort)
                unsigned v = 0; int n2 = 0;
                while (n2 < 2 && isHex(cur())) { v = v * 16 + hexVal(raw[++i]); ++n2; }
                out.push_back(static_cast<char>(v));
                break;
            }
            case 'u': {
                // \uXXXX (best effort, no surrogate math)
                unsigned v = 0; int n2 = 0;
                while (n2 < 4 && i + 1 < raw.size() && isHex(raw[i + 1])) {
                    v = v * 16 + hexVal(raw[++i]); ++n2;
                }
                if (v < 0x80) out.push_back(static_cast<char>(v));
                else if (v < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (v >> 6)));
                    out.push_back(static_cast<char>(0x80 | (v & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (v >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((v >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (v & 0x3F)));
                }
                break;
            }
            default: out.push_back(n); break;
        }
    }
    return out;
}

unsigned TestScanner::hexVal(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    return static_cast<unsigned>(c - 'A' + 10);
}

void TestScanner::lexString() {
    std::size_t b = p_;
    char quote = cur();
    ++p_;
    bool unterminated = false;
    while (!eof() && cur() != quote) {
        if (cur() == '\\' && at(1) != '\0') { p_ += 2; continue; }
        if (cur() == '\n') { unterminated = true; break; }
        ++p_;
    }
    if (!eof() && cur() == quote) ++p_;
    else unterminated = true;

    std::string lexeme(src_.substr(b, p_ - b));
    std::string inner = lexeme.size() >= 2 ? lexeme.substr(1, lexeme.size() - 2)
                                           : std::string();

    Token t;
    t.kind = StringLiteral;
    t.span = span(b, p_);
    t.lexeme = std::string_view(src_).substr(b, p_ - b);
    t.stringValue = cook(inner);
    t.rawText = lexeme;
    t.unterminated = unterminated;
    t.precededByNewline = pendingNl_;
    pendingNl_ = false;
    toks_.push_back(std::move(t));
}

void TestScanner::lexTemplateOpen() {
    std::size_t b = p_;
    ++p_;  // consume `
    bool hasSub = false;
    std::string raw = readTemplateRaw(hasSub);
    Token t;
    t.span = span(b, p_);
    t.lexeme = std::string_view(src_).substr(b, p_ - b);
    t.rawText = raw;
    t.stringValue = cook(raw);
    t.precededByNewline = pendingNl_;
    pendingNl_ = false;
    if (!hasSub) {
        t.kind = TemplateString;
        toks_.push_back(std::move(t));
        return;
    }
    t.kind = TemplateHead;
    toks_.push_back(std::move(t));
    tmplStack_.push_back(braceDepth_);
}

// Reads the raw characters of a template chunk starting at p_, up to either
// `${` (consumed; sets hasSub) or the closing backtick (consumed).
std::string TestScanner::readTemplateRaw(bool& hasSub) {
    std::string raw;
    hasSub = false;
    while (!eof()) {
        char c = cur();
        if (c == '\\' && at(1) != '\0') {
            raw.push_back(c);
            raw.push_back(at(1));
            p_ += 2;
            continue;
        }
        if (c == '`') { ++p_; return raw; }
        if (c == '$' && at(1) == '{') {
            p_ += 2;
            hasSub = true;
            return raw;
        }
        raw.push_back(c);
        ++p_;
    }
    return raw;
}

void TestScanner::lexTemplateContinuation() {
    std::size_t b = p_;
    ++p_;  // consume the `}` that closes the substitution
    bool hasSub = false;
    std::string raw = readTemplateRaw(hasSub);
    Token t;
    t.kind = hasSub ? TemplateMiddle : TemplateTail;
    t.span = span(b, p_);
    t.lexeme = std::string_view(src_).substr(b, p_ - b);
    t.rawText = raw;
    t.stringValue = cook(raw);
    t.precededByNewline = pendingNl_;
    pendingNl_ = false;
    toks_.push_back(std::move(t));
    if (hasSub) tmplStack_.push_back(braceDepth_);
}

void TestScanner::lexRegExp() {
    std::size_t b = p_;
    ++p_;  // leading /
    bool inClass = false;
    while (!eof()) {
        char c = cur();
        if (c == '\\' && at(1) != '\0') { p_ += 2; continue; }
        if (c == '[') inClass = true;
        else if (c == ']') inClass = false;
        else if (c == '/' && !inClass) { ++p_; break; }
        else if (c == '\n') break;
        ++p_;
    }
    while (isIdentPart(cur())) ++p_;  // flags

    std::string lexeme(src_.substr(b, p_ - b));
    std::size_t last = lexeme.rfind('/');
    std::string pattern = last > 0 ? lexeme.substr(1, last - 1) : std::string();
    std::string flags = last != std::string::npos ? lexeme.substr(last + 1)
                                                  : std::string();

    Token t;
    t.kind = RegExpLiteral;
    t.span = span(b, p_);
    t.lexeme = std::string_view(src_).substr(b, p_ - b);
    t.stringValue = pattern;
    t.rawText = flags;
    t.precededByNewline = pendingNl_;
    pendingNl_ = false;
    toks_.push_back(std::move(t));
}

}  // namespace cp3test
