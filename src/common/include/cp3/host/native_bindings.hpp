// =============================================================================
// cp3/native_bindings.hpp - the declaration model behind `native-install`.
//
// OWNERSHIP: common module. `cp3c native-install` builds this model from
// libclang and writes it as `.cp3/native/<id>/native.json`; the generated
// `index.cp3` is the human-readable view. The compiler half (the module
// resolver, sema and codegen) reads the same model back through
// `readNativeBinding` below (spec 19).
//
// This is deliberately the slice the binding command needs: one module, its
// functions, simple classes and enums, and the cp3 value types those map to.
// =============================================================================
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/core/types.hpp"

namespace cp3 {

// Type-name prefixes carried in `Type::name`. The module id after the first
// prefix is how a type finds its binding again:
//   $native:<id>:<qualified-cpp-name>
//   $nativeEnum:<id>:<qualified-cpp-name>
constexpr const char* kNativeClassTypePrefix = "$native:";
constexpr const char* kNativeEnumTypePrefix = "$nativeEnum:";

// How one cp3 argument reaches the native call. Buffer/array parameters consume
// two C++ arguments (pointer and length) and are declared as one cp3 parameter.
enum class NativeParamKind {
    Value,
    CStr,         // std::string / const char* -> cp3 string
    ByteBuffer,   // (const char*|uint8_t*, size_t) -> cp3 Buffer
    NumberArray,  // (const T*, size_t) -> cp3 number[]
};

struct NativeParam {
    std::string name;
    Type type;
    NativeParamKind kind = NativeParamKind::Value;
    // The exact C++ parameter spelling, for reference and diagnostics.
    std::string cppType;
    // Buffer/array pointer spelling, for example "const unsigned char*".
    std::string cppPointer;
    // Number of C++ arguments this one cp3 parameter expands to (1 or 2).
    std::size_t cppArgCount = 1;
};

struct NativeFunction {
    std::string name;       // cp3 name
    std::string cppName;    // qualified free function, or method name
    std::vector<NativeParam> params;
    Type returnType;
    bool returnsCStr = false;
    bool isStatic = false;  // class method callable without a receiver
};

struct NativeField {
    std::string name;     // cp3 name
    std::string cppName;  // the real C++ member spelling
    Type type;
    std::string cppType;
};

struct NativeBoundClass {
    std::string name;          // cp3 name
    std::string cppQualified;  // namespaces::Class
    std::vector<NativeField> fields;
    std::vector<NativeFunction> methods;

    const NativeFunction* findMethod(const std::string& methodName) const;
};

struct NativeEnumMember {
    std::string name;     // cp3 name
    std::string cppName;  // the real C++ enumerator spelling
};

struct NativeEnum {
    std::string name;          // cp3 name
    std::string cppQualified;  // namespaces::Enum
    std::vector<NativeEnumMember> members;
};

struct NativeModule {
    std::string id;
    std::string source;
    std::vector<std::string> includeDirs;
    std::vector<std::string> defines;
    std::vector<std::string> headers;
    std::vector<std::string> libDirs;
    std::vector<std::string> linkNames;
    std::vector<std::string> staticLibraries;
    std::vector<std::string> sharedLibraries;
    std::vector<std::string> runtimeDlls;
    std::string abi;
    std::string target;
    std::string libclangVersion;
    std::string contentHash;
    std::vector<NativeFunction> functions;
    std::vector<NativeBoundClass> classes;
    std::vector<NativeEnum> enums;
    // Declarations bindgen deliberately did not expose, each with the reason.
    std::vector<std::string> rejected;
};

// --- type-name helpers -------------------------------------------------------

// The qualified C++ name encoded after the module id.
std::string nativeTypeCpp(const std::string& name, const char* prefix);

Type nativeClassType(const std::string& id, const std::string& cppQualified);
Type nativeEnumType(const std::string& id, const std::string& cppQualified);

bool isNativeClassType(const Type& t);
bool isNativeEnumType(const Type& t);

// --- lookups -----------------------------------------------------------------

// The free function / class / enum a binding exposes under `name`, or nullptr.
const NativeFunction* findNativeFunction(const NativeModule& module,
                                         const std::string& name);
const NativeBoundClass* findNativeClass(const NativeModule& module,
                                         const std::string& name);
const NativeEnum* findNativeEnum(const NativeModule& module,
                                 const std::string& name);

// --- the reader (compiler half) ----------------------------------------------

// The metadata schema this compiler reads and writes.
inline constexpr const char* kNativeMetadataSchema = "cp3-native-binding/1";

// Why a binding could not be loaded. `Ok` is the only status that carries a
// usable module.
enum class NativeReadStatus {
    Ok,
    Missing,        // the binding directory has no native.json
    IoError,        // the file exists but could not be read
    ParseError,     // the text is not a JSON object
    SchemaMismatch, // `schema` is not "cp3-native-binding/1"
    Invalid,        // a required field is missing or malformed
    Stale,          // `contentHash` does not match the on-disk index.cp3
};

// The outcome of reading one binding.
struct NativeReadResult {
    NativeReadStatus status = NativeReadStatus::Ok;
    // What went wrong (empty when the status is Ok).
    std::string message;
    // What the user can do about it (empty when the status is Ok).
    std::string remedy;
    // The parsed binding (populated only when the status is Ok).
    NativeModule module;
};

// Parses and validates one `native.json` document. The schema must be
// "cp3-native-binding/1"; every function/class/enum is checked for the
// fields the compiler half relies on.
NativeReadResult readNativeMetadata(std::string_view text);

// Reads `<dir>/native.json` and verifies its `contentHash` against the
// on-disk `<dir>/index.cp3`. A binding whose index was edited or replaced
// after the metadata was written is reported as Stale, with the remedy
// `re-run 'cp3 native-install <sdk> --name <id> --force'`.
NativeReadResult readNativeBinding(const std::filesystem::path& dir);

// The remedy text a stale binding reports.
std::string nativeStaleRemedy(const std::string& id);

// One native binding a program imports (`import native "id" as alias`).
struct NativeBinding {
    std::string id;       // the binding id ("demo")
    std::string alias;    // the local alias ("dm")
    std::string dir;      // the binding directory
    NativeModule module;  // the parsed binding
};

}  // namespace cp3
