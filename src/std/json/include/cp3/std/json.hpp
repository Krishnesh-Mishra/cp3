// =============================================================================
// cp3/std/json.hpp - `JSON` (std/json) backing: `stringify` for scalar values.
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `JSON.stringify(value)` -> `::cp3::runtime::json::stringify(value)` for a
//   `number` / `string` / `boolean` / `null` value; overload resolution in the
//   generated C++ picks the right spelling.
//
//   NOT covered here: `JSON.stringify(undefined)` (parked ambiguity - JS
//   returns `undefined`, which a `string`-returning signature cannot express),
//   objects/arrays/indent (wave 3+), and `JSON.parse` (the stdlib table's
//   generics + runtime shape validation are a later wave).
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * Strings are quoted with JSON escaping: `"`, `\`, the short control
//     escapes (`\b \f \n \r \t`) and `\u00xx` for the rest of C0. Raw UTF-8
//     bytes pass through (JS keeps non-ASCII characters unescaped).
//   * Numbers reuse D9 `toString(double)`; NaN and +/-Infinity serialize as
//     `null` (ECMAScript JSON.stringify).
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <string>

#include "cp3/core/runtime.hpp"

namespace cp3::runtime::json {

inline std::string escapeString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            default: break;
        }
        if (byte < 0x20u) {
            static const char* const kHex = "0123456789abcdef";
            out += "\\u00";
            out.push_back(kHex[(byte >> 4) & 0xFu]);
            out.push_back(kHex[byte & 0xFu]);
            continue;
        }
        out.push_back(c);
    }
    return out;
}

inline std::string stringify(const std::string& value) {
    return "\"" + escapeString(value) + "\"";
}

inline std::string stringify(double value) {
    return std::isfinite(value) ? toString(value) : std::string("null");
}

inline std::string stringify(bool value) {
    return value ? std::string("true") : std::string("false");
}

inline std::string stringify(std::nullptr_t) { return std::string("null"); }

}  // namespace cp3::runtime::json
