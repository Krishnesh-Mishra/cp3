// =============================================================================
// src/cli/src/detail/cache.hpp - incremental build layout + manifest DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/cache.cpp (STYLE.md §3/§8: headers declare,
// `.cpp` defines).
//
// Layout (docs/cli-tooling/01-compilation-pipeline.md, DECISIONS D61):
//   <projectRoot>/.cp3/build/            generated C++ intermediates
//     src/<module>.{cpp,hpp}             the entry TU's generated pair
//     src/lib/.../<module>.{cpp,hpp}     one pair per checked unit,
//                                       mirroring the source tree below it
//     cp3_program.hpp                    the per-module shared header
//     _cp3_pch.pch / .stamp / _prefix.hpp  the dev-profile precompiled header
//   <projectRoot>/.cp3/cache/            cached build outputs
//     src/<module>.o                     one object per TU, mirroring src/
//     _runtime/_cp3_runtime_<dir>_<tu>.o  stock runtime objects (D72)
//     build-manifest.txt                 change-detection state
//     <exe>.link-stamp                   link fingerprint
//   <projectRoot>/.cp3/out/              the linked executable
//   <projectRoot>/.cp3/out/              final artifacts
//     <entry><exe-suffix>                linked executable
//
// A build only regenerates and recompiles translation units whose inputs
// changed (compared by content hash); unchanged object files are reused and
// only the link step reruns. `--release` ignores the cache (clean whole-program
// build with LTO).
//
// Headers are hashed as a set (headerFingerprintOf: every file under the
// include roots); each manifest entry records the fingerprint it was built
// under, so a changed include tree retires every object on its next build,
// because no translation unit is tracked back to the headers it includes. The
// fingerprint also feeds the executable's link stamp (commands/build.cpp), so
// a rebuilt header or runtime object cannot leave an executable linked
// against the previous ones.
//
// Manifest v3 also records the effective compiler key (family, exact parsed
// version and driver path). A compiler update under `latest`, or an exact pin
// that resolves to a different version, retires every object and the link
// stamp instead of reusing code produced by another compiler.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cp3/host/options.hpp"

#include "detail/core/util.hpp"

namespace cp3::cli {

struct BuildLayout {
    fs::path projectRoot;
    fs::path buildDir;    // generated C++ intermediates (mirrors src/)
    fs::path cacheDir;    // cached objects + the manifest (mirrors src/)
    fs::path outDir;      // final artifacts (the linked executable)
    fs::path manifestPath;
    fs::path exePath;
    std::vector<fs::path> includeDirs;
    std::vector<fs::path> runtimeSources;  // one object per TU
};

struct CacheEntry {
    std::string profile;  // "incremental" | "release" | "debug"
    std::string source;   // logical source key (the .cp3 path or the real .cpp)
    std::string hash;     // hash of the generated/real .cpp content
    std::string header;   // include-tree fingerprint when the object was built
    std::string compiler; // effective compiler key (family, version, path)
    std::string cpp;      // generated .cpp path
    std::string obj;      // object path
};

// Locates the build root for an entry file: the nearest ancestor directory with
// a package.json, or the entry file's own directory.
fs::path findProjectRoot(const fs::path& entryFile);

// Assembles the build/out directories, manifest, executable and include roots.
BuildLayout makeBuildLayout(const fs::path& entryFile);

// Logical module name for generated-code include guards / output naming.
std::string moduleNameFor(const fs::path& entryFile, const fs::path& projectRoot);

const char* profileNameOf(BuildProfile profile) noexcept;

// Content fingerprint of every regular file under the include roots, sorted
// and hashed in a deterministic order. Empty includeDirs fingerprint to "".
std::string headerFingerprintOf(const std::vector<fs::path>& includeDirs);

// Fingerprint of the unconditional base include roots only (the always-
// available std families plus the runtime/common/language trees). The dev
// PCH precompiles only the frozen core/async headers, so validating it
// against these roots instead of the full per-program include tree lets
// Lane B produce the same fingerprint without knowing the program's std
// imports; std-family headers never enter the PCH anyway. The TU cache
// keeps the full headerFingerprintOf.
std::string pchFingerprintOf();

// Reads the manifest. A file written before the fingerprint format (or a
// corrupt one) yields no entries, which retires every object once.
std::vector<CacheEntry> loadManifest(const fs::path& manifestPath);

void saveManifest(const fs::path& manifestPath,
                  const std::vector<CacheEntry>& entries);

}  // namespace cp3::cli
