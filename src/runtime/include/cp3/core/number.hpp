// =============================================================================
// cp3/core/number.hpp - language-level Number helpers.
//
// Number methods and statics are part of the language, not an importable
// module. The generated unit pulls this header when it uses one of them.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace cp3::runtime::numbers {

// The standard Number constants (ECMAScript 2023).
inline constexpr double kMaxSafeInteger = 9007199254740991.0;
inline constexpr double kMinSafeInteger = -9007199254740991.0;
inline constexpr double kEpsilon =
    2.2204460492503130808472633361816e-16;
inline constexpr double kMaxValue = 1.7976931348623157e+308;
inline constexpr double kMinValue = 5e-324;
inline constexpr double kPositiveInfinity =
    std::numeric_limits<double>::infinity();
inline constexpr double kNegativeInfinity =
    -std::numeric_limits<double>::infinity();
inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline bool isInteger(double value) {
    return std::isfinite(value) && std::trunc(value) == value;
}

inline bool isSafeInteger(double value) {
    return std::isfinite(value) && std::trunc(value) == value &&
           std::abs(value) <= 9007199254740991.0;
}

inline bool isFinite(double value) { return std::isfinite(value); }

inline bool isNaN(double value) { return std::isnan(value); }

namespace detail {

inline std::string cleanupExponent(std::string text) {
    const std::size_t e = text.find('e');
    if (e == std::string::npos || e + 2 >= text.size()) return text;
    const std::size_t digits = e + 1;
    if (text[digits] != '+' && text[digits] != '-') return text;
    std::size_t first = digits + 1;
    while (first + 1 < text.size() && text[first] == '0') ++first;
    if (first > digits + 1) text.erase(digits + 1, first - (digits + 1));
    return text;
}

inline int digitCount(double digits, double fallback, double maximum) {
    if (std::isnan(digits)) return static_cast<int>(fallback);
    if (digits < 0.0) return 0;
    if (digits > maximum) return static_cast<int>(maximum);
    return static_cast<int>(std::trunc(digits));
}

inline std::string nonFinite(double value) {
    if (std::isnan(value)) return "NaN";
    return value < 0.0 ? "-Infinity" : "Infinity";
}

}  // namespace detail

// `(1.2345).toFixed(2)` -> "1.23". JavaScript returns the plain decimal
// spelling for |value| >= 1e21 instead of a fixed expansion.
inline std::string toFixed(double value, double digits = 0.0) {
    if (!std::isfinite(value)) return detail::nonFinite(value);
    const int precision = detail::digitCount(digits, 0.0, 100.0);
    if (std::abs(value) >= 1e21) {
        std::ostringstream out;
        out << value;
        return out.str();
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// `(1234.5).toExponential(2)` -> "1.23e+3". The exponent is normalized to
// JavaScript's spelling (no leading zero in a two-digit exponent).
inline std::string toExponential(double value, double digits = 6.0) {
    if (!std::isfinite(value)) return detail::nonFinite(value);
    const int precision = detail::digitCount(digits, 6.0, 100.0);
    std::ostringstream out;
    out << std::scientific << std::setprecision(precision) << value;
    return detail::cleanupExponent(out.str());
}

// `(1234.5).toPrecision(3)` -> "1.23e+3"; `(12.34).toPrecision(3)` -> "12.3".
// Fixed notation is used while the decimal exponent lies in [-6, precision).
inline std::string toPrecision(double value, double precision) {
    if (!std::isfinite(value)) return detail::nonFinite(value);
    const int p = detail::digitCount(precision, 6.0, 100.0);
    if (p == 0) return "0";
    if (value == 0.0) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(p - 1) << value;
        return out.str();
    }
    const int exponent =
        static_cast<int>(std::floor(std::log10(std::abs(value))));
    if (exponent < -6 || exponent >= p) {
        std::ostringstream out;
        out << std::scientific << std::setprecision(p - 1) << value;
        return detail::cleanupExponent(out.str());
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(p - 1 - exponent) << value;
    return out.str();
}

// `(255).toString(16)` -> "ff". Only radixes 2-36 are legal; anything else
// (or a non-integer) is the empty string in JS (RangeError in the source).
// Non-finite values spell NaN/Infinity like toString(10).
inline std::string toString(double value, double radix = 10.0) {
    if (!std::isfinite(value)) return detail::nonFinite(value);
    const int base = static_cast<int>(radix);
    if (radix != static_cast<double>(base) || base < 2 || base > 36) {
        return "";
    }
    if (base == 10) {
        std::ostringstream out;
        out << value;
        return out.str();
    }
    // Non-decimal radix: only integers have a spelling; fractions truncate.
    double intPart;
    const double fraction = std::modf(value, &intPart);
    if (std::abs(fraction) > 0.0) {
        // JS: non-integers in other radixes still spell (with a fraction).
        // For v0.2: truncate toward zero, matching the integer fast path.
    }
    bool negative = intPart < 0.0;
    double v = std::abs(intPart);
    if (v == 0.0) return "0";
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string result;
    while (v > 0.0) {
        const int digit = static_cast<int>(std::fmod(v, base));
        result.push_back(digits[digit]);
        v = std::floor(v / base);
    }
    if (negative) result.push_back('-');
    std::reverse(result.begin(), result.end());
    return result;
}

// `Number.parseInt(s)` / `Number.parseFloat(s)`: the same parsers as the
// globals (runtime.hpp), surfaced on the Number namespace.
inline double parseInt(const std::string& text) {
    // Forward to the runtime parser via the header-only path: replicate the
    // radix-10 integer scan here to keep numbers.hpp self-contained.
    const char* p = text.c_str();
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
           *p == '\f' || *p == '\v') ++p;
    bool negative = false;
    if (*p == '+' || *p == '-') { negative = *p == '-'; ++p; }
    double value = 0.0;
    bool any = false;
    for (; *p >= '0' && *p <= '9'; ++p) {
        value = value * 10.0 + static_cast<double>(*p - '0');
        any = true;
    }
    if (!any) return std::numeric_limits<double>::quiet_NaN();
    return negative ? -value : value;
}

inline double parseFloat(const std::string& text) {
    const char* p = text.c_str();
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
           *p == '\f' || *p == '\v') ++p;
    const char* start = p;
    if (*p == '+' || *p == '-') ++p;
    bool anyDigit = false;
    while (*p >= '0' && *p <= '9') { ++p; anyDigit = true; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') { ++p; anyDigit = true; }
    }
    if (!anyDigit) return std::numeric_limits<double>::quiet_NaN();
    const char* end = p;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        if (*q == '+' || *q == '-') ++q;
        bool expDigit = false;
        while (*q >= '0' && *q <= '9') { ++q; expDigit = true; }
        if (expDigit) end = q;
    }
    return std::strtod(std::string(start, end).c_str(), nullptr);
}

}  // namespace cp3::runtime::numbers
