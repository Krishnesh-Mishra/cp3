// =============================================================================
// src/cli/src/native/metadata.cpp - `native.json` read/write and validation.
//
// OWNERSHIP: cli module (native-install). Implements native/metadata.hpp.
// =============================================================================
#include "native/libclang/metadata.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/host/native_bindings.hpp"
#include "detail/cache/checksum.hpp"
#include "detail/pkg/json.hpp"

namespace cp3::cli::native {

namespace {

std::string arrayOfStrings(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += jsonQuote(values[i]);
    }
    out += "]";
    return out;
}

std::string typeJson(const Type& t) {
    switch (t.kind) {
        case TypeKind::Number: return "{\"kind\": \"number\"}";
        case TypeKind::String: return "{\"kind\": \"string\"}";
        case TypeKind::Boolean: return "{\"kind\": \"boolean\"}";
        case TypeKind::Void: return "{\"kind\": \"void\"}";
        case TypeKind::Array: {
            const Type elem = t.parts.empty() ? Type::unknown() : t.parts.front();
            return "{\"kind\": \"array\", \"elem\": " + typeJson(elem) + "}";
        }
        case TypeKind::Class: {
            if (!isNativeClassType(t) && t.name == "Buffer") {
                return "{\"kind\": \"buffer\"}";
            }
            const std::string cpp =
                isNativeClassType(t)
                    ? nativeTypeCpp(t.name, kNativeClassTypePrefix)
                    : t.name;
            return "{\"kind\": \"class\", \"cpp\": " + jsonQuote(cpp) + "}";
        }
        case TypeKind::Enum: {
            const std::string cpp =
                isNativeEnumType(t)
                    ? nativeTypeCpp(t.name, kNativeEnumTypePrefix)
                    : t.name;
            return "{\"kind\": \"enum\", \"cpp\": " + jsonQuote(cpp) + "}";
        }
        default: return "{\"kind\": \"unknown\"}";
    }
}

std::string paramKindName(NativeParamKind kind) {
    switch (kind) {
        case NativeParamKind::CStr: return "cstr";
        case NativeParamKind::ByteBuffer: return "byteBuffer";
        case NativeParamKind::NumberArray: return "numberArray";
        default: return "value";
    }
}

std::string functionJson(const NativeFunction& fn, const std::string& indent) {
    const std::string inner = indent + "  ";
    std::string out = indent + "{\n";
    out += inner + "\"name\": " + jsonQuote(fn.name) + ",\n";
    out += inner + "\"cppName\": " + jsonQuote(fn.cppName) + ",\n";
    out += inner + "\"returnType\": " + typeJson(fn.returnType) + ",\n";
    out += inner + "\"returnsCStr\": " +
           (fn.returnsCStr ? "true" : "false") + ",\n";
    out += inner + "\"isStatic\": " + (fn.isStatic ? "true" : "false") + ",\n";
    out += inner + "\"params\": [";
    if (!fn.params.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            const NativeParam& p = fn.params[i];
            out += inner + "  {\"name\": " + jsonQuote(p.name) + ", \"kind\": " +
                   jsonQuote(paramKindName(p.kind)) + ", \"type\": " +
                   typeJson(p.type) + ", \"cppType\": " + jsonQuote(p.cppType) +
                   ", \"cppPointer\": " + jsonQuote(p.cppPointer) +
                   ", \"cppArgCount\": " + std::to_string(p.cppArgCount) + "}";
            if (i + 1 < fn.params.size()) out += ",";
            out += "\n";
        }
        out += inner;
    }
    out += "]\n" + indent + "}";
    return out;
}

