// =============================================================================
// src/cli/src/detail/package_cache.hpp - the consumer side of the binary cache
// DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/package_cache.cpp.
//
// A source package is compiled from its authored `.cp3` sources on the
// consuming machine - the normal path needs no decision. A hand-made package
// whose author set `excludeSource` has no usable source; when it also ships no
// cached binary for this platform the install fails with a named error
// instead of a raw linker failure. (Binary caches belong to the pre-format-2
// world; a .cp3pak built by this cp3c never carries one.)
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

#include "detail/cache/cache_binary.hpp"
#include "detail/pkg/json.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

// How a consumer would use an installed package on this platform.
enum class PackageArtifactMode {
    SourceFallback,  // the package's sources are compiled (the normal path)
    CachedArtifact,  // a hand-made cached binary for this platform is used
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
