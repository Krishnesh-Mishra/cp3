// =============================================================================
// cp3/std/json_parse.hpp - the `std/json` reader (parse) side.
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the
// generated-code ABI (D14): all functions are `inline`, header-only. Included
// by cp3/std/json.hpp, which owns the codegen contract comment.
//
// The recursive-descent JSON reader plus the typed surface codegen spells:
//   * `parseNumber` / `parseString` / `parseBoolean` for a primitive `T`,
//   * `parseArray<T>` for `T[]` (the element conversion via `convertElement`),
//   * `parseRecord(text, builder)` for a record shape: the generated builder
//     reads every declared field through `numberField`/`stringField`/... and
//     throws `ParseError` on a missing or mistyped one.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/json_value.hpp"

namespace cp3::runtime::json {

namespace parse_detail {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline void appendUtf8(unsigned code, std::string& out) {
    if (code < 0x80u) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (code >> 6)));
        out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xE0u | (code >> 12)));
        out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    }
}

}  // namespace parse_detail

// A recursive-descent JSON reader over one text. `parseDocument` accepts
// exactly one value plus trailing whitespace; anything else is a ParseError.
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Value parseDocument() {
        skipSpace();
        Value v = parseValue();
        skipSpace();
        if (pos_ != text_.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        throw ParseError(std::string("invalid JSON: ") + what + " at offset " +
                         std::to_string(pos_));
    }

    void skipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
            ++pos_;
        }
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool consume(const char* word) {
        const std::size_t n = std::char_traits<char>::length(word);
        if (text_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    Value parseValue() {
        const char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return value(parseStringLiteral());
        if (c == 't' && consume("true")) return value(true);
        if (c == 'f' && consume("false")) return value(false);
        if (c == 'n' && consume("null")) return nullValue();
        if (c == '-' || parse_detail::isDigit(c)) return value(parseNumberLiteral());
        fail("unexpected character");
    }

    Value parseObject() {
        ++pos_;  // '{'
        Value out;
        out.kind = Value::Kind::Object;
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            return out;
        }
        while (true) {
            skipSpace();
            if (peek() != '"') fail("expected a string key");
            std::string key = parseStringLiteral();
            skipSpace();
            if (peek() != ':') fail("expected ':'");
            ++pos_;
            skipSpace();
            out.fields.emplace_back(std::move(key), parseValue());
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return out;
            }
            fail("expected ',' or '}'");
        }
    }

    Value parseArray() {
        ++pos_;  // '['
        Value out;
        out.kind = Value::Kind::Array;
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            return out;
        }
        while (true) {
            skipSpace();
            out.items.push_back(parseValue());
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return out;
            }
            fail("expected ',' or ']'");
        }
    }

    double parseNumberLiteral() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (!parse_detail::isDigit(peek())) fail("expected a digit");
        while (parse_detail::isDigit(peek())) ++pos_;
        if (peek() == '.') {
            ++pos_;
            if (!parse_detail::isDigit(peek())) fail("expected a fraction digit");
            while (parse_detail::isDigit(peek())) ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!parse_detail::isDigit(peek())) fail("expected an exponent digit");
            while (parse_detail::isDigit(peek())) ++pos_;
        }
        return std::strtod(text_.c_str() + start, nullptr);
    }

    unsigned readHex4() {
        if (pos_ + 4u > text_.size()) fail("truncated \\u escape");
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit =
                parse_detail::hexDigit(text_[pos_ + static_cast<std::size_t>(i)]);
            if (digit < 0) fail("bad \\u escape");
            code = code * 16u + static_cast<unsigned>(digit);
        }
        pos_ += 4u;
        return code;
    }

    void appendEscapedUtf8(std::string& out) {
        unsigned code = readHex4();
        if (code >= 0xD800u && code <= 0xDBFFu && pos_ + 1u < text_.size() &&
            text_[pos_] == '\\' && text_[pos_ + 1u] == 'u') {
            const std::size_t saved = pos_;
            pos_ += 2u;
            const unsigned low = readHex4();
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            } else {
                pos_ = saved;  // not a pair: keep the raw code point
            }
        }
        parse_detail::appendUtf8(code, out);
    }

    std::string parseStringLiteral() {
        ++pos_;  // '"'
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) fail("unterminated escape");
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': appendEscapedUtf8(out); break;
                default: fail("unknown escape");
            }
        }
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

