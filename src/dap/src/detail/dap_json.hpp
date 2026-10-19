// =============================================================================
// src/dap/src/detail/dap_json.hpp - minimal JSON model for DAP payloads.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**). PRIVATE header.
//
// Deliberately tiny: DAP messages are small and this server only reads fields
// it knows and echoes request ids verbatim. Numbers keep their raw lexeme so
// an id round-trips exactly (no double rounding), and parsing is depth-limited
// so hostile input cannot exhaust the stack (PKG-LSP-BRIEF robustness).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cp3::dap {

// Nesting cap for parsed payloads; a deeper document is rejected, never
// recursed into.
inline constexpr int kJsonMaxDepth = 64;

// A JSON value. Object members keep insertion order (small payloads, no need
// for a hash map) and arrays are flat vectors.
class Json {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    std::string number;   // raw lexeme, echoed verbatim (request ids)
    std::string string;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    bool isNull() const noexcept { return kind == Kind::Null; }
    bool isBool() const noexcept { return kind == Kind::Bool; }
    bool isNumber() const noexcept { return kind == Kind::Number; }
    bool isString() const noexcept { return kind == Kind::String; }
    bool isArray() const noexcept { return kind == Kind::Array; }
    bool isObject() const noexcept { return kind == Kind::Object; }

    // The member named `key`, or nullptr. Only meaningful for objects.
    const Json* find(std::string_view key) const;

    // The string payload, or nullptr when this value is not a string.
    const std::string* asString() const noexcept {
        return kind == Kind::String ? &string : nullptr;
    }

    // Object member (insert or replace) / array element. Chainable.
    Json& set(std::string key, Json value);
    Json& push(Json value);
};

Json jsonNull();
Json jsonBool(bool value);
Json jsonInt(std::int64_t value);
Json jsonString(std::string value);
Json jsonArray();
Json jsonObject();

// Parses one complete JSON document (trailing bytes are an error). Returns
// false with `err` set on malformed input; `out` is then unspecified.
bool parseJson(std::string_view text, Json& out, std::string& err);

// Serializes compactly; non-ASCII bytes pass through (the input is UTF-8).
std::string toJsonText(const Json& value);

}  // namespace cp3::dap
