// =============================================================================
// src/dap/src/json.cpp - the minimal JSON reader/writer from dap_json.hpp.
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements detail/dap_json.hpp.
//
// Recursive descent with a depth cap (kJsonMaxDepth): malformed input is a
// normal false return, never an exception or a crash. Strings are decoded to
// UTF-8 (surrogates paired, lone surrogates become U+FFFD); serialization
// escapes control characters and passes UTF-8 through unchanged.
// =============================================================================
#include "detail/dap_json.hpp"

#include <cstddef>

namespace cp3::dap {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Appends one non-surrogate Unicode code point as UTF-8.
void appendUtf8(std::string& out, std::uint32_t cp) {
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

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(Json& out) {
        skipWs();
        if (!value(out, 0)) return false;
        skipWs();
        if (pos_ != text_.size()) return fail("trailing characters");
        return true;
    }

    const std::string& error() const noexcept { return error_; }

private:
    bool value(Json& out, int depth) {
        if (depth > kJsonMaxDepth) return fail("nesting too deep");
        const char c = peek();
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.kind = Json::Kind::String;
            return string(out.string);
        }
        if (c == 't') return literal("true", out, Json::Kind::Bool, true);
        if (c == 'f') return literal("false", out, Json::Kind::Bool, false);
        if (c == 'n') return literal("null", out, Json::Kind::Null, false);
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return fail("unexpected character");
    }

    bool object(Json& out, int depth) {
        out.kind = Json::Kind::Object;
        ++pos_;
        skipWs();
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWs();
            if (peek() != '"') return fail("expected an object key");
            std::string key;
            if (!string(key)) return false;
            skipWs();
            if (peek() != ':') return fail("expected ':'");
            ++pos_;
            skipWs();
            Json member;
            if (!value(member, depth + 1)) return false;
            out.set(std::move(key), std::move(member));
            skipWs();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool array(Json& out, int depth) {
        out.kind = Json::Kind::Array;
        ++pos_;
        skipWs();
        if (peek() == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWs();
            Json element;
            if (!value(element, depth + 1)) return false;
            out.push(std::move(element));
            skipWs();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool string(std::string& out) {
        ++pos_;  // the caller verified the opening quote
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (!escape(out)) return false;
                continue;
            }
            if (c < 0x20) return fail("control character in string");
            out.push_back(static_cast<char>(c));
            ++pos_;
        }
        return fail("unterminated string");
    }

    bool escape(std::string& out) {
        if (pos_ >= text_.size()) return fail("truncated escape");
        const char code = text_[pos_++];
        switch (code) {
            case '"': out.push_back('"'); return true;
            case '\\': out.push_back('\\'); return true;
            case '/': out.push_back('/'); return true;
            case 'b': out.push_back('\b'); return true;
            case 'f': out.push_back('\f'); return true;
            case 'n': out.push_back('\n'); return true;
            case 'r': out.push_back('\r'); return true;
            case 't': out.push_back('\t'); return true;
            case 'u': return codePointEscape(out);
            default: return fail("bad escape");
        }
    }

    // A `\uXXXX` sequence was already consumed by the caller.
    bool codePointEscape(std::string& out) {
        std::uint32_t cp = 0;
        if (!hex4(cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF) return highSurrogate(out, cp);
        if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;  // lone low surrogate
        appendUtf8(out, cp);
        return true;
    }

    bool highSurrogate(std::string& out, std::uint32_t high) {
        if (pos_ + 1 < text_.size() && text_[pos_] == '\\' &&
            text_[pos_ + 1] == 'u') {
            pos_ += 2;
            std::uint32_t low = 0;
            if (!hex4(low)) return false;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                appendUtf8(out, 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00));
                return true;
            }
        }
        appendUtf8(out, 0xFFFD);  // unpaired high surrogate
        return true;
    }

    bool hex4(std::uint32_t& out) {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= text_.size()) return fail("truncated \\u escape");
            const int digit = hexValue(text_[pos_]);
            if (digit < 0) return fail("bad \\u escape");
            value = (value << 4) | static_cast<std::uint32_t>(digit);
            ++pos_;
        }
        out = value;
        return true;
    }

    bool number(Json& out) {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (!digits()) return false;
        if (peek() == '.') {
            ++pos_;
            if (!digits()) return fail("expected digits after '.'");
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!digits()) return fail("expected exponent digits");
        }
        out.kind = Json::Kind::Number;
        out.number.assign(text_.substr(start, pos_ - start));
        return true;
    }

    bool digits() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ == start) return fail("expected a digit");
        return true;
    }

    bool literal(std::string_view word, Json& out, Json::Kind kind, bool boolean) {
        if (text_.substr(pos_, word.size()) != word) return fail("bad literal");
        pos_ += word.size();
        out.kind = kind;
        out.boolean = boolean;
        return true;
    }

    void skipWs() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    char peek() const noexcept { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool fail(const char* what) {
        if (error_.empty()) {
            error_ = what;
            error_ += " at offset ";
            error_ += std::to_string(pos_);
        }
        return false;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

void writeString(const std::string& value, std::string& out) {
    out.push_back('"');
    for (const unsigned char c : value) {
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
                    out += "\\u00";
                    out.push_back(kHexDigits[(c >> 4) & 0x0F]);
                    out.push_back(kHexDigits[c & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

void writeValue(const Json& value, std::string& out) {
    switch (value.kind) {
        case Json::Kind::Null:
            out += "null";
            return;
        case Json::Kind::Bool:
            out += value.boolean ? "true" : "false";
            return;
        case Json::Kind::Number:
            out += value.number.empty() ? "0" : value.number;
            return;
        case Json::Kind::String:
            writeString(value.string, out);
            return;
        case Json::Kind::Array:
            break;
        case Json::Kind::Object:
            out.push_back('{');
            for (std::size_t i = 0; i < value.members.size(); ++i) {
                if (i != 0) out.push_back(',');
                writeString(value.members[i].first, out);
                out.push_back(':');
                writeValue(value.members[i].second, out);
            }
            out.push_back('}');
            return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < value.items.size(); ++i) {
        if (i != 0) out.push_back(',');
        writeValue(value.items[i], out);
    }
    out.push_back(']');
}

}  // namespace

const Json* Json::find(std::string_view key) const {
    for (const auto& member : members) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

Json& Json::set(std::string key, Json value) {
    for (auto& member : members) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    members.emplace_back(std::move(key), std::move(value));
    return *this;
}

Json& Json::push(Json value) {
    items.push_back(std::move(value));
    return *this;
}

Json jsonNull() { return Json{}; }

Json jsonBool(bool value) {
    Json out;
    out.kind = Json::Kind::Bool;
    out.boolean = value;
    return out;
}

Json jsonInt(std::int64_t value) {
    Json out;
    out.kind = Json::Kind::Number;
    out.number = std::to_string(value);
    return out;
}

Json jsonString(std::string value) {
    Json out;
    out.kind = Json::Kind::String;
    out.string = std::move(value);
    return out;
}

Json jsonArray() {
    Json out;
    out.kind = Json::Kind::Array;
    return out;
}

Json jsonObject() {
    Json out;
    out.kind = Json::Kind::Object;
    return out;
}

bool parseJson(std::string_view text, Json& out, std::string& err) {
    Parser parser(text);
    Json value;
    if (!parser.parse(value)) {
        err = parser.error();
        return false;
    }
    out = std::move(value);
    return true;
}

std::string toJsonText(const Json& value) {
    std::string out;
    writeValue(value, out);
    return out;
}

}  // namespace cp3::dap
