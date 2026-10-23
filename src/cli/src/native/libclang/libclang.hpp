// =============================================================================
// src/cli/src/native/libclang.hpp - runtime discovery of the system libclang.
//
// OWNERSHIP: cli module (native-install). PRIVATE header.
//
// libclang is loaded dynamically so cp3c still starts (and reports a clear
// remedy) when no LLVM installation exists. Nothing is bundled or extracted:
// the user installs LLVM with the platform package manager, and CP3_LIBCLANG
// may point at a development copy for tests.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "clang-c/Index.h"

namespace cp3::cli::native {

// Every C API entry point the binder uses. The list keeps discovery and loading
// in one place; a missing symbol fails loudly instead of crashing later.
#define CP3_LIBCLANG_FUNCTIONS(X)                       \
    X(clang_createIndex)                                \
    X(clang_disposeIndex)                               \
    X(clang_getClangVersion)                            \
    X(clang_getCString)                                 \
    X(clang_disposeString)                              \
    X(clang_parseTranslationUnit2)                      \
    X(clang_disposeTranslationUnit)                     \
    X(clang_getNumDiagnostics)                          \
    X(clang_getDiagnostic)                              \
    X(clang_defaultDiagnosticDisplayOptions)            \
    X(clang_formatDiagnostic)                           \
    X(clang_getDiagnosticSeverity)                      \
    X(clang_disposeDiagnostic)                          \
    X(clang_getTranslationUnitCursor)                   \
    X(clang_visitChildren)                              \
    X(clang_getCursorKind)                              \
    X(clang_getCursorSpelling)                          \
    X(clang_getCursorDisplayName)                       \
    X(clang_getCursorUSR)                               \
    X(clang_getCursorType)                              \
    X(clang_getCursorResultType)                        \
    X(clang_Cursor_getArgument)                         \
    X(clang_getCursorLocation)                          \
    X(clang_getExpansionLocation)                       \
    X(clang_getFileName)                                \
    X(clang_getCursorExtent)                            \
    X(clang_getRangeStart)                              \
    X(clang_getRangeEnd)                                \
    X(clang_equalCursors)                               \
    X(clang_Cursor_isNull)                              \
    X(clang_getCXXAccessSpecifier)                      \
    X(clang_CXXMethod_isStatic)                         \
    X(clang_CXXMethod_isConst)                          \
    X(clang_CXXMethod_isVirtual)                        \
    X(clang_Cursor_isVariadic)                          \
    X(clang_getNumOverloadedDecls)                      \
    X(clang_getOverloadedDecl)                          \
    X(clang_getTypedefDeclUnderlyingType)               \
    X(clang_getEnumConstantDeclValue)                   \
    X(clang_getCanonicalType)                           \
    X(clang_getPointeeType)                             \
    X(clang_getArrayElementType)                        \
    X(clang_getArraySize)                               \
    X(clang_getResultType)                              \
    X(clang_getNumArgTypes)                             \
    X(clang_getArgType)                                 \
    X(clang_isFunctionTypeVariadic)                     \
    X(clang_getFunctionTypeCallingConv)                 \
    X(clang_getTypeSpelling)                            \
    X(clang_getTypeKindSpelling)                        \
    X(clang_getCursorSemanticParent)                    \
    X(clang_isVirtualBase)                              \
    X(clang_getCursorReferenced)                        \
    X(clang_isCursorDefinition)                         \
    X(clang_getTypeDeclaration)                         \
    X(clang_getTranslationUnitTargetInfo)                \
    X(clang_TargetInfo_dispose)                          \
    X(clang_TargetInfo_getTriple)                        \
    X(clang_TargetInfo_getPointerWidth)                  \
    X(clang_Location_isInSystemHeader)

// The loaded library. A default instance is unusable; use `load()`.
struct LibClang {
    bool loaded = false;
    std::string path;
    std::string version;
    std::vector<std::string> searched;
    void* handle = nullptr;

#define CP3_DECLARE_FN(name) decltype(&::name) name = nullptr;
    CP3_LIBCLANG_FUNCTIONS(CP3_DECLARE_FN)
#undef CP3_DECLARE_FN

    // Discovers and loads libclang. On failure returns a default instance and
    // fills `error` with the searched locations and the platform install
    // command. The returned instance owns the library handle for the process
    // lifetime (leaked deliberately: the binding command is short lived).
    static LibClang load(std::string& error);
};

// The platform install command shown when libclang cannot be found.
std::string libclangInstallHint();

}  // namespace cp3::cli::native
