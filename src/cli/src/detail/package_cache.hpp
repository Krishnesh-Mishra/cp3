// =============================================================================
// src/cli/src/detail/package_cache.hpp - the consumer side of the binary cache
// DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/package_cache.cpp.
//
// A consumer uses a package's cached binary when the archive carries one for
// the consumer's platform, and falls back to the package's C++ source when it
// does not (CLI-README §5.4). A package whose author excluded the source has
// no fallback at all, so the install fails with a named error instead of a
// raw linker failure.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

#include "detail/cache_binary.hpp"
#include "detail/json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

// How a consumer uses an installed package on this platform.
enum class PackageArtifactMode {
    SourceFallback,  // the package's C++ source is compiled
    CachedArtifact,  // the package's cached binary is used as is
};

// The cached artifact an installed package offers for the current platform.
bool installedPlatformCache(const fs::path& packageDir,
                             PlatformCacheManifest& out);

// The mode an installed package is consumed in on this platform.
PackageArtifactMode selectPackageArtifact(const fs::path& packageDir,
                                           PlatformCacheManifest& cache);

// The error an excluded-source package produces on a platform it has no
// cached binary for (CLI-README §5.4).
std::string excludedSourceError(const std::string& name,
                                 const std::string& version);

// Checks that a package can be used on this platform. `hasPlatformCache` says
// whether the package carries a cached binary for the current platform.
bool checkPackageUsable(const std::string& name, const std::string& version,
                         const JsonValue& manifest, bool hasPlatformCache,
                         std::string& err);

// Reports how an installed package is consumed on this platform. Silent for
// a package that carries no binary cache at all.
void reportPackageArtifactMode(const std::string& name, const fs::path& packageDir);

}  // namespace cp3::cli
