// =============================================================================
// src/common/src/detail/native_json.cpp - the minimal JSON reader DEFINITIONS.
//
// OWNERSHIP: common module. Implements detail/native_json.hpp.
//
// A recursive-descent reader over one text: values, objects, arrays and
// strings, with the JSON escape set. Standard C++20 only, no dependencies.
// =============================================================================
#include "detail/native_json.hpp"

#include <cstddef>
#include <cstdlib>
#include <string>

namespace cp3::detail {

const JsonValue* JsonValue::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) return &values[i];
    }
    return nullptr;
}

namespace {

// The recursive-descent reader. `err` carries a human-readable reason.
class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    bool parse(JsonValue& out, std::string& err) {
        skipSpace();
        if (!parseValue(out, err)) return false;
        skipSpace();
        if (pos_ != text_.size()) {
            err = "trailing characters after the JSON document";
            return false;
        }
        return true;
    }

private:
    void skipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool literal(const char* word) {
        const std::size_t n = std::char_traits<char>::length(word);
        if (text_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    bool parseValue(JsonValue& out, std::string& err) {
        if (pos_ >= text_.size()) {
            err = "unexpected end of the JSON document";
            return false;
        }
        const char c = text_[pos_];
        if (c == '{') return parseObject(out, err);
        if (c == '[') return parseArray(out, err);
        if (c == '"') {
            out.kind = JsonValue::Kind::String;
            return parseString(out.text, err);
        }
        if (literal("true")) {
            out.kind = JsonValue::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.kind = JsonValue::Kind::Bool;
            out.boolean = false;
            return true;
        }
        if (literal("null")) {
            out.kind = JsonValue::Kind::Null;
            return true;
        }
        return parseNumber(out, err);
    }

    bool parseNumber(JsonValue& out, std::string& err) {
        const std::size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            const bool numeric = (c >= '0' && c <= '9') || c == '-' || c == '+' ||
                                 c == '.' || c == 'e' || c == 'E';
            if (!numeric) break;
            ++pos_;
        }
        if (pos_ == start) {
            err = "expected a JSON value";
            return false;
        }
        out.kind = JsonValue::Kind::Number;
        const std::string digits(text_.substr(start, pos_ - start));
        out.number = std::strtod(digits.c_str(), nullptr);
        return true;
    }

    bool parseString(std::string& out, std::string& err) {
        ++pos_;  // the opening quote
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (!parseEscape(out, err)) return false;
        }
        err = "unterminated string";
        return false;
    }

    // One backslash escape: the character it stands for.
    bool parseEscape(std::string& out, std::string& err) {
        if (pos_ >= text_.size()) {
            err = "unterminated string";
            return false;
        }
        const char esc = text_[pos_++];
        switch (esc) {
            case 'n': out.push_back('\n'); return true;
            case 't': out.push_back('\t'); return true;
            case 'r': out.push_back('\r'); return true;
            case 'b': out.push_back('\b'); return true;
            case 'f': out.push_back('\f'); return true;
            case 'u': return parseUnicodeEscape(out, err);
            default: out.push_back(esc); return true;
        }
    }

    // A \uXXXX escape as UTF-8. A binding never carries a surrogate pair; one
    // is written as the replacement character.
    bool parseUnicodeEscape(std::string& out, std::string& err) {
        if (pos_ + 4 > text_.size()) {
            err = "truncated \\u escape";
            return false;
        }
        out += decodeUtf8(text_.substr(pos_, 4));
        pos_ += 4;
        return true;
    }

    // The UTF-8 encoding of a \uXXXX code point.
    static std::string decodeUtf8(std::string_view hex) {
        unsigned code = 0;
        for (const char c : hex) {
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
        }
        std::string out;
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        return out;
    }

    bool parseArray(JsonValue& out, std::string& err) {
        out.kind = JsonValue::Kind::Array;
        ++pos_;  // '['
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            skipSpace();
            JsonValue item;
            if (!parseValue(item, err)) return false;
            out.items.push_back(std::move(item));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            err = "expected ',' or ']' in a JSON array";
            return false;
        }
    }

    bool parseObject(JsonValue& out, std::string& err) {
        out.kind = JsonValue::Kind::Object;
        ++pos_;  // '{'
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                err = "expected a member name in a JSON object";
                return false;
            }
            std::string key;
            if (!parseString(key, err)) return false;
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                err = "expected ':' after a JSON member name";
                return false;
            }
            ++pos_;
            skipSpace();
            JsonValue value;
            if (!parseValue(value, err)) return false;
            out.keys.push_back(std::move(key));
            out.values.push_back(std::move(value));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            err = "expected ',' or '}' in a JSON object";
            return false;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

bool parseJson(std::string_view text, JsonValue& out, std::string& err) {
    JsonReader reader(text);
    return reader.parse(out, err);
}

}  // namespace cp3::detail
