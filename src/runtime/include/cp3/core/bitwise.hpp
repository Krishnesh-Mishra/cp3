// =============================================================================
// cp3/core/bitwise.hpp - ECMAScript bitwise/shift backing for generated code.
//
// ADDITIVE to the generated-code ABI (D14): every entry point is `inline`,
// header-only, so the required runtime translation-unit set is unchanged.
//
// The ECMAScript semantics (the corpus pins them):
//   * both operands are converted with ToInt32 / ToUint32: NaN, +-Infinity and
//     -0 become +0; any other finite value is truncated toward zero and taken
//     modulo 2^32 (the low 32 bits).
//   * `& | ^ ~ << >>` produce a signed 32-bit result, `>>>` an unsigned one
//     (so `-1 >>> 0` is 4294967295, not -1).
//   * the shift count is `ToUint32(rhs) & 31`, so `1 << 33` is `1 << 1`.
//   * `>>` duplicates the sign bit (arithmetic shift), `>>>` fills with zeros.
//
// cp3 `number` is a double and every int32/uint32 value is exactly
// representable in it, so every helper returns the result as a double.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace cp3::runtime::bitwise {

// ToInt32: truncate toward zero, then reduce modulo 2^32 into [-2^31, 2^31).
inline std::int32_t toInt32(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0;
    double m = std::fmod(std::trunc(v), 4294967296.0);
    if (m >= 2147483648.0) m -= 4294967296.0;
    if (m < -2147483648.0) m += 4294967296.0;
    return static_cast<std::int32_t>(m);
}

// ToUint32: the same low 32 bits, as an unsigned value (the `>>>` output and
// the shift count are uint32).
inline std::uint32_t toUint32(double v) {
    return static_cast<std::uint32_t>(toInt32(v));
}

inline std::uint32_t shiftCount(double v) { return toUint32(v) & 31u; }

inline double band(double a, double b) {
    return static_cast<double>(toInt32(a) & toInt32(b));
}

inline double bor(double a, double b) {
    return static_cast<double>(toInt32(a) | toInt32(b));
}

inline double bxor(double a, double b) {
    return static_cast<double>(toInt32(a) ^ toInt32(b));
}

// `~a` (the bitwise complement of the ToInt32).
inline double bnot(double a) {
    return static_cast<double>(~toInt32(a));
}

// `a << count`: the uint32 shift is done unsigned, then reinterpreted as the
// signed 32-bit result (`1 << 31` is -2147483648).
inline double shl(double a, double b) {
    const std::uint32_t bits =
        static_cast<std::uint32_t>(toInt32(a)) << shiftCount(b);
    return static_cast<double>(static_cast<std::int32_t>(bits));
}

// `a >> count`: an arithmetic shift (the sign bit is replicated). The fill is
// only applied for a negative ToInt32 value; count 0 is a no-op.
inline double shr(double a, double b) {
    const std::int32_t value = toInt32(a);
    const std::uint32_t count = shiftCount(b);
    std::uint32_t shifted = static_cast<std::uint32_t>(value) >> count;
    if (value < 0 && count != 0) {
        shifted |= ~std::uint32_t(0) << (32u - count);
    }
    return static_cast<double>(static_cast<std::int32_t>(shifted));
}

// `a >>> count`: a logical (zero-filling) shift of the ToUint32 value.
inline double ushr(double a, double b) {
    return static_cast<double>(toUint32(a) >> shiftCount(b));
}

}  // namespace cp3::runtime::bitwise
