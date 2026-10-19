// =============================================================================
// src/dap/src/mi.cpp - MI value parsing (detail/mi.hpp).
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// The grammar parsed here is gdb's `output` production from the GDB/MI spec:
// scalars, tuples, lists, with `name=` tags allowed inside tuples and inside
// list elements. Strings are C-quoted; the common escapes plus octal/hex are
// decoded because file paths and printed values travel through them.
// =============================================================================
#include "detail/mi.hpp"

namespace cp3::dap {
namespace {

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A quoted string: the leading quote is at `in[pos]`. Decodes escapes.
bool parseQuoted(const std::string& in, std::size_t& pos, MiValue& out) {
    ++pos;  // opening quote
    std::size_t start = pos;
    while (pos < in.size() && in[pos] != '\\' && in[pos] != '"') ++pos;
    if (pos >= in.size()) return false;
    std::string raw = in.substr(start, pos - start);
    while (pos < in.size() && in[pos] != '"') {
        if (in[pos] == '\\' && pos + 1 < in.size()) {
            raw.push_back(in[pos]);
            raw.push_back(in[pos + 1]);
            pos += 2;
            continue;
        }
        raw.push_back(in[pos]);
        ++pos;
    }
    if (pos >= in.size()) return false;  // unterminated
    ++pos;                               // closing quote
    out.kind = MiValue::Kind::Scalar;
    out.text = miUnescape(raw);
    return true;
}

// A bare scalars: everything up to the next delimiter.
bool parseBare(const std::string& in, std::size_t& pos, MiValue& out) {
    out.kind = MiValue::Kind::Scalar;
    out.text.clear();
    while (pos < in.size() && in[pos] != ',' && in[pos] != '}' && in[pos] != ']') {
        out.text.push_back(in[pos++]);
    }
    return true;
}

// Parses `[name=]value`; the tag (when present) lands in out.name.
bool parseElement(const std::string& in, std::size_t& pos, MiValue& out) {
    const std::size_t nameStart = pos;
    std::size_t scan = pos;
    while (scan < in.size() && isNameChar(in[scan])) ++scan;
    std::string name;
    if (scan > nameStart && scan < in.size() && in[scan] == '=') {
        name = in.substr(nameStart, scan - nameStart);
        pos = scan + 1;
    }
    if (!parseMiValue(in, pos, out)) return false;
    out.name = std::move(name);
    return true;
}

// Shared by tuples and lists: parse `,`-separated elements until `close`.
bool parseSequence(const std::string& in, std::size_t& pos, MiValue& out,
                   char close) {
    if (pos < in.size() && in[pos] == close) {
        ++pos;
        return true;
    }
    for (;;) {
        MiValue element;
        if (!parseElement(in, pos, element)) return false;
        if (close == '}') {
            out.fields.emplace_back(element.name, std::move(element));
        } else {
            out.items.push_back(std::move(element));
        }
        if (pos >= in.size()) return false;
        if (in[pos] == ',') {
            ++pos;
            continue;
        }
        if (in[pos] == close) {
            ++pos;
            return true;
        }
        return false;
    }
}

}  // namespace

bool parseMiValue(const std::string& in, std::size_t& pos, MiValue& out) {
    if (pos >= in.size()) return false;
    const char c = in[pos];
    if (c == '{') {
        ++pos;
        out.kind = MiValue::Kind::Tuple;
        return parseSequence(in, pos, out, '}');
    }
    if (c == '[') {
        ++pos;
        out.kind = MiValue::Kind::List;
        return parseSequence(in, pos, out, ']');
    }
    if (c == '"') return parseQuoted(in, pos, out);
    return parseBare(in, pos, out);
}

bool parseMiBody(const std::string& in, std::size_t pos, MiValue& out) {
    out.kind = MiValue::Kind::Tuple;
    if (pos >= in.size()) return true;
    for (;;) {
        MiValue field;
        if (!parseElement(in, pos, field)) return false;
        out.fields.emplace_back(field.name, std::move(field));
        if (pos >= in.size()) return true;
        if (in[pos] != ',') return false;
        ++pos;
    }
}

const MiValue* miField(const MiValue& value, const std::string& name) {
    for (const auto& field : value.fields) {
        if (field.first == name) return &field.second;
    }
    return nullptr;
}

std::string miText(const MiValue& value, const std::string& name) {
    const MiValue* field = miField(value, name);
    if (field == nullptr || field->kind != MiValue::Kind::Scalar) return {};
    return field->text;
}

std::string miUnescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c != '\\' || i + 1 >= in.size()) {
            out.push_back(c);
            continue;
        }
        const char escape = in[++i];
        switch (escape) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'a': out.push_back('\a'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'v': out.push_back('\v'); break;
            case 'x':
            case 'X': {
                unsigned value = 0;
                int digits = 0;
                while (digits < 2 && i + 1 < in.size()) {
                    const int digit = hexDigit(in[i + 1]);
                    if (digit < 0) break;
                    value = value * 16 + static_cast<unsigned>(digit);
                    ++i;
                    ++digits;
                }
                out.push_back(static_cast<char>(value));
                break;
            }
            default:
                if (escape >= '0' && escape <= '7') {
                    unsigned value = static_cast<unsigned>(escape - '0');
                    int digits = 1;
                    while (digits < 3 && i + 1 < in.size() && in[i + 1] >= '0' &&
                           in[i + 1] <= '7') {
                        value = value * 8 + static_cast<unsigned>(in[++i] - '0');
                        ++digits;
                    }
                    out.push_back(static_cast<char>(value));
                } else {
                    out.push_back(escape);  // covers \\, \", \'
                }
                break;
        }
    }
    return out;
}

std::string miQuote(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 2);
    out.push_back('"');
    for (const char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace cp3::dap
