// =============================================================================
// src/cli/src/detail/json.hpp - a minimal JSON reader/writer for the CLI DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/json.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// Scope: exactly what the package manager needs - parse `package.json` /
// `.cp3/lock.json` / the `.cp3pak` manifest, and write lock/manifest text.
// Insertion order is preserved for objects (so re-writing a parsed document
// keeps a stable field order). Standard C++20 only, no dependencies.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cp3::cli {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull() const noexcept { return type == Type::Null; }
    bool isObject() const noexcept { return type == Type::Object; }
    bool isString() const noexcept { return type == Type::String; }
    bool isArray() const noexcept { return type == Type::Array; }

    // First field with `key`, or nullptr (absent or not an object).
    const JsonValue* find(std::string_view key) const noexcept;

    // Convenience accessors: the value when it has the requested shape.
    const std::string* stringField(std::string_view key) const noexcept;
    const JsonValue* arrayField(std::string_view key) const noexcept;

    // Appends a parsed document from `text`. Returns false and fills `err`
    // (with a 0-based byte offset) when the text is not valid JSON.
    static bool parse(std::string_view text, JsonValue& out, std::string& err);
};

// Escapes `s` (control characters, `"`, `\`) and returns it in quotes.
std::string jsonQuote(std::string_view s);

}  // namespace cp3::cli
