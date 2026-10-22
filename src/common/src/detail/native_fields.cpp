// =============================================================================
// src/common/src/detail/native_fields.cpp - the `native.json` field readers.
//
// OWNERSHIP: common module. Implements detail/native_fields.hpp.
//
// Every reader checks the shape it needs and reports the first problem it
// finds through the FieldError: a binding that does not match the
// "cp3-native-binding/1" schema is rejected, never half-read.
// =============================================================================
#include "detail/native_fields.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace cp3::detail {

namespace {

bool stringField(const JsonValue& obj, std::string_view key, std::string& out) {
    const JsonValue* value = obj.find(key);
    if (value == nullptr || !value->isString()) return false;
    out = value->text;
    return true;
}

bool boolField(const JsonValue& obj, std::string_view key, bool& out) {
    const JsonValue* value = obj.find(key);
    if (value == nullptr || !value->isBool()) return false;
    out = value->boolean;
    return true;
}

bool numberField(const JsonValue& obj, std::string_view key, double& out) {
    const JsonValue* value = obj.find(key);
    if (value == nullptr || !value->isNumber()) return false;
    out = value->number;
    return true;
}

// A boolean flag that may be absent (the default is kept).
void boolFlag(const JsonValue& obj, std::string_view key, bool& out) {
    bool value = false;
    if (boolField(obj, key, value)) out = value;
}

bool stringArrayField(const JsonValue& obj, std::string_view key,
                      std::vector<std::string>& out) {
    const JsonValue* value = obj.find(key);
    if (value == nullptr) return true;  // absent is an empty list
    if (!value->isArray()) return false;
    for (const JsonValue& item : value->items) {
        if (!item.isString()) return false;
        out.push_back(item.text);
    }
    return true;
}

NativeParamKind paramKindFrom(const std::string& name) {
    if (name == "cstr") return NativeParamKind::CStr;
    if (name == "byteBuffer") return NativeParamKind::ByteBuffer;
    if (name == "numberArray") return NativeParamKind::NumberArray;
    return NativeParamKind::Value;
}

}  // namespace

bool readNativeType(const JsonValue& node, const std::string& id, Type& out,
                    FieldError& err) {
    if (!node.isObject()) {
        err.message = "a type entry is not an object";
        return false;
    }
    std::string kind;
    if (!stringField(node, "kind", kind)) {
        err.message = "a type entry has no 'kind'";
        return false;
    }
    if (kind == "number") { out = Type::number(); return true; }
    if (kind == "string") { out = Type::string(); return true; }
    if (kind == "boolean") { out = Type::boolean(); return true; }
    if (kind == "void") { out = Type::voidType(); return true; }
    if (kind == "unknown") { out = Type::unknown(); return true; }
    if (kind == "buffer") { out = Type(TypeKind::Class, "Buffer"); return true; }
    if (kind == "array") {
        const JsonValue* elem = node.find("elem");
        Type element;
        if (elem == nullptr || !readNativeType(*elem, id, element, err)) {
            if (err.message.empty()) err.message = "an array type has no element";
            return false;
        }
        out = Type::array(element);
        return true;
    }
    if (kind == "class" || kind == "enum") {
        std::string cpp;
        if (!stringField(node, "cpp", cpp) || cpp.empty()) {
            err.message = "a " + kind + " type has no 'cpp' name";
            return false;
        }
        out = kind == "class" ? nativeClassType(id, cpp)
                              : nativeEnumType(id, cpp);
        return true;
    }
    err.message = "unknown type kind '" + kind + "'";
    return false;
}

bool readNativeParam(const JsonValue& node, const std::string& id,
                     NativeParam& out, FieldError& err) {
    if (!node.isObject()) {
        err.message = "a parameter entry is not an object";
        return false;
    }
    if (!stringField(node, "name", out.name)) {
        err.message = "a parameter has no 'name'";
        return false;
    }
    std::string kind = "value";
    if (!stringField(node, "kind", kind)) {
        err.message = "parameter '" + out.name + "' has no 'kind'";
        return false;
    }
    out.kind = paramKindFrom(kind);
    const JsonValue* type = node.find("type");
    if (type == nullptr || !readNativeType(*type, id, out.type, err)) {
        if (err.message.empty()) {
            err.message = "parameter '" + out.name + "' has no usable 'type'";
        }
        return false;
    }
    double count = 1.0;
    if (!numberField(node, "cppArgCount", count) || count < 1.0) count = 1.0;
    out.cppArgCount = static_cast<std::size_t>(count);
    // A buffer or number array parameter is a pointer plus its length: the
    // recorded argument count must say so, or the lowering would pass the
    // wrong number of C++ arguments.
    const bool pair = out.kind == NativeParamKind::ByteBuffer ||
                      out.kind == NativeParamKind::NumberArray;
    if (pair != (out.cppArgCount == 2)) {
        err.message = "parameter '" + out.name + "' records cppArgCount " +
                      std::to_string(out.cppArgCount) +
                      ", but a " + (pair ? "pointer-and-length" : "single-value") +
                      " parameter needs " + (pair ? "2" : "1");
        return false;
    }
    stringField(node, "cppType", out.cppType);
    stringField(node, "cppPointer", out.cppPointer);
    return true;
}

