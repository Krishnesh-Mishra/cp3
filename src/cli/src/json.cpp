// =============================================================================
// cli/json.cpp — a minimal JSON reader/writer for the CLI DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/json.hpp.
//
// Recursive-descent parser. Depth-limited (a hostile file cannot blow the
// stack) and strict enough for package metadata: trailing garbage, trailing
// commas and control characters in strings are rejected. Object field order
// is preserved.
// =============================================================================
#include "private/json.hpp"

#include <cstdint>
#include <cstdlib>

namespace cp3::cli {

namespace {

constexpr int kMaxDepth = 64;

void appendUtf8(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Parser {
    std::string_view text;
    std::size_t pos = 0;
    std::string err;

    bool fail(const std::string& message) {
        if (err.empty()) {
            err = message + " at offset " + std::to_string(pos);
        }
        return false;
    }

    bool atEnd() const { return pos >= text.size(); }
    char peek() const { return text[pos]; }

    void skipWs() {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\r' ||
                            peek() == '\n')) {
            ++pos;
        }
    }

    bool parseValue(JsonValue& out, int depth) {
        if (depth > kMaxDepth) return fail("JSON nesting too deep");
        skipWs();
        if (atEnd()) return fail("unexpected end of JSON");
        const char c = peek();
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            out = JsonValue{};
            out.type = JsonValue::Type::String;
            return parseString(out.string);
        }
        if (c == 't' || c == 'f') return parseBool(out, c == 't');
        if (c == 'n') return parseNull(out);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("unexpected character in JSON");
    }

    bool parseObject(JsonValue& out, int depth) {
        out = JsonValue{};
        out.type = JsonValue::Type::Object;
        ++pos;  // '{'
        skipWs();
        if (!atEnd() && peek() == '}') {
            ++pos;
            return true;
        }
        for (;;) {
            skipWs();
            std::string key;
            if (atEnd() || peek() != '"') return fail("expected object key");
            if (!parseString(key)) return false;
            skipWs();
            if (atEnd() || peek() != ':') return fail("expected ':'");
            ++pos;
            JsonValue value;
            if (!parseValue(value, depth + 1)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            skipWs();
            if (atEnd()) return fail("unterminated object");
            if (peek() == ',') {
                ++pos;
                continue;
            }
            if (peek() == '}') {
                ++pos;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out, int depth) {
        out = JsonValue{};
        out.type = JsonValue::Type::Array;
        ++pos;  // '['
        skipWs();
        if (!atEnd() && peek() == ']') {
            ++pos;
            return true;
        }
        for (;;) {
            JsonValue value;
            if (!parseValue(value, depth + 1)) return false;
            out.array.push_back(std::move(value));
            skipWs();
            if (atEnd()) return fail("unterminated array");
            if (peek() == ',') {
                ++pos;
                continue;
            }
            if (peek() == ']') {
                ++pos;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(std::string& out) {
        ++pos;  // '"'
        out.clear();
        while (!atEnd()) {
            const unsigned char c = static_cast<unsigned char>(text[pos]);
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c < 0x20) return fail("control character in string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++pos;
                continue;
            }
            ++pos;
            if (atEnd()) return fail("unterminated escape");
            const char esc = text[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        cp = combineSurrogate(cp);
                    }
                    appendUtf8(cp, out);
                    break;
                }
                default: return fail("invalid escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseHex4(std::uint32_t& cp) {
        if (pos + 4 > text.size()) return fail("truncated \\u escape");
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            const int d = hexDigit(text[pos++]);
            if (d < 0) return fail("bad hex digit in \\u escape");
            cp = (cp << 4) | static_cast<std::uint32_t>(d);
        }
        return true;
    }

    // High surrogate: consume a following "\uDCxx" low surrogate when
    // present; otherwise keep the lone surrogate's code point.
    std::uint32_t combineSurrogate(std::uint32_t high) {
        if (pos + 6 <= text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
            std::uint32_t low = 0;
            const std::size_t save = pos;
            pos += 2;
            if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
            }
            pos = save;
        }
        return high;
    }

    bool parseBool(JsonValue& out, bool value) {
        const std::string_view word = value ? "true" : "false";
        if (text.substr(pos, word.size()) != word) return fail("invalid literal");
        pos += word.size();
        out = JsonValue{};
        out.type = JsonValue::Type::Bool;
        out.boolean = value;
        return true;
    }

    bool parseNull(JsonValue& out) {
        if (text.substr(pos, 4) != "null") return fail("invalid literal");
        pos += 4;
        out = JsonValue{};
        out.type = JsonValue::Type::Null;
        return true;
    }

    bool parseNumber(JsonValue& out) {
        const std::size_t start = pos;
        if (!atEnd() && peek() == '-') ++pos;
        bool anyDigit = false;
        while (!atEnd() && peek() >= '0' && peek() <= '9') {
            ++pos;
            anyDigit = true;
        }
        if (!anyDigit) return fail("invalid number");
        if (!atEnd() && peek() == '.') {
            ++pos;
            bool frac = false;
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++pos;
                frac = true;
            }
            if (!frac) return fail("invalid number");
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++pos;
            if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos;
            bool exp = false;
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++pos;
                exp = true;
            }
            if (!exp) return fail("invalid number");
        }
        const std::string token(text.substr(start, pos - start));
        out = JsonValue{};
        out.type = JsonValue::Type::Number;
        out.number = std::strtod(token.c_str(), nullptr);
        return true;
    }
};

void writeQuoted(std::string_view s, std::string& out) {
    out.push_back('"');
    for (char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
}

}  // namespace

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type != Type::Object) return nullptr;
    for (const auto& field : object) {
        if (field.first == key) return &field.second;
    }
    return nullptr;
}

const std::string* JsonValue::stringField(std::string_view key) const noexcept {
    const JsonValue* value = find(key);
    if (value == nullptr || value->type != Type::String) return nullptr;
    return &value->string;
}

const JsonValue* JsonValue::arrayField(std::string_view key) const noexcept {
    const JsonValue* value = find(key);
    if (value == nullptr || value->type != Type::Array) return nullptr;
    return value;
}

bool JsonValue::parse(std::string_view text, JsonValue& out, std::string& err) {
    Parser parser;
    parser.text = text;
    JsonValue value;
    if (!parser.parseValue(value, 0)) {
        err = parser.err.empty() ? "invalid JSON" : parser.err;
        return false;
    }
    parser.skipWs();
    if (!parser.atEnd()) {
        err = "trailing characters after JSON value at offset " +
              std::to_string(parser.pos);
        return false;
    }
    out = std::move(value);
    return true;
}

std::string jsonQuote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    writeQuoted(s, out);
    return out;
}

}  // namespace cp3::cli
