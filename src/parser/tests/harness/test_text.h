// =============================================================================
// src/parser/tests/test_text.h - shared character/printing helpers for the
// standalone parser driver (scanner + AST printer). Test-only.
// =============================================================================
#pragma once

#include <string>

namespace cp3test {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

inline bool isHex(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

inline bool isIdentPart(char c) { return isIdentStart(c) || isDigit(c); }

// Escapes control characters and quotes so a string can be printed on one line.
inline std::string escapeForPrint(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': out += "\\0"; break;
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

}  // namespace cp3test
