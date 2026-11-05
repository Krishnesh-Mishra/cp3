// =============================================================================
// src/cli/src/commands/pack.cpp - `cp3c build` for `"type": "package"` projects.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail::packProject.
//
// Output (CLI-README §5, D61): intermediates under .cp3/build/, the final
// artifact .cp3/out/<name>.cp3pak - a gzip-compressed archive carrying
//   cp3pak.json      container manifest (integer formatVersion, §6)
//   package.json     the author's manifest, verbatim
//   index.cp3        signatures/types only, generated from the entry AST
//   pak/<module>.*   the transpiled C++ (cpp + hpp)
//   pak/<platform>/   the optional cached binary of that platform (§5.4)
//   <build.files>    extra files listed in package.json, verbatim
//
// A package build produces source; the binary cache is opt-in through
// `build.cacheBinary` and only ever covers the platform the build machine
// runs on - cp3c never forces a cross-compilation (§5.4). `build.excludeSource`
// drops the C++ source, but only once the archive carries every cache the
// package claims; the headers and index.cp3 always stay.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/cache/cache.hpp"
#include "detail/cache/cache_binary.hpp"
#include "detail/cache/cache_build.hpp"
#include "detail/core/commands.hpp"
#include "detail/pkg/cp3pak.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/native_pack.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/archive/platform.hpp"
#include "detail/pkg/signatures.hpp"
#include "detail/archive/tar.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace detail {

namespace {

std::string genericName(std::string name) {
    for (char& c : name) {
        if (c == '\\') c = '/';
    }
    return name;
}

// Appends `<path>` (relative to the project) from the author's build.files.
bool appendBuildFile(const fs::path& projectRoot, const std::string& listedName,
                     std::vector<TarEntry>& entries, std::string& err) {
    const std::string name = genericName(listedName);
    const fs::path path = projectRoot / fs::path(name);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        err = "build.files lists '" + listedName +
              "', but that file does not exist";
        return false;
    }
    std::string data;
    if (!readTextFile(path, data)) {
        err = "cannot read build.files entry '" + listedName + "'";
        return false;
    }
    entries.push_back(TarEntry{name, std::move(data)});
    return true;
}

bool appendBuildFiles(const JsonValue& manifest, const fs::path& projectRoot,
                      std::vector<TarEntry>& entries, std::string& err) {
    const JsonValue* build = manifest.find("build");
    if (build == nullptr) return true;
    const JsonValue* files = build->arrayField("files");
    if (files == nullptr) return true;
    for (const JsonValue& item : files->array) {
        if (item.type != JsonValue::Type::String) {
            err = "build.files entries must be strings";
            return false;
        }
        if (!appendBuildFile(projectRoot, item.string, entries, err)) {
            return false;
        }
    }
    return true;
}

// Refuses the properties of a package build this build cannot honor.
// Returns kExitSuccess when packing may proceed.
int checkPackManifest(const std::string& entryText, const Program& program) {
    if (!hasDefaultExport(program)) {
        std::fprintf(stderr,
                     "cp3c: %s: 'type: package' projects need an "
                     "`export default { ... }` in the entry file (CLI-README "
                     "§5.2); no .cp3pak was produced\n",
                     entryText.c_str());
        return kExitCompileError;
    }
    return kExitSuccess;
}

// Intermediates mirror the application pipeline: .cp3/build/<module>.{cpp,hpp}
bool writeIntermediates(const PackInput& input, const fs::path& projectRoot,
                        std::string& err) {
    const fs::path moduleBase =
        projectRoot / ".cp3" / "build" / fs::path(*input.moduleName);
    std::error_code ec;
    fs::create_directories(moduleBase.parent_path(), ec);
    if (ec) {
        err = "cannot create " + moduleBase.parent_path().string() + " (" +
              ec.message() + ")";
        return false;
    }
    if (!writeTextFile(moduleBase.string() + ".cpp", input.module->source,
                       nullptr) ||
        !writeTextFile(moduleBase.string() + ".hpp", input.module->header,
                       nullptr)) {
        err = "cannot write build intermediates under " +
              (projectRoot / ".cp3" / "build").string();
        return false;
    }
    return true;
}

// The native binding ids the package build resolved. The module graph is the
// authority; the generated surface is a fallback for callers that only have
// the pack input.
std::vector<std::string> nativeIdsForPack(const PackInput& input,
                                          const std::string& surface) {
    std::vector<std::string> ids;
    if (input.nativeBindings != nullptr) {
        for (const NativeBinding& binding : *input.nativeBindings) {
            if (binding.id.empty()) continue;
            if (std::find(ids.begin(), ids.end(), binding.id) == ids.end()) {
                ids.push_back(binding.id);
            }
        }
    }
    for (const std::string& id : nativeBindingIdsInText(surface)) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    return ids;
}

