// =============================================================================
// src/codegen/src/support/cpp_writer.cpp - PRIVATE generated-C++ text helpers
// (STYLE.md §8). Implements cpp_writer.h; emits bytes verbatim.
// =============================================================================
#include "detail/cpp_writer.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

namespace cp3 {
namespace detail {

void line(std::string& out, int indent, const std::string& text) {
    out.append(static_cast<std::size_t>(indent) * 4, ' ');
    out += text;
    out += '\n';
}

std::string cppStringLiteral(const std::string& value) {
    std::string out;
    out.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': out += "\\000"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\%03o", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string numberLiteralText(double value) {
    if (std::isnan(value)) {
        return "std::numeric_limits<double>::quiet_NaN()";
    }
    if (std::isinf(value)) {
        return value < 0.0 ? "(-std::numeric_limits<double>::infinity())"
                           : "std::numeric_limits<double>::infinity()";
    }
    char buf[64];
    const std::to_chars_result res = std::to_chars(buf, buf + sizeof(buf), value);
    std::string text;
    if (res.ec == std::errc()) {
        text.assign(buf, res.ptr);
    } else {
        std::snprintf(buf, sizeof(buf), "%.17g", value);
        text.assign(buf);
    }
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string joinStrings(const std::vector<std::string>& parts,
                        const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

}  // namespace detail
}  // namespace cp3
