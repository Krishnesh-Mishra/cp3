// =============================================================================
// cp3/native_types.cpp - the native type-name table and widening rules.
//
// See cp3/native_types.hpp. The table is data, not a pair of ad-hoc switches:
// sema and codegen resolve the same spelling to the same canonical id, so the
// C++ spelling a program runs with is exactly the type sema checked.
// =============================================================================
#include "cp3/host/native_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/suggest.hpp"

namespace cp3 {
namespace {

using NC = NativeClass;

enum class Sys { Rust, Cpp, Ts };

// The supported native value types. `bits` uses sizeof for the platform-width
// C++ types so the widening table agrees with the C++ compiler that builds the
// generated program.
const NativeTypeInfo kInfos[] = {
    {"char", "char", NC::Integer, 8, true, 10},
    {"i8", "std::int8_t", NC::Integer, 8, true, 10},
    {"i16", "std::int16_t", NC::Integer, 16, true, 20},
    {"i32", "std::int32_t", NC::Integer, 32, true, 30},
    {"i64", "std::int64_t", NC::Integer, 64, true, 50},
    {"u8", "std::uint8_t", NC::Integer, 8, false, 10},
    {"u16", "std::uint16_t", NC::Integer, 16, false, 20},
    {"u32", "std::uint32_t", NC::Integer, 32, false, 30},
    {"u64", "std::uint64_t", NC::Integer, 64, false, 50},
    {"long", "long", NC::Integer, static_cast<int>(sizeof(long) * 8), true, 40},
    {"unsigned long", "unsigned long", NC::Integer,
     static_cast<int>(sizeof(unsigned long) * 8), false, 40},
    {"f32", "float", NC::Float, 32, true, 60},
};

// One accepted spelling. `bool`, `f64`/`double`, `void`, `std::string` and the
// TypeScript names are special-cased in `resolve` because they map onto an
// existing cp3 type rather than a NativeTypeInfo row.
struct AliasRow {
    Sys sys;
    const char* spelling;
    const char* id;
};

const AliasRow kAliases[] = {
    // Rust numeric names.
    {Sys::Rust, "i8", "i8"},
    {Sys::Rust, "i16", "i16"},
    {Sys::Rust, "i32", "i32"},
    {Sys::Rust, "i64", "i64"},
    {Sys::Rust, "isize", "i64"},
    {Sys::Rust, "u8", "u8"},
    {Sys::Rust, "u16", "u16"},
    {Sys::Rust, "u32", "u32"},
    {Sys::Rust, "u64", "u64"},
    {Sys::Rust, "usize", "u64"},
    {Sys::Rust, "f32", "f32"},
    // C++ spellings, including every standard spelling of each integer rank
    // and the fixed-width aliases.
    {Sys::Cpp, "char", "char"},
    {Sys::Cpp, "signed char", "i8"},
    {Sys::Cpp, "unsigned char", "u8"},
    {Sys::Cpp, "short", "i16"},
    {Sys::Cpp, "short int", "i16"},
    {Sys::Cpp, "signed short", "i16"},
    {Sys::Cpp, "signed short int", "i16"},
    {Sys::Cpp, "unsigned short", "u16"},
    {Sys::Cpp, "unsigned short int", "u16"},
    {Sys::Cpp, "int", "i32"},
    {Sys::Cpp, "signed", "i32"},
    {Sys::Cpp, "signed int", "i32"},
    {Sys::Cpp, "unsigned", "u32"},
    {Sys::Cpp, "unsigned int", "u32"},
    {Sys::Cpp, "long", "long"},
    {Sys::Cpp, "long int", "long"},
    {Sys::Cpp, "signed long", "long"},
    {Sys::Cpp, "signed long int", "long"},
    {Sys::Cpp, "unsigned long", "unsigned long"},
    {Sys::Cpp, "unsigned long int", "unsigned long"},
    {Sys::Cpp, "long long", "i64"},
    {Sys::Cpp, "long long int", "i64"},
    {Sys::Cpp, "signed long long", "i64"},
    {Sys::Cpp, "signed long long int", "i64"},
    {Sys::Cpp, "unsigned long long", "u64"},
    {Sys::Cpp, "unsigned long long int", "u64"},
    {Sys::Cpp, "int8_t", "i8"},
    {Sys::Cpp, "int16_t", "i16"},
    {Sys::Cpp, "int32_t", "i32"},
    {Sys::Cpp, "int64_t", "i64"},
    {Sys::Cpp, "uint8_t", "u8"},
    {Sys::Cpp, "uint16_t", "u16"},
    {Sys::Cpp, "uint32_t", "u32"},
    {Sys::Cpp, "uint64_t", "u64"},
    {Sys::Cpp, "std::int8_t", "i8"},
    {Sys::Cpp, "std::int16_t", "i16"},
    {Sys::Cpp, "std::int32_t", "i32"},
    {Sys::Cpp, "std::int64_t", "i64"},
    {Sys::Cpp, "std::uint8_t", "u8"},
    {Sys::Cpp, "std::uint16_t", "u16"},
    {Sys::Cpp, "std::uint32_t", "u32"},
    {Sys::Cpp, "std::uint64_t", "u64"},
    {Sys::Cpp, "size_t", "u64"},
    {Sys::Cpp, "std::size_t", "u64"},
    {Sys::Cpp, "intptr_t", "i64"},
    {Sys::Cpp, "uintptr_t", "u64"},
    {Sys::Cpp, "ptrdiff_t", "i64"},
    {Sys::Cpp, "float", "f32"},
};

std::string normalizeSpelling(const std::string& raw) {
    std::string out;
    bool pendingSpace = false;
    for (char c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(c);
    }
    return out;
}

const char* vocabularyName(Sys sys) {
    switch (sys) {
        case Sys::Rust: return "Rust";
        case Sys::Cpp: return "C++";
        case Sys::Ts: return "TypeScript";
    }
    return "native";
}

std::string prefixOf(Sys sys) {
    switch (sys) {
        case Sys::Rust: return "r_";
        case Sys::Cpp: return "c_";
        case Sys::Ts: return "ts_";
    }
    return std::string();
}

std::vector<std::string> candidatesFor(Sys sys) {
    std::vector<std::string> out;
    if (sys == Sys::Rust) {
        out.push_back("bool");
        out.push_back("String");
        out.push_back("f64");
    } else if (sys == Sys::Cpp) {
        out.push_back("bool");
        out.push_back("double");
        out.push_back("std::string");
        out.push_back("void");
    } else {
        out.push_back("boolean");
        out.push_back("null");
        out.push_back("number");
        out.push_back("string");
        out.push_back("undefined");
        out.push_back("unknown");
        out.push_back("void");
    }
    for (const AliasRow& row : kAliases) {
        if (row.sys == sys) out.push_back(row.spelling);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

const char* aliasId(Sys sys, const std::string& name) {
    for (const AliasRow& row : kAliases) {
        if (row.sys == sys && name == row.spelling) return row.id;
    }
    return nullptr;
}

bool isUnsupported(Sys sys, const std::string& name) {
    if (sys == Sys::Rust) {
        return name == "i128" || name == "u128" || name == "char" ||
               name == "str";
    }
    if (sys == Sys::Cpp) {
        return name == "long double" || name == "wchar_t" || name == "char8_t" ||
               name == "char16_t" || name == "char32_t" || name == "auto" ||
               name == "std::string_view";
    }
    return name == "any" || name == "object" || name == "bigint" ||
           name == "symbol";
}

std::string unsupportedMessage(Sys sys, const std::string& name) {
    if (sys == Sys::Rust) {
        if (name == "i128" || name == "u128") {
            return "the Rust type '" + name +
                   "' has no portable C++ value type; use i64 or u64";
        }
        if (name == "char") {
            return "the Rust type 'char' is not supported as a cp3 value type; "
                   "use u32 for a code point";
        }
        return "the Rust type 'str' is an unsized view, not a plain value "
               "type; use the cp3 'string' type";
    }
    if (sys == Sys::Cpp) {
        if (name == "std::string_view") {
            return "the C++ type 'std::string_view' is a non-owning view, not "
                   "a plain value type; use c_'std::string'";
        }
        return "the C++ type '" + name +
               "' is not a plain cp3 value type; use size_t, int64_t or "
               "double";
    }
    if (name == "any") {
        return "the 'any' type is banned; use 'unknown' or a concrete type";
    }
    if (name == "object") {
        return "the TypeScript 'object' type has no concrete cp3 value shape; "
               "use 'unknown' or a named type";
    }
    return "the TypeScript type '" + name +
           "' has no supported C++ value type; use number or r_'i64'";
}

std::string withSuggestion(std::string message, Sys sys,
                           const std::string& name) {
    const std::vector<std::string> candidates = candidatesFor(sys);
    const std::string best = suggestName(name, candidates);
    if (!best.empty()) message += "; did you mean '" + best + "'?";
    return message;
}

NativeNameResult ok(Type type) {
    NativeNameResult out;
    out.status = NativeNameStatus::Ok;
    out.type = std::move(type);
    return out;
}

NativeNameResult fail(NativeNameStatus status, std::string message) {
    NativeNameResult out;
    out.status = status;
    out.error = std::move(message);
    return out;
}

// The existing cp3 types a native spelling can name.
std::optional<Type> specialType(Sys sys, const std::string& name) {
    if (sys == Sys::Rust) {
        if (name == "bool") return Type::boolean();
        if (name == "f64") return Type::number();
        if (name == "String") return Type::string();
        return std::nullopt;
    }
    if (sys == Sys::Cpp) {
        if (name == "bool") return Type::boolean();
        if (name == "double") return Type::number();
        if (name == "void") return Type::voidType();
        if (name == "std::string") return Type::string();
        return std::nullopt;
    }
    if (name == "number") return Type::number();
    if (name == "string") return Type::string();
    if (name == "boolean") return Type::boolean();
    if (name == "undefined") return Type::undefined();
    if (name == "null") return Type::nullType();
    if (name == "void") return Type::voidType();
    if (name == "unknown") return Type::unknown();
    if (name == "never") return Type::unknown();
    return std::nullopt;
}

NativeNameResult resolve(Sys sys, const std::string& raw, bool prefixed) {
    const std::string name = normalizeSpelling(raw);
    const std::string shown =
        prefixed ? prefixOf(sys) + "'" + name + "'" : "'" + name + "'";
    if (name.empty()) {
        return fail(NativeNameStatus::Unknown,
                    "empty " + std::string(vocabularyName(sys)) +
                        " type name" + (prefixed ? " in " + shown : ""));
    }
    if (auto type = specialType(sys, name)) return ok(*type);
    if (const char* id = aliasId(sys, name)) {
        return ok(Type::native(std::string(id)));
    }
    if (isUnsupported(sys, name)) {
        return fail(NativeNameStatus::Unsupported,
                    unsupportedMessage(sys, name));
    }
    std::string message = "unknown " + std::string(vocabularyName(sys)) +
                          " type name '" + name + "'";
    if (prefixed) message += " in " + shown;
    if (sys == Sys::Cpp) {
        message += " (C++ pointer, reference and array spellings are not "
                   "value types)";
    }
    return fail(NativeNameStatus::Unknown,
                withSuggestion(std::move(message), sys, name));
}

const NativeTypeInfo* promote(const NativeTypeInfo& n) {
    if (n.cls == NC::Float) return &n;
    // Integral promotions: every type narrower than int promotes to int.
    if (n.rank < 30) return nativeById("i32");
    return &n;
}

const char* unsignedCounterpart(const std::string& id) {
    if (id == "char" || id == "i8") return "u8";
    if (id == "i16") return "u16";
    if (id == "i32") return "u32";
    if (id == "i64") return "u64";
    if (id == "long") return "unsigned long";
    return nullptr;
}

}  // namespace

const NativeTypeInfo* nativeById(const std::string& id) noexcept {
    for (const NativeTypeInfo& row : kInfos) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

bool isNativeType(const Type& t) noexcept {
    return t.kind == TypeKind::Native;
}

bool isNativeInteger(const Type& t) noexcept {
    if (!isNativeType(t)) return false;
    const NativeTypeInfo* row = nativeById(t.name);
    return row != nullptr && row->cls == NC::Integer;
}

bool isNativeFloat(const Type& t) noexcept {
    if (!isNativeType(t)) return false;
    const NativeTypeInfo* row = nativeById(t.name);
    return row != nullptr && row->cls == NC::Float;
}

NativeNameResult resolveNativeTypeName(const std::string& prefix,
                                       const std::string& spelling) {
    if (prefix == "r") return resolve(Sys::Rust, spelling, true);
    if (prefix == "c") return resolve(Sys::Cpp, spelling, true);
    if (prefix == "ts") return resolve(Sys::Ts, spelling, true);
    return fail(NativeNameStatus::Unsupported,
                "unknown native type-name prefix '" + prefix +
                    "'; use r_, c_, or ts_");
}

NativeNameResult resolvePlainNativeTypeName(TypeSystem ts,
                                            const std::string& spelling) {
    if (ts == TypeSystem::Rust) return resolve(Sys::Rust, spelling, false);
    if (ts == TypeSystem::Cpp) return resolve(Sys::Cpp, spelling, false);
    NativeNameResult out;
    out.status = NativeNameStatus::Unknown;  // the TS vocabulary handles it
    return out;
}

bool isCppTypeContinuation(const std::string& current, const std::string& next) {
    const std::string joined = normalizeSpelling(current + " " + next);
    for (const AliasRow& row : kAliases) {
        if (row.sys != Sys::Cpp) continue;
        const std::string spelling = row.spelling;
        if (spelling.rfind(joined, 0) == 0) return true;
    }
    return false;
}

std::optional<Type> nativeArithmeticResult(const Type& l, const Type& r) {
    if (!isNativeType(l) || !isNativeType(r)) return std::nullopt;
    const NativeTypeInfo* a = nativeById(l.name);
    const NativeTypeInfo* b = nativeById(r.name);
    if (a == nullptr || b == nullptr) return std::nullopt;
    if (a->cls == NC::Float || b->cls == NC::Float) {
        return Type::native("f32");
    }
    const NativeTypeInfo* pa = promote(*a);
    const NativeTypeInfo* pb = promote(*b);
    if (pa->id == pb->id) return Type::native(pa->id);
    if (pa->isSigned == pb->isSigned) {
        return Type::native(pa->rank >= pb->rank ? pa->id : pb->id);
    }
    const NativeTypeInfo* signedType = pa->isSigned ? pa : pb;
    const NativeTypeInfo* unsignedType = pa->isSigned ? pb : pa;
    if (unsignedType->rank >= signedType->rank) {
        return Type::native(unsignedType->id);
    }
    if (signedType->bits > unsignedType->bits) {
        return Type::native(signedType->id);
    }
    const char* widened = unsignedCounterpart(signedType->id);
    if (widened == nullptr) return std::nullopt;
    return Type::native(widened);
}

bool nativeCanRepresentLiteral(const Type& native, double value) {
    if (!isNativeType(native)) return false;
    const NativeTypeInfo* row = nativeById(native.name);
    if (row == nullptr) return false;
    if (row->cls == NC::Float) return std::isfinite(value);
    if (!std::isfinite(value) || std::floor(value) != value) return false;
    const double magnitude = std::ldexp(1.0, row->isSigned ? row->bits - 1
                                                          : row->bits);
    if (row->isSigned) return value >= -magnitude && value < magnitude;
    return value >= 0.0 && value < magnitude;
}

}  // namespace cp3
