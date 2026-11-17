// =============================================================================
// src/std/json/parse.cpp - the `std/json` parse engine translation unit.
//
// OWNERSHIP: the std/json family (src/std/json/**). The engine back-end for
// the `std/json` reader (cp3/std/json_parse.hpp): the vendored simdjson DOM
// parser (vendor/third_party/simdjson, v5.1.1, Apache-2.0). This is the ONLY
// translation unit that includes simdjson.h - generated code keeps including
// cp3/std/json_parse.hpp only, so the amalgamation never enters a consumer
// module and is compiled exactly once per program (the std/json native row,
// src/std/json/native.hpp, owns both this TU and vendor/third_party/simdjson/
// src/simdjson.cpp; the row marker selects them only for programs whose text
// names the `std/json` parse/verify spellings).
//
// Contract with cp3/std/json_parse.hpp:
//   * success -> the same Value tree the recursive-descent reader builds;
//     all numbers carry their exact double (int64/uint64/double tape tags all
//     convert through round-to-nearest-even, the same conversion fast_float
//     performs over the raw token; `-0` rides DOUBLE because this TU is
//     compiled with -DSIMDJSON_MINUS_ZERO_AS_FLOAT=1).
//   * failure -> std::nullopt. Every engine failure mode is mapped onto the
//     hand-written reader instead of onto new diagnostics: malformed input,
//     a duplicate key and engine conditions this code cannot reproduce
//     exactly all re-run the recursive-descent reader, which throws the
//     canonical ParseError (same message, same byte offset). Engine errors
//     never reach callers.
//
// The duplicate-key rule (STDLIB spec: never first/last-writer-wins) is not
// detectable by the engine, so the walker below checks it per object while
// building the tree.
// =============================================================================

#include "cp3/std/json_parse.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "simdjson.h"

namespace cp3::runtime::json::parse_detail {

namespace {

// Internal to this TU: the engine reached a shape it cannot reproduce
// exactly (a duplicate key, a BIGINT - number_as_string is never enabled - or
// anything unexpected). The caller maps it onto the hand-written reader.
class EngineUnsupported {};

Value valueFromDom(const simdjson::dom::element& element) {
    switch (element.type()) {
        case simdjson::dom::element_type::ARRAY: {
            Value out;
            out.kind = Value::Kind::Array;
            const simdjson::dom::array array = element.get_array();
            for (const simdjson::dom::element item : array) {
                out.items.push_back(valueFromDom(item));
            }
            return out;
        }
        case simdjson::dom::element_type::OBJECT: {
            Value out;
            out.kind = Value::Kind::Object;
            const simdjson::dom::object object = element.get_object();
            for (const simdjson::dom::key_value_pair field : object) {
                std::string key(field.key);
                // A duplicate key throws (never first/last-writer-wins). The
                // engine does not detect duplicates, so the walker does it
                // during the build; the caller re-runs the hand-written
                // reader to produce the canonical diagnostic + byte offset.
                for (const Value::Field& entry : out.fields) {
                    if (entry.key == key) {
                        throw EngineUnsupported{};
                    }
                }
                out.fields.emplace_back(std::move(key),
                                        valueFromDom(field.value));
            }
            return out;
        }
        case simdjson::dom::element_type::STRING: {
            const std::string_view text = element.get_string();
            Value out;
            out.kind = Value::Kind::String;
            out.text = std::string(text);
            return out;
        }
        case simdjson::dom::element_type::INT64:
        case simdjson::dom::element_type::UINT64:
        case simdjson::dom::element_type::DOUBLE: {
            // The tape tag only records how the number was written; all three
            // carry the exact value. int64/uint64 -> double is correctly
            // rounded, the same conversion fast_float performs over the raw
            // token, and overflows to infinity were rejected by the engine
            // (the hand-written reader accepts them, so they take the
            // fallback). `-0` is stored as DOUBLE(-0.0) under
            // SIMDJSON_MINUS_ZERO_AS_FLOAT=1, so the sign survives too.
            const double number = element.get_double();
            Value out;
            out.kind = Value::Kind::Number;
            out.number = number;
            return out;
        }
        case simdjson::dom::element_type::BOOL: {
            const bool boolean = element.get_bool();
            Value out;
            out.kind = Value::Kind::Bool;
            out.boolean = boolean;
            return out;
        }
        case simdjson::dom::element_type::NULL_VALUE:
            return nullValue();
        default:
            // BIGINT (number_as_string is never enabled, so unreachable) and
            // anything else unexpected: the hand-written reader handles it.
            throw EngineUnsupported{};
    }
}

}  // namespace

std::optional<Value> parseJsonDocument(const std::string& text) {
    // One reusable DOM parser per thread: its tape/string buffers amortize
    // across calls, and no state is shared across threads. The engine is
    // eager (structural scan + tape build + validation happen inside
    // parse()), so an accepted document is fully valid UTF-8 with a fully
    // consumed root and no trailing content (anything else is an engine
    // error, mapped onto the hand-written reader).
    thread_local simdjson::dom::parser parser;
    try {
        const simdjson::dom::element root = parser.parse(text);
        return valueFromDom(root);
    } catch (const simdjson::simdjson_error&) {
        // The engine rejected the document (unescaped control characters,
        // invalid UTF-8, unterminated structures, number overflow, trailing
        // content, nesting past the engine's 1024-deep limit, empty input,
        // ...). The hand-written reader re-derives the same Value or the
        // canonical ParseError - never this error code.
        return std::nullopt;
    } catch (const EngineUnsupported&) {
        // A duplicate key (or an unreachable BIGINT). The hand-written
        // reader re-throws the canonical "duplicate key '<key>'" diagnostic
        // with the same byte offset as always.
        return std::nullopt;
    }
}

}  // namespace cp3::runtime::json::parse_detail