// Archive members, in the order CLI-README §5.3 lists them.
bool collectEntries(const PackInput& input, const JsonValue& manifest,
                    const fs::path& projectRoot, std::vector<TarEntry>& entries,
                    const std::vector<std::string>& nativeIds,
                    std::string& err) {
    std::string packageJsonText;
    if (!readTextFile(projectRoot / "package.json", packageJsonText)) {
        err = "cannot read " + (projectRoot / "package.json").string();
        return false;
    }
    entries.push_back(TarEntry{"package.json", std::move(packageJsonText)});
    // The declaration surface keeps every native import the implementation
    // needs, even when the original import lived in a non-exported module.
    entries.push_back(TarEntry{
        "index.cp3",
        ensureNativeImports(stripToSignatures(
                                *input.source, *input.program,
                                input.opt ? input.opt->compile.typeSystem
                                        : TypeSystem::TypeScript),
                            nativeIds)});
    entries.push_back(
        TarEntry{"pak/" + *input.moduleName + ".cpp", input.module->source});
    entries.push_back(
        TarEntry{"pak/" + *input.moduleName + ".hpp", input.module->header});
    if (!collectNativeBindingMembers(projectRoot, nativeIds, entries, err)) {
        return false;
    }
    return appendBuildFiles(manifest, projectRoot, entries, err);
}

// The claim list of a plan, spelled the way the archive records it.
std::vector<std::string> claimTexts(const std::vector<PlatformId>& claims) {
    std::vector<std::string> out;
    for (const PlatformId& claim : claims) out.push_back(platformIdText(claim));
    return out;
}

// The claim list as one comma-separated phrase.
std::string claimList(const std::vector<PlatformId>& claims) {
    std::string list;
    for (const PlatformId& claim : claims) {
        if (!list.empty()) list += ", ";
        list += platformIdText(claim);
    }
    return list;
}

// True when the current machine's platform is one of the claimed platforms.
bool claimsCurrentPlatform(const std::vector<PlatformId>& claims) {
    const PlatformId& here = currentPlatform();
    return std::any_of(claims.begin(), claims.end(),
                       [&](const PlatformId& claim) {
                           return platformSatisfies(claim, here);
                       });
}

// The state of one package build's binary cache.
struct CacheProduction {
    const PackInput* input = nullptr;
    const CacheBinaryPlan* plan = nullptr;
    fs::path projectRoot;
    std::vector<std::string> nativeIds;
    std::vector<TarEntry>* entries = nullptr;
    std::vector<PlatformId>* cached = nullptr;
};

// The cached artifact of the current platform, when the package asks for one
// and this machine is one of the claimed platforms (CLI-README §5.4).
bool produceCache(const CacheProduction& work, std::string& err) {
    const PackInput& input = *work.input;
    const CacheBinaryPlan& plan = *work.plan;
    if (!plan.requested || plan.claims.empty()) return true;
    if (!claimsCurrentPlatform(plan.claims)) {
        std::fprintf(stderr,
                     "cp3c: warning: build.cacheBinary lists %s, but this "
                     "machine is %s; no cached binary was produced for it "
                     "(cp3c never cross-compiles)\n",
                     claimList(plan.claims).c_str(),
                     platformIdText(currentPlatform()).c_str());
        return true;
    }
    const PlatformId& here = currentPlatform();
    CacheBuildRequest request;
    request.module = *input.moduleName;
    request.source = input.module->source;
    request.header = input.module->header;
    request.workDir = work.projectRoot / ".cp3" / "build" / "cache" /
                      fs::path(platformIdText(here));
    request.verbose = input.opt->verbose;
    // The package implementation includes the native binding's headers; the
    // cached object must see the same include roots and defines as a consumer
    // compiling the source fallback.
    if (!nativeBuildFlagsFromProject(work.projectRoot, work.nativeIds,
                                     request.includeDirs, request.defines,
                                     err)) {
        return false;
    }
    PlatformCacheBuild artifact;
    if (!buildPlatformArtifact(request, artifact, err)) return false;

    PlatformCacheManifest cache;
    if (!planPlatformCache({CacheSourceRef{request.module, request.source,
                                          request.header}},
                           cache, err)) {
        return false;
    }
    work.entries->push_back(TarEntry{platformCacheManifestMember(here),
                                     platformCacheManifestToJson(cache)});
    work.entries->push_back(TarEntry{artifact.member, artifact.data});
    work.cached->push_back(here);
    std::fprintf(stderr,
                 "cp3c: cached binary for %s (pak/%s/%s.o)\n",
                 platformIdText(here).c_str(), platformIdText(here).c_str(),
                 request.module.c_str());
    return true;
}

