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
//   `JSON.stringify(value, replacer?, space?)`: the ECMAScript replacer +
//     space forms. `replacer` is a CP3 callback `(key, value) => replacement`
//     (adapted to `Replacer`) or a `string[]` allowlist; `space` is a number
//     or a string, normalized by `normalizeSpace`. The CP3 indent form
//     `stringify(value, indentNumber)` is kept as the second-argument number
//     case (see emitJsonStringify).
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
//   `JSON.parse<{ x: number }>(text)` -> the same `parseRecord(text, builder)`
//     over the annotation's `$o{...}` value-shape struct (a plain object shape
//     of primitives, one-level primitive arrays, or nested plain records);
//     the struct materializes like an object-shape literal. Other object
//     shapes keep the unsupported-argument diagnostic.
//
//   NOT covered here: untyped `JSON.parse(text)` in run mode (the result is
//   `unknown`, which has no runtime representation; codegen reports a type
//   error asking for `JSON.parse<T>`), `undefined` arguments, object fields
//   other than primitive / primitive-array, and cross-module record shapes.
//   `JSON.stringify` accepts a non-literal object value through its generated
//   `$o{...}` field list; a class/interface receiver reports a type error.
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
#include <functional>
#include <optional>
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
        case Value::Kind::Undefined: out += "undefined"; return;
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

// =============================================================================
// Replacer + space (the ECMAScript JSON.stringify options)
// =============================================================================

// The replacer the CP3 call lowering adapts a CP3 callback to: it receives
// the property key and the original value tree and returns the replacement
// tree, or `std::nullopt` to omit the property (ECMAScript `undefined`).
// The codegen wrapper homogenizes a CP3 callback's scalar return through
// `value(...)` first, so returning the `value` parameter forwards the child.
using Replacer =
    std::function<std::optional<Value>(const std::string&, const Value&)>;

inline std::optional<Value> toOptionalValue(const Value& v) {
    if (isUndefined(v)) return std::nullopt;
    return v;
}

// Normalize the `space` argument the way ECMAScript does: a number clamps to
// 0..10 spaces per level (a negative count is 0), a string keeps at most its
// first 10 characters. The empty gap means compact (no pretty-printing).
inline std::string normalizeSpace(double space) {
    if (!std::isfinite(space)) return std::string();
    double count = std::trunc(space);
    if (count < 0.0) count = 0.0;
    if (count > 10.0) count = 10.0;
    return std::string(static_cast<std::size_t>(count), ' ');
}

inline std::string normalizeSpace(const std::string& space) {
    return space.size() > 10 ? space.substr(0, 10) : space;
}

// The gap-aware pretty writer used by the replacer/allowlist forms. `gap`
// empty is the compact form; otherwise every member is preceded by a newline
// plus `depth` copies of `gap` (ECMAScript SerializeJSONProperty).
inline void writeGapPad(std::string& out, const std::string& gap, int depth) {
    if (gap.empty()) return;
    out.push_back('\n');
    for (int i = 0; i < depth; ++i) out += gap;
}

inline void writeValueGap(const Value& v, std::string& out,
                          const std::string& gap, int depth);

inline void writeArrayGap(const Value& v, std::string& out,
                          const std::string& gap, int depth) {
    if (v.items.empty()) {
        out += "[]";
        return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < v.items.size(); ++i) {
        if (i != 0) out.push_back(',');
        writeGapPad(out, gap, depth + 1);
        writeValueGap(v.items[i], out, gap, depth + 1);
    }
    writeGapPad(out, gap, depth);
    out.push_back(']');
}

inline void writeObjectGap(const Value& v, std::string& out,
                           const std::string& gap, int depth) {
    if (v.fields.empty()) {
        out += "{}";
        return;
    }
    out.push_back('{');
    for (std::size_t i = 0; i < v.fields.size(); ++i) {
        if (i != 0) out.push_back(',');
        writeGapPad(out, gap, depth + 1);
        out += stringify(v.fields[i].key);
        out.push_back(':');
        if (!gap.empty()) out.push_back(' ');
        writeValueGap(v.fields[i].value, out, gap, depth + 1);
    }
    writeGapPad(out, gap, depth);
    out.push_back('}');
}

