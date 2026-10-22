// =============================================================================
// src/common/src/native_bindings_read.cpp - the `native.json` reader.
//
// OWNERSHIP: common module. Implements the reader half of
// cp3/native_bindings.hpp: it parses the metadata `cp3c native-install`
// wrote, validates it against the "cp3-native-binding/1" schema, and checks
// the recorded `contentHash` against the on-disk `index.cp3`.
//
// The document's fields are read by the private field readers
// (detail/native_fields.hpp); the JSON reader is private to the common module
// (detail/native_json.hpp). The package manager and the language server keep
// their own.
// =============================================================================
#include "cp3/native_bindings.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "cp3/checksum.hpp"
#include "detail/native_fields.hpp"
#include "detail/native_json.hpp"

namespace cp3 {

namespace {

namespace fs = std::filesystem;

using detail::JsonValue;

// Reads a whole file as text. False when it cannot be opened.
bool readTextFile(const fs::path& path, std::string& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

}  // namespace

std::string nativeStaleRemedy(const std::string& id) {
    return "re-run 'cp3 native-install <sdk> --name " + id + " --force'";
}

NativeReadResult readNativeMetadata(std::string_view text) {
    NativeReadResult result;
    detail::JsonValue root;
    std::string err;
    if (!detail::parseJson(text, root, err)) {
        result.status = NativeReadStatus::ParseError;
        result.message = "the native binding metadata is not valid JSON: " + err;
        return result;
    }
    if (!root.isObject()) {
        result.status = NativeReadStatus::ParseError;
        result.message = "the native binding metadata is not a JSON object";
        return result;
    }
    const JsonValue* schema = root.find("schema");
    if (schema == nullptr || !schema->isString()) {
        result.status = NativeReadStatus::SchemaMismatch;
        result.message = "the native binding metadata has no 'schema' field";
        return result;
    }
    if (schema->text != kNativeMetadataSchema) {
        result.status = NativeReadStatus::SchemaMismatch;
        result.message = "the native binding metadata has schema '" + schema->text +
                         "', but this compiler reads '" +
                         std::string(kNativeMetadataSchema) + "'";
        return result;
    }
    detail::FieldError fieldError;
    if (!detail::readNativeModule(root, result.module, fieldError)) {
        result.status = NativeReadStatus::Invalid;
        result.message = "the native binding metadata is invalid: " +
                         fieldError.message;
        return result;
    }
    return result;
}

NativeReadResult readNativeBinding(const fs::path& dir) {
    NativeReadResult result;
    const fs::path jsonPath = dir / "native.json";
    std::error_code ec;
    if (!fs::exists(jsonPath, ec)) {
        result.status = NativeReadStatus::Missing;
        result.message = "no native binding at '" + dir.generic_string() + "'";
        return result;
    }
    std::string text;
    if (!readTextFile(jsonPath, text)) {
        result.status = NativeReadStatus::IoError;
        result.message = "cannot read '" + jsonPath.generic_string() + "'";
        return result;
    }
    result = readNativeMetadata(text);
    if (result.status != NativeReadStatus::Ok) return result;

    // The staleness check: the recorded contentHash is the SHA-256 of the
    // exact index.cp3 bytes. A binding that records none is not checked (a
    // hand-written binding may carry no fingerprint).
    const std::string& expected = result.module.contentHash;
    if (expected.empty()) return result;
    const std::string id = result.module.id;
    std::string indexText;
    if (!readTextFile(dir / "index.cp3", indexText)) {
        result.status = NativeReadStatus::Invalid;
        result.message = "the native binding '" + id +
                         "' has no readable index.cp3 next to its native.json";
        result.remedy = nativeStaleRemedy(id);
        return result;
    }
    if (sha256Hex(indexText) == expected) return result;
    result.status = NativeReadStatus::Stale;
    result.module = NativeModule{};
    result.message = "the native binding '" + id +
                     "' is stale: its index.cp3 no longer matches the "
                     "contentHash recorded in native.json";
    result.remedy = nativeStaleRemedy(id);
    return result;
}

const NativeFunction* findNativeFunction(const NativeModule& module,
                                         const std::string& name) {
    for (const NativeFunction& fn : module.functions) {
        if (fn.name == name) return &fn;
    }
    return nullptr;
}

const NativeBoundClass* findNativeClass(const NativeModule& module,
                                         const std::string& name) {
    for (const NativeBoundClass& cls : module.classes) {
        if (cls.name == name) return &cls;
    }
    return nullptr;
}

const NativeEnum* findNativeEnum(const NativeModule& module,
                                 const std::string& name) {
    for (const NativeEnum& en : module.enums) {
        if (en.name == name) return &en;
    }
    return nullptr;
}

}  // namespace cp3
