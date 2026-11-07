// =============================================================================
// cp3/src/core/runtime.cpp - the value/formatting surface of the generated-code ABI
// (see cp3/core/runtime.hpp): `Undefined`, the D9 number formatter, toString, print/
// println, and provenance.
// =============================================================================
#include "cp3/core/runtime.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>

namespace cp3::runtime {
namespace {

// Split a `std::to_chars` scientific form ("d[.ddd]e±xx") into:
//   * `digits`  - the mantissa with the decimal point removed (every digit),
//   * `exp`     - the explicit base-10 exponent (0 when absent),
//   * `fracLen` - digits after the mantissa's decimal point.
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
// as ECMAScript would (no sign; the caller adds it). The float specialisation
// keeps the shortest spelling of the `float` value: promoting `0.1f` to double
// first would print its longer double expansion.
template <typename Float>
std::string positiveNumberToString(Float value) {
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
        std::snprintf(fallback, sizeof(fallback), "%.*g",
                      std::numeric_limits<Float>::max_digits10,
                      static_cast<double>(value));
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

// ECMAScript Number::toString(value, 10) - DECISIONS D9.
template <typename Float>
std::string numberToString(Float value) {
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
std::string toString(float value) { return numberToString(value); }
std::string toString(const std::string& value) { return value; }
std::string toString(bool value) { return value ? "true" : "false"; }
std::string toString(std::nullptr_t) { return "null"; }
std::string toString(Undefined) { return "undefined"; }

double parseInt(const std::string& text) {
    const char* p = text.c_str();
    while (std::isspace(static_cast<unsigned char>(*p)) != 0) ++p;
    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        ++p;
    }
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    double value = 0.0;
    bool any = false;
    for (; *p != '\0'; ++p) {
        int digit = -1;
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (base == 16 && *p >= 'a' && *p <= 'f') {
            digit = *p - 'a' + 10;
        } else if (base == 16 && *p >= 'A' && *p <= 'F') {
            digit = *p - 'A' + 10;
        }
        if (digit < 0 || digit >= base) break;
        value = value * static_cast<double>(base) + static_cast<double>(digit);
        any = true;
    }
    if (!any) return std::numeric_limits<double>::quiet_NaN();
    return negative ? -value : value;
}

double toNumber(double value) { return value; }
double toNumber(bool value) { return value ? 1.0 : 0.0; }
double toNumber(std::nullptr_t) { return 0.0; }
double toNumber(Undefined) {
    return std::numeric_limits<double>::quiet_NaN();
}
double toNumber(const std::string& value) {
    // ECMAScript Number(string): trims whitespace, then the whole thing must
    // be a numeric literal (or empty -> 0); a partial literal is NaN.
    const char* p = value.c_str();
    while (std::isspace(static_cast<unsigned char>(*p))) ++p;
    const char* end = p + std::strlen(p);
    while (end > p && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    if (p == end) return 0.0;
    const std::string trimmed(p, end);
    if (trimmed == "Infinity" || trimmed == "+Infinity") {
        return std::numeric_limits<double>::infinity();
    }
    if (trimmed == "-Infinity") {
        return -std::numeric_limits<double>::infinity();
    }
    char* stop = nullptr;
    const double result = std::strtod(trimmed.c_str(), &stop);
    if (stop != nullptr && *stop != '\0') {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return result;
}
bool toBoolean(double value) {
    // NaN, 0 and -0 are falsy.
    return value != 0.0 && !std::isnan(value);
}
bool toBoolean(bool value) { return value; }
bool toBoolean(std::nullptr_t) { return false; }
bool toBoolean(Undefined) { return false; }
bool toBoolean(const std::string& value) { return !value.empty(); }
bool isNaN(double value) { return std::isnan(value); }
bool isFiniteNumber(double value) { return std::isfinite(value); }

double parseFloat(const std::string& text) {
    const char* p = text.c_str();
    while (std::isspace(static_cast<unsigned char>(*p)) != 0) ++p;
    const char* start = p;
    if (*p == '+' || *p == '-') ++p;
    bool anyDigit = false;
    while (*p >= '0' && *p <= '9') {
        ++p;
        anyDigit = true;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++p;
            anyDigit = true;
        }
    }
    if (!anyDigit) return std::numeric_limits<double>::quiet_NaN();
    const char* end = p;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        if (*q == '+' || *q == '-') ++q;
        bool exponentDigit = false;
        while (*q >= '0' && *q <= '9') {
            ++q;
            exponentDigit = true;
        }
        if (exponentDigit) end = q;
    }
    return std::strtod(std::string(start, end).c_str(), nullptr);
}

// print/println write through C stdio rather than std::cout on purpose: the
// iostream objects are initialized by a static initializer in every TU that
// includes <iostream>, and that machinery (plus the locale facets it builds) is
// pure startup cost for a program that only prints. std::cout with the default
// sync_with_stdio(true) is a stdio_sync_filebuf, so these calls land in exactly
// the same stdout buffer with the same ordering semantics.
void print(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
}

void println(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
}

const char* name() noexcept {
    return "cp3-runtime";
}

std::string version() {
    return "0.2.0";
}

}  // namespace cp3::runtime
