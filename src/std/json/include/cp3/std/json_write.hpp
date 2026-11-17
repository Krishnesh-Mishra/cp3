// =============================================================================
// cp3/std/json_write.hpp - the `std/json` dynamic-value writer, glaze-backed.
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the
// generated-code ABI (D14): all functions are `inline`, header-only. Included
// by cp3/std/json.hpp, which owns the codegen contract comment; this header
// is the writer half only (nothing below is generated-code surface).
//
// This is the glaze-backed emitter for the dynamic `Value` model (D68 /
// "Ledger J"). It routes the tree walk through glaze's writer machinery —
// `glz::to<JSON, Value>` is the glaze write visitor for custom types, and
// every structural byte goes through `glz::dump` / `glz::ensure_space` on a
// `glz::resize_unfilled`'d std::string that `glz::buffer_traits<std::string>`
// finalizes (truncates to the document length) — the same buffer model
// `glz::write` uses. The leaf writers all come from glaze's JSON serializer
// (`glaze/json/write.hpp`): strings through the SIMD/SWAR escape scan with
// `escape_control_characters`, booleans/`null` through glaze's own ops, and
// pre-formatted text through `glz::raw_json`. Only `glaze/json/write.hpp` is
// included — no csv/cbor/bson/msgpack/yaml/toml serializer, and the float
// formatters are never instantiated (numbers use the CP3 D9 `toString`, see
// below). `glaze/core/reflect.hpp` does pull `glaze/beve/header.hpp`
// transitively (glaze's own core include); it contains header-forge helpers
// only, no BEVE serializer.
//
// BYTE-IDENTITY CONTRACT (every corpus fixture, every documented example):
//   * Numbers keep the CP3 D9 spelling (`toString(double)`), NaN and
//     +/-Infinity serialize as `null` (ECMAScript JSON.stringify). glaze's
//     shortest-float writer differs on the exponential spelling (`1E21`
//     style, uppercase exponent, no `+`, and a different fixed/scientific
//     threshold), so numbers are pre-formatted text routed through
//     `glz::raw_json` — never glaze's float formatter.
//   * Strings keep CP3's escape table: `\"`, `\\`, `\b \f \n \r \t`, and
//     `\u00xx` with LOWERCASE hex for the rest of C0. glaze's
//     `escape_control_characters` path emits the short escapes and the raw
//     high bytes identically, but spells the remaining `\u00XX` escapes with
//     UPPERCASE hex. Any string carrying a C0 byte other than the short-escape
//     set therefore takes the hand-written escape (same bytes CP3 always
//     emitted); every other string goes through glaze's writer.
//   * Pretty layout is CP3's: a newline plus `depth` copies of the gap before
//     every member and before the closing bracket, a single space after `:`
//     when pretty-printing, `{}`/`[]` for the empty containers at any indent.
//     The gap is a runtime string (`std::string`), repeated per level, which
//     is why the layout lives in the context (`GlazeWriteContext`) rather than
//     in glaze's compile-time `indentation_char`/`indentation_width` opts.
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

// The JSON writer only. Never add glaze csv/cbor/bson/msgpack/yaml/toml
// includes here (binary size + scope); keep the float path unused.
#include "glaze/json/write.hpp"

#include "cp3/core/runtime.hpp"
#include "cp3/std/json_value.hpp"

namespace cp3::runtime::json {

// CP3's JSON string escaper, verbatim from the historical writer. Kept for
// (a) `stringify(const std::string&)` in json.hpp and (b) the byte class
// glaze cannot reproduce (see hasGlazeUnsafeC0 below).
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

// The byte class for which glaze's `escape_control_characters` writer differs
// from CP3: C0 bytes with no short escape are emitted by glaze as `\u00XX`
// with UPPERCASE hex; CP3 emits LOWERCASE hex. Bytes whose low nibble is a-f
// therefore produce different bytes (0x0b, 0x0e, 0x0f, 0x1a..0x1f); this gate
// is deliberately the whole C0-with-short-escape set so the two writers agree
// byte-for-byte on everything it admits, and the fallback below produces the
// historical bytes for everything it does not. Bytes >= 0x20 (including 0x7f
// and all of UTF-8) pass through raw on both writers.
inline bool hasGlazeUnsafeC0(std::string_view value) {
    for (const char c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20u && byte != static_cast<unsigned char>('\b') &&
            byte != static_cast<unsigned char>('\f') &&
            byte != static_cast<unsigned char>('\n') &&
            byte != static_cast<unsigned char>('\r') &&
            byte != static_cast<unsigned char>('\t')) {
            return true;
        }
    }
    return false;
}

