// =============================================================================
// cp3/std/json.hpp - `JSON` (std/json) backing: stringify + parse umbrella.
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only. This is the include the
// generated C++ names (`#include "cp3/std/json.hpp"`); the dynamic value model
// lives in cp3/std/json_value.hpp, the reader in cp3/std/json_parse.hpp, and
// the glaze-backed dynamic-value writer in cp3/std/json_write.hpp.
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
//   `JSON.parseFileSync<T>(path)` / `JSON.parseFile<T>(path)` -> `parseFileSync` /
//     `parseFile` in cp3/std/json_file.hpp: the file read happens through
//     `std/fs` (`readFileSync` / `readFile`), then the text is parsed and
//     checked against T exactly like `parse<T>` (the same builder, the same
//     shape rules; duplicate keys throw). Read failure, invalid JSON and a
//     shape mismatch are one class: `json::ParseError` (thrown here, rejected
//     there).
//   `JSON.stringifyToFileSync<T>(path, value)` / `JSON.stringifyToFile<T>(path, value)`
//     -> `stringifyToFileSync` / `stringifyToFile` in cp3/std/json_file.hpp:
//     the `stringify(value)` lowering unchanged, then a raw overwrite through
//     `std/fs` (`writeFileSync` / `writeFile`). A write failure is
//     `json::ParseError` (thrown here, rejected there).
//   `JSON.verify<T>(text)`        -> `verifyRecord` / `verify*` in cp3/std/json_parse.hpp:
//     the same checker as `parse<T>`, folded into `bool`; any failure
//     (invalid JSON, missing/mistyped required field, duplicate key) is
//     `false`. Never throws.
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
// The glaze-backed dynamic-value writer (Ledger J): escapeString lives there,
// and the `Value` tree emits through `glz::to<JSON, Value>`.
#include "cp3/std/json_write.hpp"
// NOT included unconditionally: cp3/std/json_file.hpp pulls cp3/std/fs.hpp,
// which only exists under src/std/fs/include. The five file API members add
// the include through the module-writer when a program names them.
//#include "cp3/std/json_file.hpp"

namespace cp3::runtime::json {

// =============================================================================
// stringify
// =============================================================================
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

// A negative `indent` is the compact form (no whitespace); 0 and positive
// values pretty-print with that many spaces per nesting level. `indent == 0`
// still newline-separates with a zero-width gap, exactly like the historical
// writer. The tree emit goes through glz::to<JSON, Value>.
inline std::string stringify(const Value& v, int indent = -1) {
    GlazeWriteContext context;
    if (indent >= 0) {
        context.pretty = true;
        context.gap.assign(static_cast<std::size_t>(indent), ' ');
    }
    return writeGlazeValue(v, context);
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

// The gap-aware form: the normalized gap (empty = compact). The newline plus
// `depth` copies of the gap and the post-colon space are emitted by the
// glaze-backed writer (cp3/std/json_write.hpp), byte-identically to the
// historical writeGapPad walker.
inline std::string stringify(const Value& v, const std::string& gap) {
    GlazeWriteContext context;
    std::string normalized = normalizeSpace(gap);
    if (!normalized.empty()) {
        context.pretty = true;
        context.gap = std::move(normalized);
    }
    return writeGlazeValue(v, context);
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
