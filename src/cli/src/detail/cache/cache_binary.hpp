// =============================================================================
// src/cli/src/detail/cache_binary.hpp - the precompiled binary cache DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/cache_binary.cpp.
//
// The cache is the optional per-platform compiled artifact a package may ship
// next to its C++ source (CLI-README §5.4). The archive layout is:
//
//   pak/<platform>/cache.json    the cache manifest of that platform
//   pak/<platform>/<module>.o    the compiled artifact of that module
//
// <platform> is the canonical arch-qualified id of the platform the artifact
// was built for ("windows-x64", "linux-x64", "mac-arm64"); a family-only id
// never names a directory, only a claim.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "detail/pkg/json.hpp"
#include "detail/archive/platform.hpp"
#include "detail/archive/tar.hpp"

namespace cp3::cli {

// The resolved `build.cacheBinary` of a package.
struct CacheBinaryPlan {
    bool requested = false;           // cacheBinary was set at all
    std::vector<PlatformId> claims;   // the normalized platform list
};

// Reads `build.cacheBinary` from a package manifest. `true` means "the
// platform this build runs on"; an array lists platform ids. Returns false
// with a message in `err` for a malformed value.
bool parseCacheBinary(const JsonValue& manifest, CacheBinaryPlan& out,
                      std::string& err);

// True when the manifest asks for the package's C++ source to be dropped.
bool excludeSourceRequested(const JsonValue& manifest);

// One compiled artifact inside a platform's cache.
struct PlatformCacheArtifact {
    std::string module;      // the module it was built from ("src/index")
    std::string member;      // the archive member holding it
    std::string sourceHash;  // sha256 of the compiled C++ source
    std::string headerHash;  // sha256 of the C++ header
};

// The manifest of one platform's cached artifacts.
struct PlatformCacheManifest {
    int formatVersion = 1;
    std::string platform;   // the canonical arch-qualified platform id
    std::string abi;        // the C++ ABI the artifact was built for
    std::string kind;       // the artifact kind ("object")
    std::vector<PlatformCacheArtifact> artifacts;
    std::string compiler;         // the compiler identity ("GCC 13.2.0")
    std::string compilerId;       // the compiler family ("gcc")
    std::string compilerVersion;  // the exact parsed version
    std::string compilerPath;     // the driver the artifact was built with
    std::string profile;          // the build profile ("incremental")
};

std::string platformCacheManifestToJson(const PlatformCacheManifest& manifest);
bool parsePlatformCacheManifest(const std::string& text,
                                PlatformCacheManifest& out, std::string& err);

// The archive member holding a platform's cache manifest.
std::string platformCacheManifestMember(const PlatformId& platform);

// The archive member holding a platform's artifact of `module`.
std::string platformCacheArtifactMember(const PlatformId& platform,
                                         const std::string& module);

// The platforms an archive carries a cache for, in archive order.
std::vector<PlatformId> archiveCachedPlatforms(const std::vector<TarEntry>& members);

// The cache manifest of `platform` in the archive, if it carries one.
bool archiveFindPlatformCache(const std::vector<TarEntry>& members,
                              const PlatformId& platform,
                              PlatformCacheManifest& out);

// True when the archive carries a cache manifest for `platform`.
bool archiveHasPlatformCache(const std::vector<TarEntry>& members,
                             const PlatformId& platform);

// True when every claim is satisfied by one of the cached platforms.
bool claimsSatisfied(const std::vector<PlatformId>& claims,
                     const std::vector<PlatformId>& cached);

// The claims of a claim set that no cached platform satisfies.
std::vector<std::string> unsatisfiedClaims(const std::vector<PlatformId>& claims,
                                           const std::vector<PlatformId>& cached);

// True when the archive still carries the package's C++ source.
bool archiveHasSource(const std::vector<TarEntry>& members);

// The C++ source members of the archive (the "pak/<module>.cpp" files).
std::vector<const TarEntry*> archiveSourceMembers(const std::vector<TarEntry>& members);

// True for a member that is a dropped-source candidate: a ".cpp" under pak/.
bool isPakSourceMember(const std::string& name);

// The module a "pak/<module>.cpp" member was built from ("src/index").
std::string pakSourceModule(const std::string& name);

// Removes the members of `platform`'s cache from `members` and returns the
// index the block occupied (the list size when the platform had no cache).
std::size_t removePlatformCacheMembers(std::vector<TarEntry>& members,
                                       const PlatformId& platform);

// The index a new cache block belongs at: right after the last member of the
// pak/ group, so the cache always sits next to the source it was built from.
std::size_t pakInsertionIndex(const std::vector<TarEntry>& members);

}  // namespace cp3::cli
