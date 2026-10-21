// =============================================================================
// src/cli/src/detail/install.hpp - local package install DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/install.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines). Shared by `cp3 install` and the GitHub release
// install path: both end with a `.cp3pak` or an unpacked directory under
// .cp3/packages/<name>/.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

#include "detail/lockfile.hpp"

namespace cp3::cli {

// Reads a directory package's or a `.cp3pak`'s name/version without copying or
// extracting anything. Used to check link conflicts before a side effect.
bool inspectPackageSource(const std::filesystem::path& source, std::string& name,
                          std::string& version, std::string& err);

// True when `source` is a directory or a regular file.
bool packageSourceExists(const std::filesystem::path& source);

// Installs a local directory or `.cp3pak` into .cp3/packages/<name> and fills
// the lock fields for it.
bool installLocalPackage(const std::filesystem::path& source,
                         const std::filesystem::path& projectRoot,
                         LockEntry& entry, std::string& err);

// Installs one `.cp3pak` archive. The caller may add source metadata (GitHub
// repo/tag/asset) to the returned entry.
bool installArchivePackage(const std::filesystem::path& archive,
                           const std::filesystem::path& projectRoot,
                           LockEntry& entry, std::string& err);

}  // namespace cp3::cli
