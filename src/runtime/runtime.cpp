// =============================================================================
// cp3/runtime.cpp — runtime support surface, including the generated-code ABI
// (see cp3/runtime.hpp).
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

// ECMAScript Number::toString(value, 10) — DECISIONS D9.
//
// Strategy: `std::to_chars` yields the shortest round-tripping decimal, which
// we re-express in the ECMAScript formatting rules. This is exact because the
// shortest round-tripping digit string is unique, and ES only decides *where*
// the decimal point / exponent goes.
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

    char buf[64];
    const std::to_chars_result res = std::to_chars(buf, buf + sizeof(buf), value);
    std::string repr;
    if (res.ec == std::errc()) {
        repr.assign(buf, res.ptr);
    } else {
        // Defensive fallback; should not happen for a 64-byte buffer.
        char fb[64];
        std::snprintf(fb, sizeof(fb), "%.17g", value);
        repr.assign(fb);
    }

    // Split into mantissa digits and an explicit base-10 exponent.
    const std::size_t epos = repr.find_first_of("eE");
    const std::string mantissa = epos == std::string::npos ? repr : repr.substr(0, epos);
    int exp10 = 0;
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
        exp10 = negative ? -magnitude : magnitude;
    }

    std::string intPart = "0";
    std::string fracPart;
    const std::size_t dot = mantissa.find('.');
    if (dot == std::string::npos) {
        intPart = mantissa;
    } else {
        intPart = mantissa.substr(0, dot);
        fracPart = mantissa.substr(dot + 1);
    }
    if (intPart.empty()) {
        intPart = "0";
    }

    const std::string raw = intPart + fracPart;  // every decimal digit
    const int fracLen = static_cast<int>(fracPart.size());
    int expAll = exp10 - fracLen;  // value == raw_as_integer * 10^expAll

    // Strip leading zeros (value unchanged) and trailing zeros (fold into the
    // exponent) so `digits` is the canonical significant digit string.
    std::size_t lead = 0;
    while (lead + 1 < raw.size() && raw[lead] == '0') {
        ++lead;
    }
    std::string digits = raw.substr(lead);
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
        ++expAll;
    }

    const int k = static_cast<int>(digits.size());  // significant digit count
    const int n = k + expAll;  // value == 0.digits * 10^n

    std::string out = sign;
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
