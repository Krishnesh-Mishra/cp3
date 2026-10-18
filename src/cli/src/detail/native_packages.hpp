// =============================================================================
// src/cli/src/detail/native_packages.hpp - std-module native backends (D68).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// DECISIONS D68: every std module whose implementation is backed by a vendored
// native library (third_party/**, DECISIONS D70) owns exactly one row of build
// wiring: extra include roots, extra compile flags, one or more runtime
// translation units, extra link library directories and link flags. A row is
// selected when the program's import graph mentions the module's specifier
// (`ModuleDependency` with `ModuleResolution::Std`), so a program that never
// imports the module compiles the same 5 runtime TUs as before and links no
// third-party archives.
//
// The rows live in the per-family headers `native_std_<family>.hpp` - one file
// per module, so a feature wave only edits its own row - and this unit
// aggregates them. All paths are repository-relative and are resolved against
// `repositoryRoot()` at build time.
//
// Verified flags per backend (include roots, defines, archives and the system
// libraries each archive actually needs) are recorded in third_party/MANIFEST.md
// under "Verified consumer flags"; keep the rows in sync with it.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3::cli::detail {

// One std module's native backend.
struct NativePackage {
    const char* specifier;                    // e.g. "std/crypto"
    std::vector<std::string> includeDirs;     // repo-relative, added as -I
    std::vector<std::string> compileFlags;    // added to every TU compile
    std::vector<std::string> runtimeSources;  // repo-relative .cpp, compiled + linked
    std::vector<std::string> libDirs;         // repo-relative, added as -L
    std::vector<std::string> linkFlags;       // appended at link time
};

// Every std module with a native backend, in a stable order. Rows are defined
// inline by the per-family headers this unit includes.
const std::vector<const NativePackage*>& nativePackages();

// The subset of nativePackages() selected by `importedStdSpecifiers` (the
// specifiers exactly as written, e.g. "std/crypto"), deduplicated and in
// nativePackages() order. Specifiers without a row are ignored: most std
// modules are pure runtime and need no backend.
std::vector<const NativePackage*> selectNativePackages(
    const std::vector<std::string>& importedStdSpecifiers);

}  // namespace cp3::cli::detail