// The writer context: the layout is a runtime `gap` (repeated `depth` times
// per level) and a `pretty` flag, which is exactly the historical gap model —
// an integer `space` of 0 still newline-separates with a post-colon space,
// which a plain empty gap cannot express. `depth` counts nesting levels and
// doubles as the pad multiplicity; it is also the `ctx.depth` glaze's
// `is_context` concept requires (writing paths only). `custom_error_message`
// completes the shape glaze's `context` carries; the resizable std::string
// buffer never sets `error`, so the writers never observe one.
struct GlazeWriteContext {
    glz::error_code error{};
    std::string_view custom_error_message;
    uint32_t depth = 0;
    std::string gap;    // one level's padding (empty: compact, or pretty with a zero-width gap)
    bool pretty = false;  // emit '\n' + pad, and a space after ':'
};

// The glaze opts this writer instantiates: the JSON format with
// `escape_control_characters` on (the default leaves C0 bytes raw, which is
// invalid JSON and not what CP3 emits). `bools_as_numbers`, `quoted_num`,
// etc. are never enabled, so the bool/null leaf ops emit exactly the
// historical literals.
struct GlazeWriterOptions : glz::opts {
    bool escape_control_characters = true;
};

// The number bytes: D9 `toString`, `null` for non-finite (ECMAScript). Kept
// as a local helper so the value writer does not depend on the stringify
// overloads' definition order; identical text to `stringify(double)`.
inline std::string numberText(double value) {
    return std::isfinite(value) ? toString(value) : std::string("null");
}

// Dump one JSON string literal. Bytes >= 0x20 and the C0 bytes that have a
// short escape are glaze's SIMD/SWAR escape scan; the remaining C0 class
// takes the hand-written escaper so the `\u00xx` hex digits stay lowercase.
inline void writeEscapedString(const std::string& value, GlazeWriteContext& ctx,
                               std::string& out, std::size_t& ix) {
    if (hasGlazeUnsafeC0(value)) {
        const std::string quoted = "\"" + escapeString(value) + "\"";
        glz::to<glz::JSON, glz::raw_json>::template op<GlazeWriterOptions{}>(
            glz::raw_json{std::move(quoted)}, ctx, out, ix);
        return;
    }
    glz::to<glz::JSON, std::string>::template op<GlazeWriterOptions{}>(value, ctx, out, ix);
}

// '\n' plus `levels` copies of the gap — the historical writePad/writeGapPad.
// Multi-byte gaps repeat per level, which glaze's compile-time
// `indentation_width` cannot express, so the pad lives here.
inline void writePrettyPad(GlazeWriteContext& ctx, std::string& out, std::size_t& ix,
                           uint32_t levels) {
    const std::size_t pad = static_cast<std::size_t>(levels) * ctx.gap.size();
    glz::ensure_space(ctx, out, ix + 1 + pad + glz::write_padding_bytes);
    glz::dump('\n', out, ix);
    for (uint32_t i = 0; i < levels; ++i) {
        glz::dump<false>(std::string_view{ctx.gap}, out, ix);
    }
}

}  // namespace cp3::runtime::json

