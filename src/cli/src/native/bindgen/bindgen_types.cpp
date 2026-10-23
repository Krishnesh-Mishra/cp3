// =============================================================================
// src/cli/src/native/bindgen_types.cpp - C++ type -> cp3 type mapping.
//
// OWNERSHIP: cli module (native-install). Part of the binder (bindgen.hpp).
//
// Beta one keeps the map deliberately small: arithmetic C++ types are cp3
// `number`, `bool` is `boolean`, `std::string`/`const char*` is `string`,
// bound classes/enums keep their identity, and a const arithmetic pointer is
// only legal as the first half of a pointer+length parameter pair.
// =============================================================================
#include <algorithm>
#include <string>

#include "native/bindgen/bindgen_internal.hpp"

namespace cp3::cli::native {

namespace {

bool containsText(const std::string& text, const char* value) {
    return text.find(value) != std::string::npos;
}

bool isConstSpelling(const std::string& spelling) {
    return spelling.find("const") != std::string::npos;
}

bool isArithmeticKind(CXTypeKind kind) {
    switch (kind) {
        case CXType_Bool:
        case CXType_Char_S:
        case CXType_Char_U:
        case CXType_SChar:
        case CXType_UChar:
        case CXType_Short:
        case CXType_UShort:
        case CXType_Int:
        case CXType_UInt:
        case CXType_Long:
        case CXType_ULong:
        case CXType_LongLong:
        case CXType_ULongLong:
        case CXType_Float:
        case CXType_Double:
        case CXType_LongDouble:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool Binder::isStdString(CXType type) const {
    const std::string spelling = typeSpelling(type);
    return containsText(spelling, "std::string") ||
           containsText(spelling, "std::basic_string<char") ||
           containsText(spelling, "basic_string<char");
}

bool Binder::isIntegral(CXType type) const {
    const CXTypeKind kind = lib_.clang_getCanonicalType(type).kind;
    switch (kind) {
        case CXType_Char_S:
        case CXType_Char_U:
        case CXType_SChar:
        case CXType_UChar:
        case CXType_Short:
        case CXType_UShort:
        case CXType_Int:
        case CXType_UInt:
        case CXType_Long:
        case CXType_ULong:
        case CXType_LongLong:
        case CXType_ULongLong:
        case CXType_Int128:
        case CXType_UInt128:
            return true;
        default:
            return false;
    }
}

MappedType Binder::mapType(CXType type, bool parameter) const {
    MappedType mapped;
    if (type.kind == CXType_Invalid) {
        mapped.reason = "invalid type";
        return mapped;
    }
    if (isStdString(type)) {
        mapped.ok = true;
        mapped.type = Type::string();
        mapped.kind = parameter ? NativeParamKind::CStr : NativeParamKind::Value;
        mapped.returnsCStr = false;
        mapped.cppType = parameter ? "const std::string&" : "std::string";
        return mapped;
    }
    if (type.kind >= CXType_FirstBuiltin && type.kind <= CXType_LastBuiltin) {
        return mapBuiltin(type);
    }
    switch (type.kind) {
        case CXType_Pointer:
            return mapPointer(type, parameter);
        case CXType_LValueReference:
        case CXType_RValueReference:
            return mapReference(type);
        case CXType_Record:
        case CXType_Enum:
            return mapRecord(type);
        case CXType_Typedef:
        case CXType_Elaborated:
        case CXType_Attributed:
        case CXType_Unexposed: {
            const CXType canonical = lib_.clang_getCanonicalType(type);
            if (canonical.kind == type.kind) {
                mapped.reason = "unmapped type '" + typeSpelling(type) + "'";
                return mapped;
            }
            return mapType(canonical, parameter);
        }
        case CXType_ConstantArray:
        case CXType_IncompleteArray:
        case CXType_VariableArray:
            mapped.reason = "array types are not supported by value; use a "
                            "pointer parameter followed by its length";
            return mapped;
        case CXType_FunctionProto:
        case CXType_FunctionNoProto:
            mapped.reason = "function pointer types are not supported";
            return mapped;
        default:
            mapped.reason = "type '" + typeSpelling(type) +
                            "' is outside the beta-one native map";
            return mapped;
    }
}

MappedType Binder::mapPointer(CXType type, bool parameter) const {
    MappedType mapped;
    const std::string spelling = typeSpelling(type);
    const CXType pointee = lib_.clang_getPointeeType(type);
    if (isStdString(pointee)) {
        mapped.reason = "pointer to std::string is not supported; pass the string";
        return mapped;
    }
    const CXType canonical = lib_.clang_getCanonicalType(pointee);
    if (canonical.kind == CXType_Void) {
        mapped.reason = "void* cannot be made safe without ownership metadata";
        return mapped;
    }
    // A plain `const char*` is a string. `signed char` / `unsigned char`
    // (and the `int8_t` / `uint8_t` typedefs) are byte buffers when they are
    // followed by a length parameter.
    const bool plainChar =
        canonical.kind == CXType_Char_S || canonical.kind == CXType_Char_U;
    if (plainChar) {
        if (!isConstSpelling(spelling)) {
            mapped.reason = "non-const char pointer is not supported; use const char*";
            return mapped;
        }
        mapped.ok = true;
        mapped.type = Type::string();
        mapped.kind = NativeParamKind::CStr;
        mapped.returnsCStr = !parameter;
        mapped.cppType = "const char*";
        mapped.cppPointer = spelling;
        return mapped;
    }
    if (!isArithmeticKind(canonical.kind)) {
        mapped.reason = "class/struct pointer type '" + spelling +
                        "' needs ownership metadata that beta one does not have";
        return mapped;
    }
    if (!parameter) {
        mapped.reason = "returned pointer type '" + spelling +
                        "' has no classified ownership shape";
        return mapped;
    }
    if (!isConstSpelling(spelling)) {
        mapped.reason = "non-const pointer type '" + spelling +
                        "' is not supported; cp3 Buffer values are immutable, "
                        "so expose input pointers as const";
        return mapped;
    }
    mapped.ok = true;
    mapped.rawPointer = true;
    mapped.type = Type::number();
    mapped.kind = (canonical.kind == CXType_SChar || canonical.kind == CXType_UChar)
                      ? NativeParamKind::ByteBuffer
                      : NativeParamKind::NumberArray;
    mapped.cppType = spelling;
    mapped.cppPointer = spelling;
    return mapped;
}

MappedType Binder::mapReference(CXType type) const {
    MappedType mapped;
    const std::string spelling = typeSpelling(type);
    const CXType pointee = lib_.clang_getPointeeType(type);
    if (!isStdString(pointee)) {
        mapped.reason = "reference type '" + spelling +
                        "' is not supported; pass values or const std::string&";
        return mapped;
    }
    if (!isConstSpelling(spelling)) {
        mapped.reason = "non-const reference type '" + spelling +
                        "' is not supported";
        return mapped;
    }
    mapped.ok = true;
    mapped.type = Type::string();
    mapped.kind = NativeParamKind::CStr;
    mapped.cppType = "const std::string&";
    return mapped;
}

MappedType Binder::mapRecord(CXType type) const {
    MappedType mapped;
    const std::string spelling = typeSpelling(type);
    const CXCursor decl = lib_.clang_getTypeDeclaration(lib_.clang_getCanonicalType(type));
    if (lib_.clang_Cursor_isNull(decl)) {
        mapped.reason = "type '" + spelling + "' has no declaration";
        return mapped;
    }
    const std::string cpp = qualifiedName(decl);
    for (const ClassDecl& candidate : classes_) {
        if (candidate.model.cppQualified != cpp) continue;
        if (candidate.rejected) {
            mapped.reason = "type '" + spelling +
                            "' was rejected (see the binding's rejected list)";
            return mapped;
        }
        mapped.ok = true;
        mapped.type = nativeClassType(module_.id, cpp);
        mapped.cppType = "::" + cpp;
        return mapped;
    }
    for (const EnumDecl& candidate : enums_) {
        if (candidate.model.cppQualified != cpp) continue;
        mapped.ok = true;
        mapped.type = nativeEnumType(module_.id, cpp);
        mapped.cppType = "::" + cpp;
        return mapped;
    }
    mapped.reason = "type '" + spelling +
                    "' is outside the beta-one native map (beta one supports "
                    "simple classes, enums and primitives)";
    return mapped;
}

MappedType Binder::mapBuiltin(CXType type) const {
    MappedType mapped;
    const CXType canonical = lib_.clang_getCanonicalType(type);
    const std::string spelling = typeSpelling(type);
    switch (canonical.kind) {
        case CXType_Void:
            mapped.ok = true;
            mapped.type = Type::voidType();
            mapped.cppType = "void";
            return mapped;
        case CXType_Bool:
            mapped.ok = true;
            mapped.type = Type::boolean();
            mapped.cppType = spelling;
            return mapped;
        case CXType_Char_S:
        case CXType_Char_U:
        case CXType_SChar:
        case CXType_UChar:
        case CXType_Short:
        case CXType_UShort:
        case CXType_Int:
        case CXType_UInt:
        case CXType_Long:
        case CXType_ULong:
        case CXType_LongLong:
        case CXType_ULongLong:
        case CXType_Float:
        case CXType_Double:
        case CXType_LongDouble:
            mapped.ok = true;
            mapped.type = Type::number();
            mapped.cppType = spelling;
            return mapped;
        default:
            mapped.reason = "builtin type '" + spelling +
                            "' is outside the beta-one native map";
            return mapped;
    }
}

Type mappedTypeOf(const MappedType& mapped) {
    if (mapped.returnsCStr) return Type::string();
    return mapped.type;
}

std::string cp3TypeText(const NativeModule& module, const Type& type) {
    switch (type.kind) {
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Void: return "void";
        case TypeKind::Array: {
            const Type elem = type.parts.empty() ? Type::unknown() : type.parts.front();
            return cp3TypeText(module, elem) + "[]";
        }
        case TypeKind::Class: {
            if (isNativeClassType(type)) {
                const std::string cpp =
                    nativeTypeCpp(type.name, kNativeClassTypePrefix);
                for (const NativeBoundClass& cls : module.classes) {
                    if (cls.cppQualified == cpp) return cls.name;
                }
                return cpp;
            }
            return type.name;
        }
        case TypeKind::Enum: {
            if (isNativeEnumType(type)) {
                const std::string cpp =
                    nativeTypeCpp(type.name, kNativeEnumTypePrefix);
                for (const NativeEnum& en : module.enums) {
                    if (en.cppQualified == cpp) return en.name;
                }
                return cpp;
            }
            return type.name;
        }
        case TypeKind::Unknown: return "unknown";
        default: return "unknown";
    }
}

}  // namespace cp3::cli::native
