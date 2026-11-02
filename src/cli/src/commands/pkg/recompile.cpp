// =============================================================================
// src/cli/src/commands/recompile.cpp - `cp3c recompile <file.cp3pak>`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdRecompile.
//
// `cp3 recompile` adds (or refreshes) the current platform's cached binary in
// an already-built `.cp3pak` (CLI-README §5.4). It reads the C++ source the
// archive already carries, compiles it for the machine it runs on and writes
// the result back into the same archive - no source re-fetch, no rebuild of
// the package, and every other member (and every other platform's cache)
// preserved untouched.
//
// The command is idempotent: an archive whose cache for this platform is
// already current is left byte-identical.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/cache/cache_binary.hpp"
#include "detail/cache/cache_build.hpp"
#include "detail/core/commands.hpp"
#include "detail/pkg/cp3pak.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/native_pack.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/archive/platform.hpp"
#include "detail/archive/tar.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

// The package's manifest inside the archive.
bool archiveManifest(const std::vector<TarEntry>& members, JsonValue& out,
                     std::string& err) {
    for (const TarEntry& member : members) {
        if (member.name != "package.json") continue;
        if (!JsonValue::parse(member.data, out, err)) {
            err = "archive's package.json is invalid: " + err;
            return false;
        }
        return true;
    }
    err = "archive has no package.json";
    return false;
}

// The claims the archive makes: the container manifest records them, and an
// archive written before the cache existed falls back to the author's own
// `build.cacheBinary`.
std::vector<PlatformId> archiveClaims(const Cp3pakManifest& pak,
                                      const JsonValue& manifest,
                                      std::string& err) {
    if (!pak.cacheClaims.empty()) {
        return normalizePlatformIds(pak.cacheClaims, err);
    }
    CacheBinaryPlan plan;
    if (!parseCacheBinary(manifest, plan, err)) return {};
    return plan.claims;
}

// The header of a module, if the archive carries one.
const TarEntry* findHeader(const std::vector<TarEntry>& members,
                           const std::string& module) {
    const std::string name = "pak/" + module + ".hpp";
    for (const TarEntry& member : members) {
        if (member.name == name) return &member;
    }
    return nullptr;
}

// The cache of the current platform, if the archive already carries one and it
// is still current: the same source, the same header and the same compiler.
bool cacheIsCurrent(const PlatformCacheManifest& existing,
                    const PlatformCacheManifest& planned) {
    if (existing.platform != planned.platform) return false;
    if (existing.abi != planned.abi) return false;
    if (existing.kind != planned.kind) return false;
    if (existing.compilerId != planned.compilerId) return false;
    if (existing.compilerVersion != planned.compilerVersion) return false;
    if (existing.compilerPath != planned.compilerPath) return false;
    if (existing.artifacts.size() != planned.artifacts.size()) return false;
    for (std::size_t i = 0; i < existing.artifacts.size(); ++i) {
        if (existing.artifacts[i].module != planned.artifacts[i].module) {
            return false;
        }
        if (existing.artifacts[i].sourceHash != planned.artifacts[i].sourceHash) {
            return false;
        }
        if (existing.artifacts[i].headerHash != planned.artifacts[i].headerHash) {
            return false;
        }
    }
    return true;
}

// A scratch directory for the translation units the recompile compiles.
fs::path recompileWorkDir(const fs::path& archivePath) {
    const std::string key = std::to_string(
        fnv1a64(fs::absolute(archivePath).lexically_normal().string()));
    fs::path base = fs::temp_directory_path();
    return base / ("cp3-recompile-" + key + "-" +
                   platformIdText(currentPlatform()));
}

// The cached platforms of the archive, after the current platform's cache was
// (re)written.
std::vector<std::string> cachedPlatformTexts(
    const std::vector<TarEntry>& members) {
    std::vector<std::string> out;
    for (const PlatformId& id : archiveCachedPlatforms(members)) {
        out.push_back(platformIdText(id));
    }
    return out;
}

// The modules the archive carries, with the source and header of each.
std::vector<CacheSourceRef> archiveSources(const std::vector<TarEntry>& members) {
    std::vector<CacheSourceRef> out;
    for (const TarEntry* source : archiveSourceMembers(members)) {
        CacheSourceRef ref;
        ref.module = pakSourceModule(source->name);
        ref.source = source->data;
        const TarEntry* header = findHeader(members, ref.module);
        if (header != nullptr) ref.header = header->data;
        out.push_back(ref);
    }
    return out;
}

// Compiles every module of the archive for the current platform and returns
// the cache members: the manifest first, then the artifacts.
bool compileCacheBlock(const std::vector<CacheSourceRef>& sources,
                       const std::vector<TarEntry>& members,
                       const fs::path& archivePath, bool verbose,
                       std::vector<TarEntry>& block, std::string& err) {
    const fs::path workDir = recompileWorkDir(archivePath);
    PlatformCacheManifest planned;
    if (!planPlatformCache(sources, planned, err)) return false;
    // The archive's embedded native assets provide the include roots and
    // defines the package implementation was written against. Materialize
    // them in the scratch directory so this machine compiles the same source.
    std::vector<std::string> includeDirs;
    std::vector<std::string> defines;
    if (!nativeBuildFlagsFromArchive(members, workDir, includeDirs, defines,
                                     err)) {
        removeTree(workDir);
        return false;
    }
    block.push_back(TarEntry{platformCacheManifestMember(currentPlatform()),
                              platformCacheManifestToJson(planned)});
    for (const CacheSourceRef& source : sources) {
        CacheBuildRequest request;
        request.module = source.module;
        request.source = source.source;
        request.header = source.header;
        request.workDir = workDir;
        request.includeDirs = includeDirs;
        request.defines = defines;
        request.verbose = verbose;
        PlatformCacheBuild artifact;
        if (!buildPlatformArtifact(request, artifact, err)) {
            removeTree(workDir);
            return false;
        }
        block.push_back(TarEntry{artifact.member, artifact.data});
    }
    removeTree(workDir);
    return true;
}

