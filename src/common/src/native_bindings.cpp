// =============================================================================
// src/common/src/native_bindings.cpp - native-model lookups and type names.
//
// OWNERSHIP: common module. Implements cp3/native_bindings.hpp.
// =============================================================================
#include "cp3/native_bindings.hpp"

namespace cp3 {

namespace {

bool startsWith(const std::string& text, const char* prefix) {
    const std::string p(prefix);
    return text.size() >= p.size() && text.compare(0, p.size(), p) == 0;
}

// The text after "<prefix><module>:", or "" when the shape does not match.
std::string cppPart(const std::string& name, const char* prefix) {
    if (!startsWith(name, prefix)) return {};
    const std::string p(prefix);
    const std::size_t sep = name.find(':', p.size());
    if (sep == std::string::npos) return {};
    return name.substr(sep + 1);
}

}  // namespace

const NativeFunction* NativeBoundClass::findMethod(
    const std::string& methodName) const {
    for (const NativeFunction& method : methods) {
        if (method.name == methodName) return &method;
    }
    return nullptr;
}

std::string nativeTypeCpp(const std::string& name, const char* prefix) {
    return cppPart(name, prefix);
}

Type nativeClassType(const std::string& id, const std::string& cppQualified) {
    return Type(TypeKind::Class, std::string(kNativeClassTypePrefix) + id + ":" +
                                     cppQualified);
}

Type nativeEnumType(const std::string& id, const std::string& cppQualified) {
    return Type(TypeKind::Enum, std::string(kNativeEnumTypePrefix) + id + ":" +
                                    cppQualified);
}

bool isNativeClassType(const Type& t) {
    return t.kind == TypeKind::Class && startsWith(t.name, kNativeClassTypePrefix);
}

bool isNativeEnumType(const Type& t) {
    return t.kind == TypeKind::Enum && startsWith(t.name, kNativeEnumTypePrefix);
}

}  // namespace cp3
