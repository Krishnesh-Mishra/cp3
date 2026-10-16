// =============================================================================
// src/codegen/src/mangle.cpp — PRIVATE codegen name mangling (STYLE.md §8).
//
// Implements the identifier/namespace helpers declared in mangle.h. Behavior is
// frozen: these spellings appear verbatim in the generated C++.
// =============================================================================
#include "mangle.h"

#include <cctype>
#include <string>
#include <unordered_set>

namespace cp3 {
namespace detail {

std::string sanitizeIdent(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "module";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front()))) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string upperSnake(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

bool isCppKeyword(const std::string& name) {
    static const std::unordered_set<std::string> kKeywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
        "public", "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
        // Reserve names that would collide with generated scaffolding.
        "main", "std", "cp3",
    };
    return kKeywords.count(name) != 0;
}

std::string mapIdent(const std::string& name) {
    return isCppKeyword(name) ? name + "_cp3" : name;
}

std::string suggestNamespace(const std::string& moduleName) {
    return "cp3_mod_" + sanitizeIdent(moduleName);
}

}  // namespace detail
}  // namespace cp3
