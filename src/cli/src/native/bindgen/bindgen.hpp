// =============================================================================
// src/cli/src/native/bindgen.hpp - libclang-driven C++ header binding.
//
// OWNERSHIP: cli module (native-install). PRIVATE header.
//
// `bindHeaders` turns the declarations in the supplied public headers into a
// `NativeModule` plus the generated `index.cp3` text. It never invokes a C++
// compiler: libclang is the only requirement, and it is loaded at runtime.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/host/native_bindings.hpp"
#include "native/libclang/libclang.hpp"

namespace cp3::cli::native {

struct BindRequest {
    std::vector<std::string> headers;      // absolute public header paths
    std::vector<std::string> includeDirs;  // -I roots
    std::vector<std::string> defines;      // name[=value]
    std::vector<std::string> publicRoots;  // declarations under these are bound
    std::vector<std::string> clangArgs;    // parse-only: target, standard include dirs
    std::string moduleId;
    std::string source;
};

struct BindResult {
    bool ok = false;
    NativeModule module;
    std::string indexText;
    std::string error;  // parse failure, with the libclang diagnostics
};

// Parses one synthetic translation unit that includes every header, binds the
// supported declarations and renders the index module. A header parse error is
// returned in `error`; unsupported declarations become `module.rejected` notes
// (the binding still succeeds when at least one declaration was bound).
BindResult bindHeaders(const LibClang& lib, const BindRequest& request);

// Renders the generated `.cp3/native/<id>/index.cp3` documentation module.
std::string renderIndexModule(const NativeModule& module);

}  // namespace cp3::cli::native
