// =============================================================================
// cp3/core/checked.hpp - checked numeric conversions for explicit casts.
//
// `parseTo<T>(value)` and `value as T` are the explicit conversion forms. A
// conversion that narrows a runtime value checks the value and throws a cp3
// Error instead of silently truncating or invoking undefined behaviour:
//
//   * a fractional value cannot become an integer;
//   * a value outside the target's range throws (64-bit bounds are checked
//     against 2^63 / 2^64, since `double` cannot represent MAX exactly);
//   * a floating target keeps C++'s ordinary conversion (including `f32`
//     rounding).
//
// Widening conversions pass through the same helper unchanged, so callers do
// not need to distinguish widening from narrowing at the emission site.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "cp3/core/error.hpp"

namespace cp3::runtime::checked {

namespace detail {

[[noreturn]] inline void outOfRange(double value, const char* target) {
    throw Thrown::make(std::string("value ") + ::cp3::runtime::toString(value) +
                       " is out of range for " + target);
}

template <typename T>
bool fitsInteger(double value) noexcept {
    if (!std::isfinite(value) || std::trunc(value) != value) return false;
    if constexpr (std::is_signed_v<T>) {
        if constexpr (sizeof(T) >= 8) {
            // [-2^63, 2^63): the upper bound is exclusive because 2^63 is
            // representable as a double but not as an int64_t.
            return value >= -9223372036854775808.0 &&
                   value < 9223372036854775808.0;
        } else {
            return value >= static_cast<double>(std::numeric_limits<T>::min()) &&
                   value <= static_cast<double>(std::numeric_limits<T>::max());
        }
    } else {
        if constexpr (sizeof(T) >= 8) {
            return value >= 0.0 && value < 18446744073709551616.0;
        } else {
            return value >= 0.0 &&
                   value <= static_cast<double>(std::numeric_limits<T>::max());
        }
    }
}

}  // namespace detail

/// Convert `value` to `T`, throwing when an integral target cannot represent it.
template <typename T>
T narrow(double value, const char* target) {
    if constexpr (std::is_integral_v<T>) {
        if (!detail::fitsInteger<T>(value)) detail::outOfRange(value, target);
    }
    return static_cast<T>(value);
}

// ---------------------------------------------------------------------------
// Checked native integer arithmetic (rule 5: overflow traps in every mode).
// Codegen emits these for native integer operands; `T` is the semantic result
// type (the usual-arithmetic common type), so a promoted `u8 + u8` is checked
// as `i32`.
// ---------------------------------------------------------------------------

[[noreturn]] inline void overflow(const char* op) {
    throw Thrown::make(std::string("integer overflow in '") + op + "'");
}

[[noreturn]] inline void divideByZero() {
    throw Thrown::make(std::string("integer division by zero"));
}

template <typename T, typename A, typename B>
T add(A a, B b) {
    const T x = static_cast<T>(a);
    const T y = static_cast<T>(b);
    if constexpr (std::is_signed_v<T>) {
        if ((y > 0 && x > std::numeric_limits<T>::max() - y) ||
            (y < 0 && x < std::numeric_limits<T>::min() - y)) {
            overflow("+");
        }
    } else {
        if (x > std::numeric_limits<T>::max() - y) overflow("+");
    }
    return static_cast<T>(x + y);
}

template <typename T, typename A, typename B>
T sub(A a, B b) {
    const T x = static_cast<T>(a);
    const T y = static_cast<T>(b);
    if constexpr (std::is_signed_v<T>) {
        if ((y < 0 && x > std::numeric_limits<T>::max() + y) ||
            (y > 0 && x < std::numeric_limits<T>::min() + y)) {
            overflow("-");
        }
    } else {
        if (x < y) overflow("-");
    }
    return static_cast<T>(x - y);
}

template <typename T, typename A, typename B>
T mul(A a, B b) {
    const T x = static_cast<T>(a);
    const T y = static_cast<T>(b);
#if defined(__GNUC__) || defined(__clang__)
    T result;
    if (__builtin_mul_overflow(x, y, &result)) overflow("*");
    return result;
#else
    if (x == 0 || y == 0) return 0;
    if constexpr (std::is_signed_v<T>) {
        if ((x == -1 && y == std::numeric_limits<T>::min()) ||
            (y == -1 && x == std::numeric_limits<T>::min())) {
            overflow("*");
        }
        if (y > 0) {
            if (x > std::numeric_limits<T>::max() / y ||
                x < std::numeric_limits<T>::min() / y) {
                overflow("*");
            }
        } else {
            if (x > std::numeric_limits<T>::min() / y ||
                x < std::numeric_limits<T>::max() / y) {
                overflow("*");
            }
        }
    } else {
        if (x > std::numeric_limits<T>::max() / y) overflow("*");
    }
    return static_cast<T>(x * y);
#endif
}

template <typename T, typename A, typename B>
T div(A a, B b) {
    const T x = static_cast<T>(a);
    const T y = static_cast<T>(b);
    if (y == 0) divideByZero();
    if constexpr (std::is_signed_v<T>) {
        if (x == std::numeric_limits<T>::min() && y == -1) overflow("/");
    }
    return static_cast<T>(x / y);
}

template <typename T, typename A, typename B>
T mod(A a, B b) {
    const T x = static_cast<T>(a);
    const T y = static_cast<T>(b);
    if (y == 0) divideByZero();
    if constexpr (std::is_signed_v<T>) {
        if (x == std::numeric_limits<T>::min() && y == -1) return 0;
    }
    return static_cast<T>(x % y);
}

template <typename T, typename A>
T neg(A a) {
    const T x = static_cast<T>(a);
    if constexpr (std::is_signed_v<T>) {
        if (x == std::numeric_limits<T>::min()) overflow("unary -");
    } else {
        if (x != 0) overflow("unary -");
    }
    return static_cast<T>(-x);
}

}  // namespace cp3::runtime::checked
