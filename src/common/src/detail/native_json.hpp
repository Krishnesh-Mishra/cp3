// =============================================================================
// src/common/src/detail/native_json.hpp - a minimal JSON reader (PRIVATE).
//
// OWNERSHIP: common module. Never included cross-module.
//
// The native binding reader needs to read one flat JSON document
// (`.cp3/native/<id>/native.json`). This is the smallest reader that does
// it, kept private to the common module: the package manager has its own
// (src/cli/src/detail/json.hpp) and the language server its own, and none of
// them shares this one.
//
// Definitions live in src/common/src/detail/native_json.cpp (STYLE.md §3:
// headers declare, `.cpp` defines).
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cp3::detail {

// A JSON value. Objects keep their member order; arrays keep their items.
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isBool() const { return kind == Kind::Bool; }

    // The member named `key`, or nullptr (absent, or not an object).
    const JsonValue* find(std::string_view key) const;
};

// Parses one complete JSON document. Returns false and fills `err` with a
// human-readable reason when the text is not valid JSON.
bool parseJson(std::string_view text, JsonValue& out, std::string& err);

}  // namespace cp3::detail
