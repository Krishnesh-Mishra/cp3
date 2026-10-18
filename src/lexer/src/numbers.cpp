// =============================================================================
// src/lexer/src/numbers.cpp - numeric literal scanning.
//
// Implements `Scanner::scanNumber` and its helpers (declared in scanner.h):
// decimal/float literals with `_` separators and an exponent, plus the
// `0x`/`0b`/`0o` radix-prefixed forms. Malformed literals (bad separator, empty
// radix digits, a trailing identifier) report CP3105 and still yield a
// NumericLiteral token for recovery (STYLE.md §6).
// =============================================================================
#include "scanner.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {
namespace {

bool isBinDigit(unsigned char c) noexcept { return c == '0' || c == '1'; }
bool isOctDigit(unsigned char c) noexcept { return c >= '0' && c <= '7'; }

}  // namespace

// Scans a run of digits of one radix allowing `_` separators strictly *between*
// two digits; appends accepted digits to `clean`.
bool Scanner::scanDigitRun(bool (*isDigit)(unsigned char), std::string& clean, bool& sepError) {
    bool saw = false;
    bool prevDigit = false;
    while (i_ < n_) {
        const unsigned char c = static_cast<unsigned char>(text_[i_]);
        if (isDigit(c)) {
            clean.push_back(static_cast<char>(c));
            saw = true;
            prevDigit = true;
            ++i_;
        } else if (c == '_') {
            const bool ok = prevDigit && i_ + 1 < n_ &&
                            isDigit(static_cast<unsigned char>(text_[i_ + 1]));
            if (!ok) sepError = true;
            ++i_;
            prevDigit = false;
        } else {
            break;
        }
    }
    return saw;
}

void Scanner::scanNumber() {
    const std::uint32_t start = static_cast<std::uint32_t>(i_);
    bool error = false;
    double value = 0.0;

    const bool hasPrefix =
        text_[i_] == '0' && i_ + 1 < n_ &&
        (text_[i_ + 1] == 'x' || text_[i_ + 1] == 'X' || text_[i_ + 1] == 'b' ||
         text_[i_ + 1] == 'B' || text_[i_ + 1] == 'o' || text_[i_ + 1] == 'O');

    if (hasPrefix) {
        scanRadixNumber(error, value);
    } else {
        scanDecimalNumber(error, value);
    }

    Token t;
    t.kind = TokenKind::NumericLiteral;
    t.span = span(start, static_cast<std::uint32_t>(i_));
    t.lexeme = file_.slice(t.span);
    t.numberValue = value;
    if (error) {
        errorAt(diagcode::kInvalidNumericLiteral, t.span.begin.offset,
                t.span.end.offset, "invalid numeric literal");
    }
    commit(std::move(t));
}

// `0x`/`0b`/`0o` forms. A digit that is not valid for the radix, or an
// identifier that immediately follows, is malformed (`0b12`, `0x1Fg`).
void Scanner::scanRadixNumber(bool& error, double& value) {
    const char p = text_[i_ + 1];
    i_ += 2;
    std::string digits;
    bool sepError = false;
    bool seen = false;
    if (p == 'x' || p == 'X') {
        seen = scanDigitRun(isHexDigit, digits, sepError);
    } else if (p == 'b' || p == 'B') {
        seen = scanDigitRun(isBinDigit, digits, sepError);
    } else {
        seen = scanDigitRun(isOctDigit, digits, sepError);
    }
    if (!seen) error = true;
    if (sepError) error = true;

    if (p == 'x' || p == 'X') {
        if (!digits.empty()) {
            const std::string hexText = "0x" + digits;
            value = std::strtod(hexText.c_str(), nullptr);
        }
    } else {
        const int radix = (p == 'b' || p == 'B') ? 2 : 8;
        for (char d : digits) value = value * radix + (d - '0');
    }

    if (i_ < n_ && (isIdentStart(static_cast<unsigned char>(text_[i_])) ||
                    isDecDigit(static_cast<unsigned char>(text_[i_])))) {
        error = true;
        while (i_ < n_ &&
               (isIdentContinue(static_cast<unsigned char>(text_[i_])) ||
                text_[i_] == '_')) {
            ++i_;
        }
    }
}

// Decimal/float form. Legacy octal (`08`, `0_1`) is rejected; cp3 has no
// implicit octal.
void Scanner::scanDecimalNumber(bool& error, double& value) {
    if (text_[i_] == '0' && i_ + 1 < n_ &&
        (isDecDigit(static_cast<unsigned char>(text_[i_ + 1])) || text_[i_ + 1] == '_')) {
        error = true;
    }

    std::string clean;
    bool sepError = false;
    scanDigitRun(isDecDigit, clean, sepError);

    if (i_ < n_ && text_[i_] == '.' &&
        !(i_ + 1 < n_ && text_[i_ + 1] == '.')) {
        clean.push_back('.');
        ++i_;
        scanDigitRun(isDecDigit, clean, sepError);
    }

    if (i_ < n_ && (text_[i_] == 'e' || text_[i_] == 'E')) {
        ++i_;
        std::string exp;
        if (i_ < n_ && (text_[i_] == '+' || text_[i_] == '-')) {
            exp.push_back(text_[i_]);
            ++i_;
        }
        std::string expDigits;
        bool expSep = false;
        const bool hadDigits = scanDigitRun(isDecDigit, expDigits, expSep);
        if (!hadDigits) {
            error = true;
        } else {
            clean.push_back('e');
            clean += exp;
            clean += expDigits;
        }
        if (expSep) error = true;
    }

    if (sepError) error = true;

    if (!clean.empty()) value = std::strtod(clean.c_str(), nullptr);

    if (i_ < n_ && isIdentStart(static_cast<unsigned char>(text_[i_]))) {
        error = true;
        while (i_ < n_ &&
               (isIdentContinue(static_cast<unsigned char>(text_[i_])) ||
                text_[i_] == '_')) {
            ++i_;
        }
    }
}

}  // namespace cp3