// Replaces the current platform's cache in the archive; every other member
// (and every other platform's cache) is preserved.
void replacePlatformCache(std::vector<TarEntry>& members,
                           const std::vector<TarEntry>& block) {
    const std::size_t index =
        removePlatformCacheMembers(members, currentPlatform());
    const std::size_t at =
        index < members.size() ? index : pakInsertionIndex(members);
    members.insert(members.begin() + static_cast<std::ptrdiff_t>(at),
                   block.begin(), block.end());
}

// The source is dropped only once every claimed platform is cached. Returns
// true when the archive changed.
bool applyExcludeSource(const std::vector<PlatformId>& claims,
                         const JsonValue& manifest,
                         std::vector<TarEntry>& members) {
    if (!excludeSourceRequested(manifest)) return false;
    const std::vector<PlatformId> cached = archiveCachedPlatforms(members);
    const std::vector<std::string> missing = unsatisfiedClaims(claims, cached);
    if (!missing.empty()) {
        std::string list;
        for (const std::string& id : missing) {
            if (!list.empty()) list += ", ";
            list += id;
        }
        std::fprintf(stderr,
                     "cp3c: warning: build.excludeSource is set, but no "
                     "cached binary exists for %s; the C++ source is kept\n",
                     list.c_str());
        return false;
    }
    std::vector<TarEntry> kept;
    kept.reserve(members.size());
    for (TarEntry& member : members) {
        if (isPakSourceMember(member.name)) continue;
        kept.push_back(std::move(member));
    }
    const bool changed = kept.size() != members.size();
    members = std::move(kept);
    std::fprintf(stderr,
                 "cp3c: excluded the C++ source (build.excludeSource); the "
                 "headers and index.cp3 stay\n");
    return changed;
}

// The archive to recompile, read and validated.
bool openArchive(const fs::path& path, Cp3pakManifest& pak,
                 std::vector<TarEntry>& members, std::string& err) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        err = "'" + path.string() + "' does not exist";
        return false;
    }
    return readCp3pak(path, pak, members, err);
}

// The usage errors of the command.
int checkRecompileUsage(const CliOptions& opt) {
    if (opt.positionals.empty()) {
        std::fprintf(stderr, "cp3c: recompile needs a .cp3pak file\n");
        std::fputs("usage: cp3c recompile <file.cp3pak>\n", stderr);
        return kExitUsageError;
    }
    if (opt.positionals.size() > 1) {
        std::fprintf(stderr,
                     "cp3c: 'recompile' takes exactly one .cp3pak file\n");
        std::fputs("usage: cp3c recompile <file.cp3pak>\n", stderr);
        return kExitUsageError;
    }
    return kExitSuccess;
}

// Writes the recompiled archive back, recording the platforms it carries.
bool writeRecompiled(const fs::path& path, Cp3pakManifest& pak,
                     const std::vector<TarEntry>& members,
                     const std::vector<PlatformId>& claims, std::string& err) {
    pak.cachePlatforms = cachedPlatformTexts(members);
    if (pak.cacheClaims.empty()) {
        for (const PlatformId& claim : claims) {
            pak.cacheClaims.push_back(platformIdText(claim));
        }
    }
    return writeCp3pak(path, pak, members, err);
}

}  // namespace

int cmdRecompile(const CliOptions& opt) {
    const int usage = checkRecompileUsage(opt);
    if (usage != kExitSuccess) return usage;

    const fs::path archivePath(opt.positionals[0]);
    Cp3pakManifest pak;
    std::vector<TarEntry> members;
    std::string err;
    if (!openArchive(archivePath, pak, members, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    if (!archiveHasSource(members)) {
        std::fprintf(stderr,
                     "cp3c: '%s' carries no C++ source to compile (no "
                     "pak/*.cpp inside); the author excluded it with "
                     "build.excludeSource, so no cache can be added for %s\n",
                     archivePath.string().c_str(),
                     platformIdText(currentPlatform()).c_str());
        return kExitCompileError;
    }

    JsonValue manifest;
    if (!archiveManifest(members, manifest, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    std::vector<PlatformId> claims = archiveClaims(pak, manifest, err);
    if (!err.empty()) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    // The cache this machine would produce, computed before anything is
    // compiled: an archive that already carries it is left untouched.
    const std::vector<CacheSourceRef> sources = archiveSources(members);
    PlatformCacheManifest planned;
    if (!planPlatformCache(sources, planned, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    PlatformCacheManifest existing;
    const bool hadCache =
        archiveFindPlatformCache(members, currentPlatform(), existing);
    const bool current = hadCache && cacheIsCurrent(existing, planned);

    bool changed = false;
    if (!current) {
        std::vector<TarEntry> block;
        if (!compileCacheBlock(sources, members, archivePath, opt.verbose,
                               block, err)) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            return kExitCompileError;
        }
        replacePlatformCache(members, block);
        changed = true;
    }

    if (applyExcludeSource(claims, manifest, members)) changed = true;

    if (changed) {
        if (!writeRecompiled(archivePath, pak, members, claims, err)) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            return kExitCompileError;
        }
        std::fprintf(stderr, "cp3c: recompiled %s (%s)\n",
                     archivePath.string().c_str(),
                     platformIdText(currentPlatform()).c_str());
        return kExitSuccess;
    }

    std::fprintf(stderr,
                 "cp3c: %s already carries a current %s cache; nothing to do\n",
                 archivePath.string().c_str(),
                 platformIdText(currentPlatform()).c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
