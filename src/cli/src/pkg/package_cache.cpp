// =============================================================================
// src/cli/src/pkg/package_cache.cpp - the consumer side of the binary cache
// DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/package_cache.hpp.
//
// The rule is simple and is the whole point of the cache: a package that
// carries a cached binary for the consumer's platform is used as is; a
// package that does not falls back to its own C++ source; a package whose
// author excluded the source and shipped no binary for this platform cannot
// be used at all, and says so by name.
// =============================================================================
#include "detail/package_cache.hpp"

#include <cstdio>
#include <system_error>
#include <vector>

#include "detail/util.hpp"

namespace cp3::cli {

namespace {

// The cache manifest of a platform inside an installed package directory.
bool readInstalledCache(const fs::path& packageDir, const PlatformId& platform,
                        PlatformCacheManifest& out) {
    const fs::path path = packageDir / fs::path("pak") /
                          fs::path(platformIdText(platform)) / "cache.json";
    std::string text;
    if (!readTextFile(path, text)) return false;
    std::string err;
    return parsePlatformCacheManifest(text, out, err);
}

// The platforms an installed package directory carries a cache for.
std::vector<PlatformId> installedPlatforms(const fs::path& packageDir) {
    std::vector<PlatformId> out;
    const fs::path pakDir = packageDir / "pak";
    std::error_code ec;
    if (!fs::is_directory(pakDir, ec)) return out;
    for (fs::directory_iterator it(pakDir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;
        PlatformId id;
        std::string err;
        if (!parsePlatformId(it->path().filename().string(), id, err)) continue;
        if (!fs::is_regular_file(it->path() / "cache.json", ec)) continue;
        out.push_back(id);
    }
    return out;
}

}  // namespace

bool installedPlatformCache(const fs::path& packageDir,
                             PlatformCacheManifest& out) {
    return readInstalledCache(packageDir, currentPlatform(), out);
}

PackageArtifactMode selectPackageArtifact(const fs::path& packageDir,
                                           PlatformCacheManifest& cache) {
    if (installedPlatformCache(packageDir, cache)) {
        return PackageArtifactMode::CachedArtifact;
    }
    return PackageArtifactMode::SourceFallback;
}

std::string excludedSourceError(const std::string& name,
                                 const std::string& version) {
    std::string out = "Installation failed for '" + name + "@" + version + "'\n";
    out += "  No precompiled binary found for your platform (" +
           platformIdText(currentPlatform()) + ").\n";
    out += "  The author excluded the C++ source from this package "
           "(excludeSource: true).\n";
    out += "  Ask the author to add support for your platform, or to drop "
           "excludeSource.\n";
    return out;
}

bool checkPackageUsable(const std::string& name, const std::string& version,
                         const JsonValue& manifest, bool hasPlatformCache,
                         std::string& err) {
    if (!excludeSourceRequested(manifest)) return true;
    if (hasPlatformCache) return true;
    err = excludedSourceError(name, version);
    return false;
}

void reportPackageArtifactMode(const std::string& name,
                               const fs::path& packageDir) {
    const std::vector<PlatformId> platforms = installedPlatforms(packageDir);
    if (platforms.empty()) return;
    PlatformCacheManifest cache;
    if (selectPackageArtifact(packageDir, cache) ==
        PackageArtifactMode::CachedArtifact) {
        std::fprintf(stderr,
                     "cp3c: '%s' carries a cached binary for %s\n",
                     name.c_str(), platformIdText(currentPlatform()).c_str());
        return;
    }
    std::fprintf(stderr,
                 "cp3c: '%s' has no cached binary for %s; the C++ source is "
                 "the fallback\n",
                 name.c_str(), platformIdText(currentPlatform()).c_str());
}

}  // namespace cp3::cli
