// =============================================================================
// cp3/src/runtime.cpp — the value/formatting surface of the generated-code ABI
// (see cp3/runtime.hpp): `Undefined`, the D9 number formatter, toString, print/
// println, and provenance.
// =============================================================================
#include "cp3/runtime.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>

namespace cp3::runtime {
namespace {

// Split a `std::to_chars` scientific form ("d[.ddd]e±xx") into:
//   * `digits`  — the mantissa with the decimal point removed (every digit),
//   * `exp`     — the explicit base-10 exponent (0 when absent),
//   * `fracLen` — digits after the mantissa's decimal point.
void parseScientific(const std::string& repr, std::string& digits, int& exp,
                     int& fracLen) {
    const std::size_t epos = repr.find_first_of("eE");
    const std::string mantissa = epos == std::string::npos ? repr : repr.substr(0, epos);
    exp = 0;
    if (epos != std::string::npos) {
        const char* first = repr.data() + epos + 1;
        const char* last = repr.data() + repr.size();
        bool negative = false;
        if (first != last && (*first == '+' || *first == '-')) {
            negative = (*first == '-');
            ++first;
        }
        int magnitude = 0;
        std::from_chars(first, last, magnitude);
        exp = negative ? -magnitude : magnitude;
    }
    const std::size_t dot = mantissa.find('.');
    fracLen = dot == std::string::npos ? 0 : static_cast<int>(mantissa.size() - dot - 1);
    digits.clear();
    digits.reserve(mantissa.size());
    for (char c : mantissa) {
        if (c != '.') {
            digits += c;
        }
    }
}

// Canonicalize `raw` (a mantissa without its point): strip leading zeros (the
// value is unchanged) and trailing zeros (folding each into `expAll`), so the
// returned string is the significant digit sequence and
// `value == digits_as_integer * 10^expAll`.
std::string canonicalDigits(std::string raw, int& expAll) {
    std::size_t lead = 0;
    while (lead + 1 < raw.size() && raw[lead] == '0') {
        ++lead;
    }
    std::string digits = raw.substr(lead);
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
        ++expAll;
    }
    return digits;
}

// ECMAScript Number::toString step 10: choose the decimal point / exponent
// position for a value equal to `0.digits * 10^n`.
std::string placeDecimalPoint(const std::string& digits, int n) {
    const int k = static_cast<int>(digits.size());
    std::string out;
    if (k <= n && n <= 21) {
        // Integer with trailing zeros: 18, 100, 1.2345678901234568e20.
        out += digits;
        out.append(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        // Decimal point inside the digits: 1.5, 123.456.
        out += digits.substr(0, static_cast<std::size_t>(n));
        out += '.';
        out += digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        // Leading "0." with padding zeros: 0.5, 0.001.
        out += "0.";
        out.append(static_cast<std::size_t>(-n), '0');
        out += digits;
    } else {
        // Exponential form with an explicit sign: 1e+21, 1e-7.
        out += digits.substr(0, 1);
        if (k > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        const int e = n - 1;
        out += (e >= 0 ? '+' : '-');
        out += std::to_string(e >= 0 ? e : -e);
    }
    return out;
}

// The shortest round-tripping decimal of a finite, positive `value`, formatted
// as ECMAScript would (no sign; the caller adds it).
std::string positiveNumberToString(double value) {
    char buf[64];
    // The scientific shortest form is unique and exact; the default/general form
    // is not shortest on every libstdc++, so we re-place the point ourselves.
    const std::to_chars_result res =
        std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::scientific);
    std::string repr;
    if (res.ec == std::errc()) {
        repr.assign(buf, res.ptr);
    } else {
        // Defensive fallback; should not happen for a 64-byte buffer.
        char fallback[64];
        std::snprintf(fallback, sizeof(fallback), "%.17g", value);
        repr.assign(fallback);
    }
    std::string digits;
    int exp10 = 0;
    int fracLen = 0;
    parseScientific(repr, digits, exp10, fracLen);
    if (digits.empty()) {
        digits = "0";
    }
    int expAll = exp10 - fracLen;  // value == digits_as_integer * 10^expAll
    digits = canonicalDigits(digits, expAll);
    return placeDecimalPoint(digits, static_cast<int>(digits.size()) + expAll);
}

// ECMAScript Number::toString(value, 10) — DECISIONS D9.
std::string numberToString(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (value == 0.0) {
        return "0";  // +0 and -0 both print "0"
    }
    std::string sign;
    if (value < 0.0) {
        sign = "-";
        value = -value;
    }
    if (std::isinf(value)) {
        return sign + "Infinity";
    }
    return sign + positiveNumberToString(value);
}

}  // namespace

std::string toString(double value) { return numberToString(value); }
std::string toString(const std::string& value) { return value; }
std::string toString(bool value) { return value ? "true" : "false"; }
std::string toString(std::nullptr_t) { return "null"; }
std::string toString(Undefined) { return "undefined"; }

void print(const std::string& text) { std::cout << text; }

void println(const std::string& text) { std::cout << text << '\n'; }

const char* name() noexcept {
    return "cp3-runtime";
}

std::string version() {
    return "0.1.0";
}

}  // namespace cp3::runtime
