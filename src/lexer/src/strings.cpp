// =============================================================================
// src/lexer/src/strings.cpp - string literals and escape sequences.
//
// Implements `Scanner::scanString` / `scanEscape` and the escape helpers
// (declared in detail/scanner.h). Both quote styles are supported; the cooked value is
// stored in `Token::stringValue`. Malformed escapes report CP3106 and a raw
// newline before the closing quote reports CP3102 (with `unterminated` set),
// but the literal is still emitted for recovery (STYLE.md §6).
// =============================================================================
#include "detail/scanner.h"

#include <cstdint>
#include <string>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {
namespace {

int hexVal(unsigned char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

}  // namespace

// Decodes one escape sequence beginning at the backslash; appends the cooked
// result to `out`. Reports CP3106 for malformed escapes.
void Scanner::scanEscape(std::string& out, bool inTemplate) {
    const std::uint32_t escStart = static_cast<std::uint32_t>(i_);
    ++i_;  // consume backslash
    if (i_ >= n_) {
        errorAt(diagcode::kInvalidEscape, escStart, static_cast<std::uint32_t>(i_),
                "invalid escape sequence at end of file");
        return;
    }
    const unsigned char c = static_cast<unsigned char>(text_[i_]);
    if (scanSimpleEscape(out, c, inTemplate)) return;
    if (c >= '0' && c <= '9') {
        scanNumericEscape(out, c, escStart);
        return;
    }
    if (c == 'x') {
        scanHexEscape(out, escStart);
        return;
    }
    if (c == 'u') {
        scanUnicodeEscape(out, escStart);
        return;
    }

    // Line continuation: backslash + line terminator contributes nothing.
    if (c == '\n' || c == '\r') {
        ++i_;
        if (c == '\r' && i_ < n_ && text_[i_] == '\n') ++i_;
        return;
    }
    if (c == 0xE2 && i_ + 2 < n_ &&
        static_cast<unsigned char>(text_[i_ + 1]) == 0x80 &&
        (static_cast<unsigned char>(text_[i_ + 2]) == 0xA8 ||
         static_cast<unsigned char>(text_[i_ + 2]) == 0xA9)) {
        i_ += 3;
        return;
    }

    // Unknown escape character: reject, but recover by emitting it.
    errorAt(diagcode::kInvalidEscape, escStart,
            static_cast<std::uint32_t>(i_ + 1), "invalid escape sequence");
    out.push_back(static_cast<char>(c));
    ++i_;
}

// Single-character escapes. Returns true (consuming the character) when `c` is a
// complete escape; false leaves `c` for the caller's recovery path.
bool Scanner::scanSimpleEscape(std::string& out, unsigned char c, bool inTemplate) {
    switch (c) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'v': out.push_back('\v'); break;
        case '\'': out.push_back('\''); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case '`':
            if (inTemplate) { out.push_back('`'); break; }
            return false;
        case '$':
            if (inTemplate) { out.push_back('$'); break; }
            return false;
        default:
            return false;
    }
    ++i_;
    return true;
}

// `\0` (null) and the legacy octal escapes `\1`-`\9`, which cp3 rejects.
void Scanner::scanNumericEscape(std::string& out, unsigned char c, std::uint32_t escStart) {
    if (c == '0') {
        if (i_ + 1 < n_ && isDecDigit(static_cast<unsigned char>(text_[i_ + 1]))) {
            ++i_;
            while (i_ < n_ && isDecDigit(static_cast<unsigned char>(text_[i_]))) ++i_;
            errorAt(diagcode::kInvalidEscape, escStart,
                    static_cast<std::uint32_t>(i_),
                    "legacy octal escape sequences are not allowed");
            return;
        }
        out.push_back('\0');
        ++i_;
        return;
    }
    ++i_;
    while (i_ < n_ && isDecDigit(static_cast<unsigned char>(text_[i_]))) ++i_;
    errorAt(diagcode::kInvalidEscape, escStart, static_cast<std::uint32_t>(i_),
            "legacy octal escape sequences are not allowed");
}

// `\xHH`.
void Scanner::scanHexEscape(std::string& out, std::uint32_t escStart) {
    ++i_;
    int count = 0;
    unsigned v = 0;
    while (count < 2 && i_ < n_ &&
           isHexDigit(static_cast<unsigned char>(text_[i_]))) {
        v = v * 16 + static_cast<unsigned>(hexVal(
                         static_cast<unsigned char>(text_[i_])));
        ++i_;
        ++count;
    }
    if (count < 2) {
        errorAt(diagcode::kInvalidEscape, escStart,
                static_cast<std::uint32_t>(i_), "invalid hexadecimal escape sequence");
    }
    out.push_back(static_cast<char>(v));
}

// `\uHHHH` and `\u{H...}`.
void Scanner::scanUnicodeEscape(std::string& out, std::uint32_t escStart) {
    ++i_;
    if (i_ < n_ && text_[i_] == '{') {
        ++i_;
        std::uint32_t cp = 0;
        int count = 0;
        while (i_ < n_ && isHexDigit(static_cast<unsigned char>(text_[i_]))) {
            cp = cp * 16 + static_cast<std::uint32_t>(hexVal(
                               static_cast<unsigned char>(text_[i_])));
            ++i_;
            ++count;
        }
        if (i_ < n_ && text_[i_] == '}') {
            ++i_;  // consume '}'
            if (count == 0) {
                errorAt(diagcode::kInvalidEscape, escStart,
                        static_cast<std::uint32_t>(i_),
                        "invalid Unicode escape sequence");
            } else if (cp > 0x10FFFF) {
                errorAt(diagcode::kInvalidEscape, escStart,
                        static_cast<std::uint32_t>(i_),
                        "Unicode code point is out of range");
            } else {
                appendUtf8(out, cp);
            }
        } else {
            errorAt(diagcode::kInvalidEscape, escStart,
                    static_cast<std::uint32_t>(i_), "invalid Unicode escape sequence");
        }
        return;
    }

    std::uint32_t cp = 0;
    int count = 0;
    while (count < 4 && i_ < n_ &&
           isHexDigit(static_cast<unsigned char>(text_[i_]))) {
        cp = cp * 16 + static_cast<std::uint32_t>(hexVal(
                           static_cast<unsigned char>(text_[i_])));
        ++i_;
        ++count;
    }
    if (count < 4) {
        errorAt(diagcode::kInvalidEscape, escStart,
                static_cast<std::uint32_t>(i_), "invalid Unicode escape sequence");
    } else {
        appendUtf8(out, cp);
    }
}

void Scanner::scanString(char quote) {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    ++i_;  // opening quote
    std::string value;
    bool terminated = false;
    while (i_ < n_) {
        const unsigned char c = static_cast<unsigned char>(text_[i_]);
        if (c == static_cast<unsigned char>(quote)) {
            ++i_;
            terminated = true;
            break;
        }
        if (c == '\\') {
            scanEscape(value, false);
            continue;
        }
        if (isLineTermAt(i_)) break;  // raw newline ends the literal
        value.push_back(static_cast<char>(c));
        ++i_;
    }
    Token t;
    t.kind = TokenKind::StringLiteral;
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    t.stringValue = std::move(value);
    if (!terminated) {
        t.unterminated = true;
        errorAt(diagcode::kUnterminatedString, start, static_cast<std::uint32_t>(i_),
                "unterminated string literal");
    }
    commit(std::move(t));
}

}  // namespace cp3
