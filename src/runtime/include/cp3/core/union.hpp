// =============================================================================
// cp3/core/union.hpp - primitive cp3 unions at the generated-code ABI.
//
// ADDITIVE to the generated-code ABI (D14): header-only, every entry inline or
// a template. A cp3 union over primitives (`number | string`, and the wider
// `number | string | boolean`) lowers to `std::variant<double, std::string,
// bool>` (canonical member order; only the members present are emitted). These
// helpers are what `typeof`, `===`/`==` and truthiness of such a value go
// through:
//   * `typeof` reports the held alternative;
//   * `===` requires the held alternative to match (no coercion);
//   * `==` coerces through ToNumber (ECMAScript §7.2.14) - the corpus pins
//     `x == "1"` true while `x === "1"` is false when x holds the number 1;
//   * truthiness follows the held alternative.
//
// ToNumber of a string is the usual simplification of ES §7.1.4.1: the string
// is trimmed, then parsed by strtod with full consumption (so `"1"` is 1,
// `""`/whitespace is 0, `"0x10"` is 16, and anything with trailing junk is
// NaN). The ES-only spellings that differ from strtod (`"Infinity"` only,
// no sign before a hex prefix, no `inf`/`nan` words) are not distinguished;
// no corpus case depends on them.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

#include "cp3/core/value.hpp"

namespace cp3::runtime::unions {

// ToNumber: a double is itself, a boolean is 1/0, a string goes through the
// trimmed strtod parse above.
inline double toNumber(double v) { return v; }
inline double toNumber(bool v) { return v ? 1.0 : 0.0; }

inline double toNumber(const std::string& raw) {
    const char* const whitespace = " \t\n\r\f\v";
    const std::size_t begin = raw.find_first_not_of(whitespace);
    if (begin == std::string::npos) return 0.0;  // empty / whitespace-only
    const std::size_t end = raw.find_last_not_of(whitespace);
    const std::string text = raw.substr(begin, end - begin + 1);
    const char* const first = text.c_str();
    char* last = nullptr;
    const double value = std::strtod(first, &last);
    if (last == first || *last != '\0') {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return value;
}

// `typeof` of a union value: report the held alternative.
template <class... Ts>
inline std::string typeOf(const std::variant<Ts...>& v) {
    return std::visit(
        [](const auto& held) { return ::cp3::runtime::typeOf(held); }, v);
}

// Rendering of one alternative: the generated object-shape renderer visits the
// variant with this and each alternative renders through its own formatter.
inline std::string textOf(double v) { return ::cp3::runtime::toString(v); }
inline std::string textOf(const std::string& v) {
    return ::cp3::runtime::toString(v);
}
inline std::string textOf(bool v) { return ::cp3::runtime::toString(v); }
// A reference-object alternative renders through its pointee.
template <class T>
inline auto textOf(const Ref<T>& v) -> decltype(v->cp3ToText()) {
    return v->cp3ToText();
}

// JS truthiness of one alternative (the generated `cp3_truthy` cannot be named
// here: it lives in the generated unit's anonymous namespace).
inline bool jsTruthy(double v) { return v != 0.0 && !std::isnan(v); }
inline bool jsTruthy(const std::string& v) { return !v.empty(); }
inline bool jsTruthy(bool v) { return v; }

template <class... Ts>
inline bool truthy(const std::variant<Ts...>& v) {
    return std::visit([](const auto& held) { return jsTruthy(held); }, v);
}

// `===`: the alternatives must match; comparing a double against a string is
// false even when the string spells the same numeral.
template <class... Ts, class T>
inline bool strictEq(const std::variant<Ts...>& v, const T& other) {
    return std::visit(
        [&other](const auto& held) -> bool {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::decay_t<T>>) {
                return held == other;
            } else {
                return false;
            }
        },
        v);
}

// `==`: same alternative compares directly, otherwise both sides are coerced
// with ToNumber (NaN never equals anything, so a non-numeric string is false
// against any number).
template <class... Ts, class T>
inline bool looseEq(const std::variant<Ts...>& v, const T& other) {
    return std::visit(
        [&other](const auto& held) -> bool {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::decay_t<T>>) {
                return held == other;
            } else {
                return toNumber(held) == toNumber(other);
            }
        },
        v);
}

}  // namespace cp3::runtime::unions