bool readNativeFunction(const JsonValue& node, const std::string& id,
                        NativeFunction& out, FieldError& err) {
    if (!node.isObject()) {
        err.message = "a function entry is not an object";
        return false;
    }
    if (!stringField(node, "name", out.name) || out.name.empty()) {
        err.message = "a function has no 'name'";
        return false;
    }
    if (!stringField(node, "cppName", out.cppName) || out.cppName.empty()) {
        err.message = "function '" + out.name + "' has no 'cppName'";
        return false;
    }
    const JsonValue* type = node.find("returnType");
    if (type == nullptr || !readNativeType(*type, id, out.returnType, err)) {
        if (err.message.empty()) {
            err.message = "function '" + out.name + "' has no usable return type";
        }
        return false;
    }
    boolFlag(node, "returnsCStr", out.returnsCStr);
    boolFlag(node, "isStatic", out.isStatic);
    const JsonValue* params = node.find("params");
    if (params != nullptr) {
        if (!params->isArray()) {
            err.message = "function '" + out.name + "' has a malformed 'params'";
            return false;
        }
        for (const JsonValue& item : params->items) {
            NativeParam param;
            if (!readNativeParam(item, id, param, err)) return false;
            out.params.push_back(std::move(param));
        }
    }
    return true;
}

bool readNativeClass(const JsonValue& node, const std::string& id,
                     NativeBoundClass& out, FieldError& err) {
    if (!node.isObject()) {
        err.message = "a class entry is not an object";
        return false;
    }
    if (!stringField(node, "name", out.name) || out.name.empty()) {
        err.message = "a class has no 'name'";
        return false;
    }
    if (!stringField(node, "cpp", out.cppQualified)) {
        err.message = "class '" + out.name + "' has no 'cpp' name";
        return false;
    }
    const JsonValue* fields = node.find("fields");
    if (fields != nullptr && fields->isArray()) {
        for (const JsonValue& item : fields->items) {
            NativeField field;
            if (!stringField(item, "name", field.name) ||
                !stringField(item, "cppName", field.cppName)) {
                err.message = "class '" + out.name + "' has a malformed field";
                return false;
            }
            const JsonValue* type = item.find("type");
            if (type == nullptr || !readNativeType(*type, id, field.type, err)) {
                err.message = "class '" + out.name + "' has a field without a type";
                return false;
            }
            stringField(item, "cppType", field.cppType);
            out.fields.push_back(std::move(field));
        }
    }
    const JsonValue* methods = node.find("methods");
    if (methods != nullptr && methods->isArray()) {
        for (const JsonValue& item : methods->items) {
            NativeFunction method;
            if (!readNativeFunction(item, id, method, err)) return false;
            out.methods.push_back(std::move(method));
        }
    }
    return true;
}

bool readNativeEnum(const JsonValue& node, NativeEnum& out, FieldError& err) {
    if (!node.isObject()) {
        err.message = "an enum entry is not an object";
        return false;
    }
    if (!stringField(node, "name", out.name) || out.name.empty()) {
        err.message = "an enum has no 'name'";
        return false;
    }
    if (!stringField(node, "cpp", out.cppQualified)) {
        err.message = "enum '" + out.name + "' has no 'cpp' name";
        return false;
    }
    const JsonValue* members = node.find("members");
    if (members != nullptr && members->isArray()) {
        for (const JsonValue& item : members->items) {
            NativeEnumMember member;
            if (!stringField(item, "name", member.name) ||
                !stringField(item, "cppName", member.cppName)) {
                err.message = "enum '" + out.name + "' has a malformed member";
                return false;
            }
            out.members.push_back(std::move(member));
        }
    }
    return true;
}

bool readNativeModule(const JsonValue& root, NativeModule& out, FieldError& err) {
    if (!root.isObject()) {
        err.message = "the metadata is not a JSON object";
        return false;
    }
    if (!stringField(root, "id", out.id) || out.id.empty()) {
        err.message = "the metadata has no 'id'";
        return false;
    }
    stringField(root, "source", out.source);
    stringField(root, "abi", out.abi);
    stringField(root, "target", out.target);
    stringField(root, "libclangVersion", out.libclangVersion);
    stringField(root, "contentHash", out.contentHash);
    if (!stringArrayField(root, "includeDirs", out.includeDirs) ||
        !stringArrayField(root, "defines", out.defines) ||
        !stringArrayField(root, "headers", out.headers) ||
        !stringArrayField(root, "libDirs", out.libDirs) ||
        !stringArrayField(root, "linkNames", out.linkNames) ||
        !stringArrayField(root, "staticLibraries", out.staticLibraries) ||
        !stringArrayField(root, "sharedLibraries", out.sharedLibraries) ||
        !stringArrayField(root, "runtimeDlls", out.runtimeDlls) ||
        !stringArrayField(root, "rejected", out.rejected)) {
        err.message = "the metadata has a malformed list field";
        return false;
    }
    const std::string& id = out.id;
    const JsonValue* functions = root.find("functions");
    if (functions != nullptr && functions->isArray()) {
        for (const JsonValue& item : functions->items) {
            NativeFunction fn;
            if (!readNativeFunction(item, id, fn, err)) return false;
            out.functions.push_back(std::move(fn));
        }
    }
    const JsonValue* classes = root.find("classes");
    if (classes != nullptr && classes->isArray()) {
        for (const JsonValue& item : classes->items) {
            NativeBoundClass cls;
            if (!readNativeClass(item, id, cls, err)) return false;
            out.classes.push_back(std::move(cls));
        }
    }
    const JsonValue* enums = root.find("enums");
    if (enums != nullptr && enums->isArray()) {
        for (const JsonValue& item : enums->items) {
            NativeEnum en;
            if (!readNativeEnum(item, en, err)) return false;
            out.enums.push_back(std::move(en));
        }
    }
    return true;
}

}  // namespace cp3::detail
