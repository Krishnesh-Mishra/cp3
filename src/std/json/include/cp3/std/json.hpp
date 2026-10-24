// =============================================================================
// cp3/std/json.hpp - `JSON` (std/json) backing: stringify + parse umbrella.
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only. This is the include the
// generated C++ names (`#include "cp3/std/json.hpp"`); the dynamic value model
// lives in cp3/std/json_value.hpp and the reader in cp3/std/json_parse.hpp.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `JSON.stringify(value)`        -> `::cp3::runtime::json::stringify(value)`
//     scalars use the dedicated overloads; an array value uses the `Array<E>`
//     template; a composite literal lowers through the dynamic value tree
//     (`arrayValue` / `objectValue` / `value`) into `stringify(Value, indent)`.
//   `JSON.parse<number|string|boolean>(text)` -> `parseNumber` / `parseString` /
//     `parseBoolean`: parse the text and throw `ParseError` on invalid JSON or
//     a kind mismatch.
//   `JSON.parse<number[]>(text)`   -> `parseArray<double>(text)` (likewise
//     `std::string` / `bool`); `parseArray<T>` uses `convertElement<T>`.
//   `JSON.parse<User>(text)`       -> `parseRecord(text, builder)`: the builder
//     is generated per interface (`cp3_out.id = numberField(cp3_f, "id"); ...`)
//     and throws `ParseError` on a missing field or a field type mismatch. The
//     C++ struct and builder exist only for the interfaces actually used as
//     `JSON.parse<T>` (usage-based reification, STDLIB docs §04).
//
//   NOT covered here: untyped `JSON.parse(text)` in run mode (the result is
//   `unknown`; codegen keeps it CP3002), `undefined` arguments, object fields
//   other than primitive / primitive-array, and cross-module record shapes.
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * Strings are quoted with JSON escaping: `"`, `\`, the short control
//     escapes (`\b \f \n \r \t`) and `\u00xx` for the rest of C0. Raw UTF-8
//     bytes pass through (JS keeps non-ASCII characters unescaped).
//   * Numbers reuse D9 `toString(double)`; NaN and +/-Infinity serialize as
//     `null` (ECMAScript JSON.stringify).
//   * A parse failure is an ordinary `std::exception` subclass, so the
//     generated `catch (e)` boxes it through `Thrown::fromCurrentException`.
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "cp3/core/runtime.hpp"
#include "cp3/std/json_parse.hpp"
#include "cp3/std/json_value.hpp"

namespace cp3::runtime::json {

// =============================================================================
// stringify
// =============================================================================
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

inline void writeValue(const Value& v, std::string& out, int indent, int depth);

// The `indent >= 0` layout: a newline plus `depth * indent` spaces before every
// member and before the closing bracket; `depth` counts nesting levels.
inline void writePad(std::string& out, int indent, int depth) {
    if (indent < 0) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
}

inline void writeArray(const Value& v, std::string& out, int indent, int depth) {
    if (v.items.empty()) {
        out += "[]";
        return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < v.items.size(); ++i) {
        if (i != 0) out.push_back(',');
        writePad(out, indent, depth + 1);
        writeValue(v.items[i], out, indent, depth + 1);
    }
    writePad(out, indent, depth);
    out.push_back(']');
}

inline void writeObject(const Value& v, std::string& out, int indent, int depth) {
    if (v.fields.empty()) {
        out += "{}";
        return;
    }
    out.push_back('{');
    for (std::size_t i = 0; i < v.fields.size(); ++i) {
        if (i != 0) out.push_back(',');
        writePad(out, indent, depth + 1);
        out += stringify(v.fields[i].key);
        out.push_back(':');
        if (indent >= 0) out.push_back(' ');
        writeValue(v.fields[i].value, out, indent, depth + 1);
    }
    writePad(out, indent, depth);
    out.push_back('}');
}

inline void writeValue(const Value& v, std::string& out, int indent, int depth) {
    switch (v.kind) {
        case Value::Kind::Null: out += "null"; return;
        case Value::Kind::Bool: out += stringify(v.boolean); return;
        case Value::Kind::Number: out += stringify(v.number); return;
        case Value::Kind::String: out += stringify(v.text); return;
        case Value::Kind::Array: writeArray(v, out, indent, depth); return;
        case Value::Kind::Object: writeObject(v, out, indent, depth); return;
    }
}

// A negative `indent` is the compact form (no whitespace); 0 and positive
// values pretty-print with that many spaces per nesting level.
inline std::string stringify(const Value& v, int indent = -1) {
    std::string out;
    writeValue(v, out, indent < 0 ? -1 : indent, 0);
    return out;
}

// A whole `Array<E>` value (`JSON.stringify(a)` for `a: number[]`): the
// elements serialize through the overload above, so nesting works.
template <class E>
inline std::string stringify(const Array<E>& elements) {
    std::string out = "[";
    if (elements) {
        for (std::size_t i = 0; i < elements->size(); ++i) {
            if (i != 0) out.push_back(',');
            out += stringify((*elements)[i]);
        }
    }
    out.push_back(']');
    return out;
}

}  // namespace cp3::runtime::json