inline Value parseText(const std::string& text) {
    return Parser(text).parseDocument();
}

inline void requireKind(const Value& v, Value::Kind kind, const char* what) {
    if (v.kind != kind) {
        throw ParseError(std::string("expected ") + what);
    }
}

inline double parseNumber(const std::string& text) {
    const Value root = parseText(text);
    requireKind(root, Value::Kind::Number, "a number");
    return root.number;
}

inline bool parseBoolean(const std::string& text) {
    const Value root = parseText(text);
    requireKind(root, Value::Kind::Bool, "a boolean");
    return root.boolean;
}

inline std::string parseString(const std::string& text) {
    const Value root = parseText(text);
    requireKind(root, Value::Kind::String, "a string");
    return root.text;
}

// One array element conversion; codegen instantiates it for the element types
// the docs surface allows (`number`, `string`, `boolean`).
template <class T>
T convertElement(const Value&) {
    throw ParseError("unsupported JSON element type");
}

template <>
inline double convertElement<double>(const Value& v) {
    requireKind(v, Value::Kind::Number, "a number");
    return v.number;
}

template <>
inline std::string convertElement<std::string>(const Value& v) {
    requireKind(v, Value::Kind::String, "a string");
    return v.text;
}

template <>
inline bool convertElement<bool>(const Value& v) {
    requireKind(v, Value::Kind::Bool, "a boolean");
    return v.boolean;
}

template <class T>
Array<T> parseArray(const std::string& text) {
    const Value root = parseText(text);
    requireKind(root, Value::Kind::Array, "an array");
    std::vector<T> out;
    out.reserve(root.items.size());
    for (const Value& item : root.items) {
        out.push_back(convertElement<T>(item));
    }
    return makeArray<T>(std::move(out));
}

// =============================================================================
// Record shapes (the generated `JSON.parse<T>` builder surface)
// =============================================================================
inline const Value& requireField(const Fields& fields, const char* name) {
    for (const auto& entry : fields) {
        if (entry.key == name) return entry.value;
    }
    throw ParseError(std::string("missing field '") + name + "'");
}

inline double numberField(const Fields& fields, const char* name) {
    const Value& v = requireField(fields, name);
    requireKind(v, Value::Kind::Number, "a number");
    return v.number;
}

inline std::string stringField(const Fields& fields, const char* name) {
    const Value& v = requireField(fields, name);
    requireKind(v, Value::Kind::String, "a string");
    return v.text;
}

inline bool boolField(const Fields& fields, const char* name) {
    const Value& v = requireField(fields, name);
    requireKind(v, Value::Kind::Bool, "a boolean");
    return v.boolean;
}

inline Array<double> numberArrayField(const Fields& fields, const char* name) {
    const Value& v = requireField(fields, name);
    requireKind(v, Value::Kind::Array, "an array");
    std::vector<double> out;
    out.reserve(v.items.size());
    for (const Value& item : v.items) out.push_back(convertElement<double>(item));
    return makeArray<double>(std::move(out));
}

inline Array<std::string> stringArrayField(const Fields& fields, const char* name) {
    const Value& v = requireField(fields, name);
    requireKind(v, Value::Kind::Array, "an array");
    std::vector<std::string> out;
    out.reserve(v.items.size());
    for (const Value& item : v.items) out.push_back(convertElement<std::string>(item));
    return makeArray<std::string>(std::move(out));
}

inline Array<bool> boolArrayField(const Fields& fields, const char* name) {
    const Value& v = requireField(fields, name);
    requireKind(v, Value::Kind::Array, "an array");
    std::vector<bool> out;
    out.reserve(v.items.size());
    for (const Value& item : v.items) out.push_back(convertElement<bool>(item));
    return makeArray<bool>(std::move(out));
}

// The validated parse: the text must be an object; the generated builder then
// extracts every declared field (and throws on a missing/mistyped one).
template <class F>
auto parseRecord(const std::string& text, F build) {
    const Value root = parseText(text);
    requireKind(root, Value::Kind::Object, "an object");
    return build(root.fields);
}

}  // namespace cp3::runtime::json
