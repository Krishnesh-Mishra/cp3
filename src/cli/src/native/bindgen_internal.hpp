// =============================================================================
// src/cli/src/native/bindgen_internal.hpp - the binder's private model.
//
// OWNERSHIP: cli module (native-install). PRIVATE header.
//
// One `Binder` walks the libclang AST in two passes: declarations first (so a
// signature can name a class or enum declared later in the header), then the
// signatures and class members. Type mapping lives in bindgen_types.cpp.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "clang-c/Index.h"

#include "cp3/native_bindings.hpp"
#include "native/bindgen.hpp"
#include "native/libclang.hpp"

namespace cp3::cli::native {

struct ClassDecl {
    CXCursor cursor{};
    NativeBoundClass model;
    bool rejected = false;
};

struct EnumDecl {
    CXCursor cursor{};
    NativeEnum model;
};

// A mapped C++ type. `rawPointer` marks a pointer that only becomes legal as
// the first half of a pointer+length parameter pair.
struct MappedType {
    bool ok = false;
    bool rawPointer = false;
    bool returnsCStr = false;
    Type type;
    NativeParamKind kind = NativeParamKind::Value;
    std::string cppType;
    std::string cppPointer;
    std::string reason;
};

class Binder {
public:
    Binder(const LibClang& lib, const BindRequest& request, NativeModule& module);

    bool run(std::string& error);

private:
    // Pass 1: names, class/enum declarations, bases.
    void collectDeclarations(CXCursor cursor, const std::vector<std::string>& ns);
    void collectClass(CXCursor cursor, const std::vector<std::string>& ns);
    void collectEnum(CXCursor cursor, const std::vector<std::string>& ns);
    void collectEnumMembers(CXCursor cursor, NativeEnum& model);
    // Pass 2: free functions and class members.
    void collectDefinitions(CXCursor cursor, const std::vector<std::string>& ns);
    void collectFunction(CXCursor cursor, const std::vector<std::string>& ns);
    void collectClassMembers(ClassDecl& decl);
    void collectField(ClassDecl& decl, CXCursor cursor);
    void collectMethod(ClassDecl& decl, CXCursor cursor, int overloads);
    bool collectSignature(CXCursor cursor, NativeFunction& out);

    // Type mapping (bindgen_types.cpp).
    MappedType mapType(CXType type, bool parameter) const;
    MappedType mapPointer(CXType type, bool parameter) const;
    MappedType mapReference(CXType type) const;
    MappedType mapRecord(CXType type) const;
    MappedType mapBuiltin(CXType type) const;
    bool isIntegral(CXType type) const;
    std::string qualifiedName(CXCursor cursor) const;
    std::string typeSpelling(CXType type) const;
    std::string cursorSpelling(CXCursor cursor) const;
    std::string cursorUSR(CXCursor cursor) const;
    std::string cursorFile(CXCursor cursor) const;
    bool inBoundHeader(CXCursor cursor) const;
    bool isStdString(CXType type) const;

    std::string uniqueName(const std::string& preferred, const std::string& kind);
    void reject(const std::string& text);
    void rejectCursor(CXCursor cursor, const std::string& kind,
                      const std::string& reason);

    const LibClang& lib_;
    const BindRequest& request_;
    NativeModule& module_;
    std::vector<ClassDecl> classes_;
    std::vector<EnumDecl> enums_;
    std::unordered_map<std::string, std::size_t> classIndex_;
    std::unordered_map<std::string, std::size_t> enumIndex_;
    std::unordered_map<std::string, int> functionCounts_;
    std::unordered_set<std::string> seenFunctions_;
    std::unordered_set<std::string> usedNames_;
    std::unordered_set<std::string> rejectedCursors_;
};

// Converts a mapped return/parameter type into the metadata type. Defined in
// bindgen_types.cpp; shared with the class-member pass.
Type mappedTypeOf(const MappedType& mapped);

// The cp3 spelling of a metadata type ("number[]", "Buffer", class name).
std::string cp3TypeText(const NativeModule& module, const Type& type);

// A C++ identifier made safe for the generated cp3 module: a C++ or cp3
// keyword gets a trailing underscore. The metadata keeps the real C++ name.
std::string cp3SafeIdent(const std::string& name);

}  // namespace cp3::cli::native
