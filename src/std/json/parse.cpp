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

#include <cstddef>
#include <deque>
#include <memory>
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

// One reusable DOM parser per thread: its tape/string buffers amortize across
// calls, and no state is shared across threads. The engine is eager
// (structural scan + tape build + validation happen inside parse()), so an
// accepted document is fully valid UTF-8 with a fully consumed root and no
// trailing content (anything else is an engine error, mapped onto the
// hand-written reader).
thread_local simdjson::dom::parser gParser;

Value valueFromDom(const simdjson::dom::element& element) {
    switch (element.type()) {
        case simdjson::dom::element_type::ARRAY: {
            Value out;
            out.kind = Value::Kind::Array;
            const simdjson::dom::array array = element.get_array();
            // The engine already knows the element count; reserving once is
            // the difference between one allocation and a geometric regrowth
            // of the whole subtree (a 50,000 element array copies ~10 MB of
            // fat nodes otherwise, then frees them again).
            out.items.reserve(array.size());
            for (const simdjson::dom::element item : array) {
                out.items.push_back(valueFromDom(item));
            }
            return out;
        }
        case simdjson::dom::element_type::OBJECT: {
            Value out;
            out.kind = Value::Kind::Object;
            const simdjson::dom::object object = element.get_object();
            out.fields.reserve(object.size());
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

// =============================================================================
// The typed record fast path (the `Record` view of cp3/std/json_parse.hpp)
// =============================================================================

// One object inside the engine's DOM, plus the values the optional-field
// probe (`findField`) materialises on demand. The cache is a deque so a
// returned `const Value*` stays put while the builder keeps reading.
struct DomObject {
    simdjson::dom::object object;
    mutable std::deque<Value> cache;
};

namespace {

// Every view handed out by an earlier parse dies at the next one: nothing
// outside a single `parseRecord` builder call may hold one.
thread_local std::vector<std::unique_ptr<DomObject>> gObjects;

std::optional<simdjson::dom::element> findElement(const DomObject& object,
                                                  const char* name) {
    const std::string_view wanted(name);
    for (const simdjson::dom::key_value_pair field : object.object) {
        if (field.key == wanted) return field.value;
    }
    return std::nullopt;
}

[[noreturn]] void missingField(const char* name) {
    throw ParseError(std::string("missing field '") + name + "'");
}

// The same walk `valueFromDom` performs, without building anything: every
// object is checked for duplicate keys (in document order, so the hand-written
// reader throws the same key at the same offset) and every value for a shape
// the engine cannot reproduce exactly. Walking the whole document is required
// for behaviour parity: a duplicate key anywhere is a ParseError today, even
// in a field the builder never reads.
void validateDocument(const simdjson::dom::element& element) {
    using simdjson::dom::element_type;
    switch (element.type()) {
        case element_type::ARRAY: {
            const simdjson::dom::array array = element.get_array();
            for (const simdjson::dom::element item : array) {
                const element_type type = item.type();
                if (type == element_type::ARRAY ||
                    type == element_type::OBJECT) {
                    validateDocument(item);
                } else if (type == element_type::BIGINT) {
                    throw EngineUnsupported{};
                }
            }
            return;
        }
        case element_type::OBJECT: {
            const simdjson::dom::object object = element.get_object();
            std::vector<std::string_view> seen;
            seen.reserve(object.size());
            for (const simdjson::dom::key_value_pair field : object) {
                for (const std::string_view key : seen) {
                    if (key == field.key) throw EngineUnsupported{};
                }
                seen.push_back(field.key);
                validateDocument(field.value);
            }
            return;
        }
        case element_type::BIGINT:
            throw EngineUnsupported{};
        default:
            return;
    }
}

// Keep one object view alive for the builder call that is about to run.
const DomObject* adopt(const simdjson::dom::element& element) {
    auto node = std::make_unique<DomObject>();
    node->object = element.get_object();
    const DomObject* view = node.get();
    gObjects.push_back(std::move(node));
    return view;
}

}  // namespace

std::optional<Value> parseJsonDocument(const std::string& text) {
    try {
        const simdjson::dom::element root = gParser.parse(text);
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

std::optional<const DomObject*> parseJsonObject(const std::string& text) {
    gObjects.clear();
    try {
        const simdjson::dom::element root = gParser.parse(text);
        if (root.type() != simdjson::dom::element_type::OBJECT) {
            // A valid non-object document: the hand-written reader produces
            // the canonical "expected an object" diagnostic.
            return std::nullopt;
        }
        validateDocument(root);
        return adopt(root);
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;  // malformed text or an engine limit
    } catch (const EngineUnsupported&) {
        return std::nullopt;  // duplicate key / BIGINT -> canonical offset
    }
}

double domNumber(const DomObject& object, const char* name) {
    using simdjson::dom::element_type;
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    switch (found->type()) {
        case element_type::INT64:
        case element_type::UINT64:
        case element_type::DOUBLE:
            // The tape tag only records how the number was written; all three
            // carry the exact value (see valueFromDom).
            return found->get_double();
        default:
            throw ParseError("expected a number");
    }
}

std::string domString(const DomObject& object, const char* name) {
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    if (found->type() != simdjson::dom::element_type::STRING) {
        throw ParseError("expected a string");
    }
    const std::string_view text = found->get_string();
    return std::string(text);
}

bool domBool(const DomObject& object, const char* name) {
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    if (found->type() != simdjson::dom::element_type::BOOL) {
        throw ParseError("expected a boolean");
    }
    return found->get_bool();
}

std::vector<double> domNumberArray(const DomObject& object, const char* name) {
    using simdjson::dom::element_type;
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    if (found->type() != element_type::ARRAY) {
        throw ParseError("expected an array");
    }
    const simdjson::dom::array array = found->get_array();
    std::vector<double> out;
    out.reserve(array.size());
    for (const simdjson::dom::element item : array) {
        const element_type type = item.type();
        if (type != element_type::INT64 && type != element_type::UINT64 &&
            type != element_type::DOUBLE) {
            throw ParseError("expected a number");
        }
        out.push_back(item.get_double());
    }
    return out;
}

std::vector<std::string> domStringArray(const DomObject& object,
                                        const char* name) {
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    if (found->type() != simdjson::dom::element_type::ARRAY) {
        throw ParseError("expected an array");
    }
    const simdjson::dom::array array = found->get_array();
    std::vector<std::string> out;
    out.reserve(array.size());
    for (const simdjson::dom::element item : array) {
        if (item.type() != simdjson::dom::element_type::STRING) {
            throw ParseError("expected a string");
        }
        const std::string_view text = item.get_string();
        out.emplace_back(text);
    }
    return out;
}

std::vector<bool> domBoolArray(const DomObject& object, const char* name) {
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    if (found->type() != simdjson::dom::element_type::ARRAY) {
        throw ParseError("expected an array");
    }
    const simdjson::dom::array array = found->get_array();
    std::vector<bool> out;
    out.reserve(array.size());
    for (const simdjson::dom::element item : array) {
        if (item.type() != simdjson::dom::element_type::BOOL) {
            throw ParseError("expected a boolean");
        }
        out.push_back(item.get_bool());
    }
    return out;
}

const DomObject* domRecord(const DomObject& object, const char* name) {
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) missingField(name);
    if (found->type() != simdjson::dom::element_type::OBJECT) {
        throw ParseError("expected an object");
    }
    return adopt(*found);
}

const Value& domRequire(const DomObject& object, const char* name) {
    const Value* found = domFind(object, name);
    if (found == nullptr) missingField(name);
    return *found;
}

const Value* domFind(const DomObject& object, const char* name) {
    const std::optional<simdjson::dom::element> found =
        findElement(object, name);
    if (!found) return nullptr;
    object.cache.emplace_back(valueFromDom(*found));
    return &object.cache.back();
}

}  // namespace cp3::runtime::json::parse_detail
