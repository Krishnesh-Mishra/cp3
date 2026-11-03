// =============================================================================
// cp3/core/number.hpp - language-level Number helpers.
//
// Number methods and statics are part of the language, not an importable
// module. The generated unit pulls this header when it uses one of them.
// =============================================================================
#pragma once

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

}  // namespace cp3::runtime::numbers
