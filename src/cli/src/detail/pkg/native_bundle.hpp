// =============================================================================
// src/cli/src/detail/pkg/native_bundle.hpp - companion native-asset DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Beta-one `.cp3pak` archives carry cp3 source only. A package that imports a
// native binding publishes its C++ headers, import library and runtime DLLs as
// a separate release asset (for example `postgres-native-win64.zip`). The
// GitHub install path downloads the asset that matches the machine and
// extracts it at the project root, so `import native "id"` resolves without a
// manual copy step.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/pkg/github.hpp"

namespace cp3::cli {

// True when `packageDir`'s generated entry declares `import native "<id>"`.
bool packageNeedsNativeBinding(const std::filesystem::path& packageDir,
                               std::vector<std::string>& ids, std::string& err);

// True when a release asset name denotes the current platform's native bundle.
bool isNativeBundleForCurrentPlatform(const std::string& assetName);

// Downloads and extracts every matching native bundle into `projectRoot`.
// Returns true when no bundle matched (the package may not need one) or when
// all matches were installed. `err` is set only for a real failure.
bool installCompanionNativeBundles(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& packageDir,
    const std::vector<GithubAsset>& assets,
    const std::filesystem::path& tempRoot, std::string& err);

}  // namespace cp3::cli
