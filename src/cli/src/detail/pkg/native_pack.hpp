// =============================================================================
// src/cli/src/detail/pkg/native_pack.hpp - native assets in a `.cp3pak`.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// A package that imports a native binding needs more than its generated cp3
// surface and C++ source: the binding's `native.json`, its declaration file,
// the C++ headers, import libraries and runtime DLLs the generated code needs.
// Beta-one published those as a separate platform asset; v0.2 embeds them in
// the `.cp3pak` so one archive installs and builds everywhere the package is
// supported.
//
// Embedded assets are relocated under `.cp3/native/<id>/` and every recorded
// path in the binding metadata is rewritten to that self-contained location.
// Installing the archive therefore copies exactly those members to the
// project's `.cp3/native/<id>/`, independent of the author's filesystem and
// without colliding with another package's `native/` tree.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/archive/tar.hpp"

namespace cp3::cli {

// True when an archive member belongs to an embedded native binding.
bool isNativeArchiveMember(const std::string& name);

// The binding ids an archive carries (`<id>` from `.cp3/native/<id>/native.json`).
std::vector<std::string> nativeBindingIdsInArchive(
    const std::vector<TarEntry>& members);

// The binding ids a package directory carries.
std::vector<std::string> nativeBindingIdsInDirectory(
    const std::filesystem::path& packageRoot);

// The binding ids imported by a cp3 source/surface (`import native "<id>"`).
std::vector<std::string> nativeBindingIdsInText(const std::string& text);

// Reads the bindings named by `ids` from `projectRoot` and appends archive
// members for them. Recorded include/library directories and explicit
// libraries/DLLs are copied into `.cp3/native/<id>/...`; `native.json` is
// rewritten to point at those locations (its contentHash still covers the
// binding's own, unchanged `index.cp3`).
bool collectNativeBindingMembers(const std::filesystem::path& projectRoot,
                                 const std::vector<std::string>& ids,
                                 std::vector<TarEntry>& entries,
                                 std::string& err);

// Writes every `.cp3/native/**` member to `projectRoot` (the package's own
// directory never owns the binding). Other members are ignored.
bool installNativeMembers(const std::vector<TarEntry>& members,
                          const std::filesystem::path& projectRoot,
                          std::string& err);

// Installs the native bindings of a package directory into `projectRoot`.
// Used by directory installs and directory links, whose package tree is kept
// in place (and whose `.cp3/native` therefore is not copied by `copyTree`).
bool installNativeBindingsFromDirectory(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& projectRoot, std::string& err);

// The compile flags a binding records: include roots and preprocessor
// defines. `projectRoot` is the root the metadata's relative paths resolve
// against (the package author's project for a pack, or a scratch root holding
// extracted archive members for a recompile).
bool nativeBuildFlagsFromProject(const std::filesystem::path& projectRoot,
                                 const std::vector<std::string>& ids,
                                 std::vector<std::string>& includeDirs,
                                 std::vector<std::string>& defines,
                                 std::string& err);

// Materializes an archive's embedded bindings under `workRoot` and returns
// the same compile flags. Used by `cp3 recompile`, which has no project tree.
bool nativeBuildFlagsFromArchive(const std::vector<TarEntry>& members,
                                 const std::filesystem::path& workRoot,
                                 std::vector<std::string>& includeDirs,
                                 std::vector<std::string>& defines,
                                 std::string& err);

// Adds `import native "<id>" as <alias>;` to a generated package surface for
// every id it does not already import. The alias is namespaced so it cannot
// collide with an export. This is what makes a consumer load the embedded
// binding even when the original native import lived in a non-exported module.
std::string ensureNativeImports(std::string surface,
                                const std::vector<std::string>& ids);

}  // namespace cp3::cli