std::string classJson(const NativeBoundClass& cls, const std::string& indent) {
    const std::string inner = indent + "  ";
    std::string out = indent + "{\n";
    out += inner + "\"name\": " + jsonQuote(cls.name) + ",\n";
    out += inner + "\"cpp\": " + jsonQuote(cls.cppQualified) + ",\n";
    out += inner + "\"fields\": [";
    if (!cls.fields.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < cls.fields.size(); ++i) {
            const NativeField& f = cls.fields[i];
            out += inner + "  {\"name\": " + jsonQuote(f.name) +
                   ", \"cppName\": " + jsonQuote(f.cppName) +
                   ", \"type\": " + typeJson(f.type) +
                   ", \"cppType\": " + jsonQuote(f.cppType) + "}";
            if (i + 1 < cls.fields.size()) out += ",";
            out += "\n";
        }
        out += inner;
    }
    out += "],\n";
    out += inner + "\"methods\": [";
    if (!cls.methods.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < cls.methods.size(); ++i) {
            out += functionJson(cls.methods[i], inner + "  ");
            if (i + 1 < cls.methods.size()) out += ",";
            out += "\n";
        }
        out += inner;
    }
    out += "]\n" + indent + "}";
    return out;
}

std::string enumJson(const NativeEnum& en, const std::string& indent) {
    const std::string inner = indent + "  ";
    std::string out = indent + "{\n";
    out += inner + "\"name\": " + jsonQuote(en.name) + ",\n";
    out += inner + "\"cpp\": " + jsonQuote(en.cppQualified) + ",\n";
    out += inner + "\"members\": [";
    if (!en.members.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < en.members.size(); ++i) {
            out += inner + "  {\"name\": " + jsonQuote(en.members[i].name) +
                   ", \"cppName\": " + jsonQuote(en.members[i].cppName) + "}";
            if (i + 1 < en.members.size()) out += ",";
            out += "\n";
        }
        out += inner;
    }
    out += "]\n" + indent + "}";
    return out;
}
}  // namespace

std::string renderMetadata(const NativeModule& module,
                           const std::string& indexText) {
    std::string out = "{\n";
    out += "  \"schema\": " + jsonQuote(kNativeMetadataSchema) + ",\n";
    out += "  \"id\": " + jsonQuote(module.id) + ",\n";
    out += "  \"source\": " + jsonQuote(module.source) + ",\n";
    out += "  \"includeDirs\": " + arrayOfStrings(module.includeDirs) + ",\n";
    out += "  \"defines\": " + arrayOfStrings(module.defines) + ",\n";
    out += "  \"headers\": " + arrayOfStrings(module.headers) + ",\n";
    out += "  \"libDirs\": " + arrayOfStrings(module.libDirs) + ",\n";
    out += "  \"linkNames\": " + arrayOfStrings(module.linkNames) + ",\n";
    out += "  \"staticLibraries\": " + arrayOfStrings(module.staticLibraries) +
           ",\n";
    out += "  \"sharedLibraries\": " + arrayOfStrings(module.sharedLibraries) +
           ",\n";
    out += "  \"runtimeDlls\": " + arrayOfStrings(module.runtimeDlls) + ",\n";
    out += "  \"abi\": " + jsonQuote(module.abi) + ",\n";
    out += "  \"target\": " + jsonQuote(module.target) + ",\n";
    out += "  \"libclangVersion\": " + jsonQuote(module.libclangVersion) + ",\n";
    out += "  \"contentHash\": " + jsonQuote(sha256Hex(indexText)) + ",\n";
    out += "  \"functions\": [";
    if (!module.functions.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < module.functions.size(); ++i) {
            out += functionJson(module.functions[i], "    ");
            if (i + 1 < module.functions.size()) out += ",";
            out += "\n";
        }
        out += "  ";
    }
    out += "],\n  \"classes\": [";
    if (!module.classes.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < module.classes.size(); ++i) {
            out += classJson(module.classes[i], "    ");
            if (i + 1 < module.classes.size()) out += ",";
            out += "\n";
        }
        out += "  ";
    }
    out += "],\n  \"enums\": [";
    if (!module.enums.empty()) {
        out += "\n";
        for (std::size_t i = 0; i < module.enums.size(); ++i) {
            out += enumJson(module.enums[i], "    ");
            if (i + 1 < module.enums.size()) out += ",";
            out += "\n";
        }
        out += "  ";
    }
    out += "],\n  \"rejected\": " + arrayOfStrings(module.rejected) + "\n}\n";
    return out;
}

}  // namespace cp3::cli::native