// -----------------------------------------------------------------------------
// The glaze write visitor for the dynamic `Value` model: one instantiation,
// dispatched on Value::Kind, emitting structurally through glz::dump and the
// leaf writes through glz::to<JSON, ...>. NOT a `glz::write` entry point —
// the layout (gap/pretty) travels in GlazeWriteContext, which no glz::opts
// can carry (the gap is a runtime string); call `writeGlazeValue` instead.
// -----------------------------------------------------------------------------
namespace glz {

template <>
struct to<JSON, cp3::runtime::json::Value> {
    template <auto Opts, class B>
    static void op(const cp3::runtime::json::Value& v,
                   cp3::runtime::json::GlazeWriteContext& ctx,
                   B& b, std::size_t& ix) {
        namespace json = cp3::runtime::json;
        using json::Value;
        const bool pretty = ctx.pretty;

        switch (v.kind) {
            case json::Value::Kind::Null:
                to<JSON, std::nullptr_t>::template op<Opts>(nullptr, ctx, b, ix);
                return;
            case json::Value::Kind::Bool:
                to<JSON, bool>::template op<Opts>(v.boolean, ctx, b, ix);
                return;
            case json::Value::Kind::Number: {
                // Pre-formatted D9 text, dumped verbatim: glaze's float
                // writer is deliberately not instantiated (byte identity).
                to<JSON, glz::raw_json>::template op<Opts>(
                    glz::raw_json{json::numberText(v.number)}, ctx, b, ix);
                return;
            }
            case json::Value::Kind::String:
                json::writeEscapedString(v.text, ctx, b, ix);
                return;
            case json::Value::Kind::Array: {
                if (v.items.empty()) {
                    glz::dump('[', b, ix);
                    glz::dump(']', b, ix);
                    return;
                }
                glz::dump('[', b, ix);
                ++ctx.depth;
                for (std::size_t i = 0; i < v.items.size(); ++i) {
                    if (i != 0) glz::dump(',', b, ix);
                    if (pretty) json::writePrettyPad(ctx, b, ix, ctx.depth);
                    to<glz::JSON, Value>::template op<Opts>(v.items[i], ctx, b, ix);
                }
                --ctx.depth;
                if (pretty) json::writePrettyPad(ctx, b, ix, ctx.depth);
                glz::dump(']', b, ix);
                return;
            }
            case json::Value::Kind::Object: {
                if (v.fields.empty()) {
                    glz::dump('{', b, ix);
                    glz::dump('}', b, ix);
                    return;
                }
                glz::dump('{', b, ix);
                ++ctx.depth;
                for (std::size_t i = 0; i < v.fields.size(); ++i) {
                    if (i != 0) glz::dump(',', b, ix);
                    if (pretty) json::writePrettyPad(ctx, b, ix, ctx.depth);
                    json::writeEscapedString(v.fields[i].key, ctx, b, ix);
                    glz::dump(':', b, ix);
                    if (pretty) glz::dump(' ', b, ix);
                    to<glz::JSON, Value>::template op<Opts>(v.fields[i].value, ctx, b, ix);
                }
                --ctx.depth;
                if (pretty) json::writePrettyPad(ctx, b, ix, ctx.depth);
                glz::dump('}', b, ix);
                return;
            }
            case json::Value::Kind::Undefined:
                // ECMAScript `undefined` has no JSON spelling; the historical
                // writer emitted the raw literal, so keep it byte-identically.
                to<glz::JSON, glz::raw_json>::template op<Opts>(
                    glz::raw_json{"undefined"}, ctx, b, ix);
                return;
        }
    }
};

}  // namespace glz

namespace cp3::runtime::json {

// Drive a whole document through glaze's buffer model: the buffer starts
// grown but unfilled (`glz::resize_unfilled`, the same preamble `glz::write`
// uses) and is truncated to the document length by `glz::buffer_traits`.
// The resizable std::string buffer never fails, so no `error` recovery is
// needed; the output bytes are exactly the dumped ones either way.
inline std::string writeGlazeValue(const Value& value, GlazeWriteContext& ctx) {
    std::string buffer;
    glz::resize_unfilled(buffer, 2 * glz::write_padding_bytes);
    std::size_t ix = 0;
    glz::to<glz::JSON, Value>::template op<GlazeWriterOptions{}>(value, ctx, buffer, ix);
    glz::buffer_traits<std::string>::finalize(buffer, ix);
    return buffer;
}

}  // namespace cp3::runtime::json
