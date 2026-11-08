// =============================================================================
// src/cli/src/detail/native_packages.hpp - std-module native backends (D68).
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// DECISIONS D68: every std module whose implementation is backed by a vendored
// native library (vendor/third_party/**, DECISIONS D70) owns exactly one row of build
// wiring: extra include roots, extra compile flags, one or more runtime
// translation units, extra link library directories and link flags. A row is
// selected when the program's import graph mentions the module's specifier
// (`ModuleDependency` with `ModuleResolution::Std`), so a program that never
// imports the module compiles the same 5 runtime TUs as before and links no
// third-party archives.
//
// The rows live in the per-family headers `src/std/<family>/native.hpp` - one
// file per module, so a feature wave only edits its own row - and this unit
// aggregates them. All paths are repository-relative and are resolved against
// `repositoryRoot()` at build time.
//
// Verified flags per backend (include roots, defines, archives and the system
// libraries each archive actually needs) are recorded in vendor/third_party/MANIFEST.md
// under "Verified consumer flags"; keep the rows in sync with it.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3::cli::detail {

// Platform-specific native archive layout and system link dependencies.
// `id` is the normalized target platform (for example `linux-x64`).
struct NativePackagePlatform {
    const char* id;
    std::vector<std::string> libDirs;
    std::vector<std::string> linkFlags;
    // When non-empty, these flags REPLACE the row's compileFlags for this
    // platform. Retired example: linux-arm64 used to lack OpenSSL archives and
    // an SSL-enabled uSockets build, so it compiled the TLS stub (fails
    // closed at runtime); it now ships the OpenSSL archives like the other
    // three targets and overrides nothing.
    std::vector<std::string> compileFlagsOverride = {};
};

// One std module's native backend.
struct NativePackage {
    const char* specifier;                    // e.g. "std/crypto"
    std::vector<std::string> includeDirs;     // repo-relative, added as -I
    std::vector<std::string> compileFlags;    // added to every TU compile
    std::vector<std::string> runtimeSources;  // repo-relative .cpp, compiled + linked
    std::vector<std::string> libDirs;         // repo-relative, added as -L
    std::vector<std::string> linkFlags;       // appended at link time
    // Parallel to runtimeSources: when entry i is non-empty it is a
    // '|'-separated any-of list of markers that must occur in the generated
    // program text for runtimeSources[i] to be compiled and linked. An empty
    // string (or a missing entry) means "always, when the row is selected", so
    // every row written before this field keeps its old behaviour.
    std::vector<std::string> runtimeSourceMarkers = {};
    // When populated, the matching target must have an explicit archive row.
    // This prevents accidentally linking the host's COFF archives into a
    // Linux target (or silently falling back to the wrong architecture).
    std::vector<NativePackagePlatform> platforms = {};
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

// Core-language backends (D48; detail/native_regex.hpp). These are not
// import-driven: `usesRegex` is true when the generated C++ includes
// cp3/core/regex.hpp, i.e. exactly when the program uses a regex literal or a
// regex value. `usesCollections` is true when the generated C++ includes
// cp3/std/collections.hpp without the program importing "std/collections"
// (the global `Object.fromEntries` / `Object.groupBy` builtins lower onto
// the collections runtime). Returns the selected rows in a stable order
// (empty when none).
std::vector<const NativePackage*> selectCoreNativePackages(
    bool usesRegex, bool usesCollections);

}  // namespace cp3::cli::detail
