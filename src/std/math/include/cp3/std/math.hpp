// =============================================================================
// cp3/std/math.hpp - `Math` (std/math) backing: JS-semantics scalar helpers.
//
// OWNERSHIP: the std/math family (src/std/math/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only, and mirror the STDLIB docs
// §04 Math signature table one-to-one.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `Math.<fn>(args)` -> `::cp3::runtime::math::<fn>(args)` for the unary and
//   binary helpers; the variadic `min`/`max` take an initializer_list, so a
//   call lowers to `::cp3::runtime::math::min({a, b})`. `Math.PI`/`Math.E`
//   lower to `::cp3::runtime::math::kPi`/`kE`.
//
// -----------------------------------------------------------------------------
// SEMANTICS (pinned by tests/cases/stdlib/math_*)
// -----------------------------------------------------------------------------
//   * round(x) is JS Math.round: half toward +Infinity (floor(x + 0.5)), so
//     round(-0.5) is -0 (prints "0") and round(-1.5) is -1.
//   * min/max accept any argument count and propagate NaN; min() is +Infinity
//     and max() is -Infinity (ECMAScript).
//   * random() returns [0, 1).
//   * gcd/lcm operate on `number` (docs §04): std::gcd/std::lcm over the
//     truncated magnitude, so gcd(0, 5) == 5 and lcm(0, 5) == 0.
//   * Everything else maps onto <cmath> directly.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <random>

namespace cp3::runtime::math {

inline constexpr double kPi = 3.14159265358979323846264338327950288;
inline constexpr double kE = 2.71828182845904523536028747135266250;

// [0, 1). One engine per thread, seeded once from std::random_device (a
// deterministic fallback seed would be acceptable: no case pins
// unpredictability, only the half-open range).
inline double random() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    static thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(engine);
}

inline double sqrt(double x) { return std::sqrt(x); }
inline double cbrt(double x) { return std::cbrt(x); }
inline double pow(double base, double exponent) {
    // Squaring is exactly the same IEEE-754 operation as the integral power
    // case and avoids dispatching to the general-purpose libm pow routine in
    // tight numeric loops.
    if (exponent == 2.0) return base * base;
    return std::pow(base, exponent);
}
inline double abs(double x) { return std::fabs(x); }
inline double floor(double x) { return std::floor(x); }
inline double ceil(double x) { return std::ceil(x); }

// JS Math.round: half toward +Infinity (not C's half-away-from-zero).
inline double round(double x) { return std::floor(x + 0.5); }

inline double trunc(double x) { return std::trunc(x); }
inline double log(double x) { return std::log(x); }
inline double log2(double x) { return std::log2(x); }
inline double log10(double x) { return std::log10(x); }
inline double sin(double x) { return std::sin(x); }
inline double cos(double x) { return std::cos(x); }
inline double tan(double x) { return std::tan(x); }

// Variadic min/max (ECMAScript): any NaN makes the result NaN; otherwise the
// smallest/largest argument wins. Empty input is +Infinity / -Infinity.
inline double min(std::initializer_list<double> values) {
    double result = std::numeric_limits<double>::infinity();
    for (double value : values) {
        if (std::isnan(value)) return value;
        if (value < result) result = value;
    }
    return result;
}

inline double max(std::initializer_list<double> values) {
    double result = -std::numeric_limits<double>::infinity();
    for (double value : values) {
        if (std::isnan(value)) return value;
        if (value > result) result = value;
    }
    return result;
}

namespace detail {

inline double max2(double a, double b) {
    if (std::isnan(a)) return a;
    if (std::isnan(b)) return b;
    if (a == 0.0 && b == 0.0) {
        return std::signbit(a) && std::signbit(b) ? -0.0 : 0.0;
    }
    return a > b ? a : b;
}

inline double min2(double a, double b) {
    if (std::isnan(a)) return a;
    if (std::isnan(b)) return b;
    if (a == 0.0 && b == 0.0) {
        return std::signbit(a) || std::signbit(b) ? -0.0 : 0.0;
    }
    return a < b ? a : b;
}

// A finite `double` truncated toward zero as a 64-bit integer; values outside
// the int64 range (or non-finite ones) are rejected.
inline bool toInt64(double x, std::int64_t& out) {
    if (!std::isfinite(x) || std::fabs(x) > 9.2e18) return false;
    out = static_cast<std::int64_t>(std::trunc(x));
    return true;
}

}  // namespace detail

inline double gcd(double a, double b) {
    std::int64_t ia = 0;
    std::int64_t ib = 0;
    if (!detail::toInt64(a, ia) || !detail::toInt64(b, ib)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(std::gcd(ia, ib));
}

inline double lcm(double a, double b) {
    std::int64_t ia = 0;
    std::int64_t ib = 0;
    if (!detail::toInt64(a, ia) || !detail::toInt64(b, ib)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(std::lcm(ia, ib));
}

// JS-equivalent to min(max(x, low), high): NaN propagates from any argument.
inline double clamp(double x, double low, double high) {
    return detail::min2(detail::max2(x, low), high);
}

inline double lerp(double start, double end, double t) {
    return start + (end - start) * t;
}

}  // namespace cp3::runtime::math