// `build.excludeSource` drops the C++ source - but only once the archive
// carries every cache the package claims, so a package is never shipped
// without a fallback it does not actually have (CLI-README §5.4).
void applyExcludeSource(const CacheBinaryPlan& plan, bool excludeSource,
                        std::vector<TarEntry>& entries,
                        const std::vector<PlatformId>& cached) {
    if (!excludeSource) return;
    const std::vector<std::string> missing =
        unsatisfiedClaims(plan.claims, cached);
    if (!missing.empty()) {
        std::string list;
        for (const std::string& id : missing) {
            if (!list.empty()) list += ", ";
            list += id;
        }
        std::fprintf(stderr,
                     "cp3c: warning: build.excludeSource is set, but no cached "
                     "binary exists for %s; the C++ source is kept so "
                     "'cp3 recompile' can add it from another machine\n",
                     list.c_str());
        return;
    }
    std::vector<TarEntry> kept;
    kept.reserve(entries.size());
    for (TarEntry& entry : entries) {
        if (isPakSourceMember(entry.name)) continue;
        kept.push_back(std::move(entry));
    }
    entries = std::move(kept);
    std::fprintf(stderr,
                 "cp3c: excluded the C++ source (build.excludeSource); the "
                 "headers and index.cp3 stay\n");
}

// The package's manifest and its binary-cache plan. The plan is validated
// here, so a malformed platform id is reported before anything is written.
bool readPackManifest(const fs::path& projectRoot, JsonValue& manifest,
                       CacheBinaryPlan& plan, bool& excludeSource,
                       std::string& err) {
    if (!loadPackageJson(projectRoot, manifest, err)) return false;
    if (!parseCacheBinary(manifest, plan, err)) return false;
    excludeSource = excludeSourceRequested(manifest);
    if (excludeSource && plan.claims.empty()) {
        err = "build.excludeSource needs a non-empty build.cacheBinary list: "
              "without a cached binary the package would be uninstallable on "
              "every platform (CLI-README §5.4); no .cp3pak was produced";
        return false;
    }
    return true;
}

// Everything the packed archive records about itself.
struct PackOutput {
    std::string name;
    std::string version;
    std::string main;
    CacheBinaryPlan plan;
    std::vector<PlatformId> cached;
};

// Writes the .cp3pak: the container manifest plus the archive members.
bool writePackArchive(const fs::path& pakPath, const PackOutput& out,
                       const std::vector<TarEntry>& entries, std::string& err) {
    Cp3pakManifest pak;
    pak.formatVersion = kCp3pakFormatVersion;
    pak.name = out.name;
    pak.version = out.version;
    pak.main = out.main;
    pak.cachePlatforms = claimTexts(out.cached);
    pak.cacheClaims = claimTexts(out.plan.claims);
    return writeCp3pak(pakPath, pak, entries, err);
}

}  // namespace

int packProject(const PackInput& input) {
    const fs::path projectRoot = findProjectRoot(*input.entryPath);

    JsonValue manifest;
    CacheBinaryPlan plan;
    bool excludeSource = false;
    std::string err;
    if (!readPackManifest(projectRoot, manifest, plan, excludeSource, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    const int policy =
        checkPackManifest(input.entryPath->string(), *input.program);
    if (policy != kExitSuccess) return policy;
    if (!writeIntermediates(input, projectRoot, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    const std::string surface =
        stripToSignatures(*input.source, *input.program,
                          input.opt ? input.opt->compile.typeSystem
                                  : TypeSystem::TypeScript);
    const std::vector<std::string> nativeIds =
        nativeIdsForPack(input, surface);

    std::vector<TarEntry> entries;
    if (!collectEntries(input, manifest, projectRoot, entries, nativeIds,
                        err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    std::vector<PlatformId> cached;
    CacheProduction production;
    production.input = &input;
    production.plan = &plan;
    production.projectRoot = projectRoot;
    production.nativeIds = nativeIds;
    production.entries = &entries;
    production.cached = &cached;
    if (!produceCache(production, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    applyExcludeSource(plan, excludeSource, entries, cached);

    const fs::path outDir = projectRoot / ".cp3" / "out";
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "cp3c: cannot create %s (%s)\n",
                     outDir.string().c_str(), ec.message().c_str());
        return kExitCompileError;
    }
    PackOutput out;
    out.name = packageName(manifest, projectRoot);
    out.version = packageVersion(manifest);
    // The container always places the generated export surface at the archive
    // root as `index.cp3`; the container manifest names that member, not the
    // author's pre-pack `main` path (which may be `src/index.cp3`).
    out.main = "index.cp3";
    out.plan = plan;
    out.cached = cached;
    const fs::path pakPath = outDir / (out.name + ".cp3pak");
    if (!writePackArchive(pakPath, out, entries, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    if (input.opt->verbose) {
        std::fprintf(stderr, "cp3c: %zu archive members, formatVersion %d\n",
                     entries.size() + 1, kCp3pakFormatVersion);
    }
    std::fprintf(stderr, "cp3c: packed %s (%s %s)\n", pakPath.string().c_str(),
                 out.name.c_str(), out.version.c_str());
    return kExitSuccess;
}

}  // namespace detail

}  // namespace cp3::cli