inline void writeValueGap(const Value& v, std::string& out,
                          const std::string& gap, int depth) {
    switch (v.kind) {
        case Value::Kind::Null: out += "null"; return;
        case Value::Kind::Bool: out += stringify(v.boolean); return;
        case Value::Kind::Number: out += stringify(v.number); return;
        case Value::Kind::String: out += stringify(v.text); return;
        case Value::Kind::Array: writeArrayGap(v, out, gap, depth); return;
        case Value::Kind::Object: writeObjectGap(v, out, gap, depth); return;
        case Value::Kind::Undefined: out += "undefined"; return;
    }
}

inline std::string stringify(const Value& v, const std::string& gap) {
    std::string out;
    writeValueGap(v, out, normalizeSpace(gap), 0);
    return out;
}

// Forward declarations: the replacer forms below call these.
inline std::optional<Value> applyReplacer(const std::string& key,
                                          const Value& original,
                                          const Replacer& replacer);
inline Replacer allowlistReplacer(const std::vector<std::string>& allow);

// `JSON.stringify(value, replacer, space?)`: the transformed tree writes with
// the normalized gap (`space: number | string`); a replacer that omits the
// root yields "" (ECMAScript returns `undefined`, which has no `string`
// spelling in CP3).
inline std::string stringify(const Value& v, const Replacer& replacer,
                             const std::string& gap) {
    const std::optional<Value> kept = applyReplacer("", v, replacer);
    if (!kept.has_value()) return std::string();
    return stringify(*kept, gap);
}

// `JSON.stringify(value, allowlist, space?)`: the array-replacer form, where
// the list is already a `std::vector<std::string>` of property names.
inline std::string stringify(const Value& v,
                             const std::vector<std::string>& allow,
                             const std::string& gap) {
    return stringify(v, allowlistReplacer(allow), gap);
}

// The replacer traversal (ECMAScript SerializeJSONProperty, minus `toJSON`
// and the holder `this`): the replacer runs top-down on every node under the
// root `{"": value}` holder; an omitted node never recurses into its
// children. `nullopt` omits a property or (for the root only) yields "".
// Array elements omitted by the replacer serialize as `null`, object fields
// are dropped, exactly as ECMAScript specifies.
inline std::optional<Value> applyReplacer(const std::string& key,
                                          const Value& original,
                                          const Replacer& replacer) {
    const std::optional<Value> replaced = replacer(key, original);
    if (!replaced.has_value() || isUndefined(*replaced)) return std::nullopt;
    Value out = *replaced;
    if (out.kind == Value::Kind::Array) {
        std::vector<Value> kept;
        kept.reserve(out.items.size());
        for (std::size_t i = 0; i < out.items.size(); ++i) {
            const std::optional<Value> child =
                applyReplacer(std::to_string(i), out.items[i], replacer);
            kept.push_back(child.has_value() ? std::move(*child) : nullValue());
        }
        out.items = std::move(kept);
    } else if (out.kind == Value::Kind::Object) {
        Fields kept;
        kept.reserve(out.fields.size());
        for (const Value::Field& field : out.fields) {
            const std::optional<Value> child =
                applyReplacer(field.key, field.value, replacer);
            if (child.has_value()) {
                kept.emplace_back(field.key, std::move(*child));
            }
        }
        out.fields = std::move(kept);
    }
    return out;
}

// An allowlist replacer (ECMAScript: a replacer that is an array keeps only
// the listed keys; the root and every array index pass through): the array
// elements are the listed property names.
inline Replacer allowlistReplacer(const std::vector<std::string>& allow) {
    return [allow](const std::string& key, const Value& v) -> std::optional<Value> {
        if (key.empty()) return v;  // the root holder
        // Primitives and arrays always pass (ECMAScript only drops
        // non-array objects whose key is missing from the list).
        if (v.kind != Value::Kind::Object) return v;
        for (const std::string& name : allow) {
            if (name == key) return v;
        }
        return std::nullopt;
    };
}

}  // namespace cp3::runtime::json
