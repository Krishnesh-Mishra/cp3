// =============================================================================
// src/cli/src/detail/cache_build.hpp - compiling a package's C++ into a cached
// artifact DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/cache_build.cpp.
//
// The artifact is a real object file built by the detected system toolchain
// from the package's own transpiled C++ - the same source a consumer would
// otherwise compile. It is a plain (non-LTO) object so it links into any
// consumer build, and the package's entry `main` is renamed while it is
// compiled, so the artifact is a library object and never collides with the
// consumer program's own entry point.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cp3/host/options.hpp"

#include "detail/cache/cache_binary.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

// One module a platform's cache is built from.
struct CacheSourceRef {
    std::string module;  // the module name ("src/index")
    std::string source;  // the transpiled C++ of that module
    std::string header;  // the transpiled C++ header of that module
};

// What to compile, and where.
struct CacheBuildRequest {
    std::string module;  // the module name ("src/index")
    std::string source;  // the transpiled C++ of that module
    std::string header;  // the transpiled C++ header of that module
    fs::path workDir;    // where the translation unit is written and compiled
    // The native bindings the implementation includes and uses.
    std::vector<std::string> includeDirs;
    std::vector<std::string> defines;
    bool verbose = false;
};

// The result of compiling one module for the current platform.
struct PlatformCacheBuild {
    std::string module;    // the module the artifact was built from
    std::string member;    // the archive member the artifact belongs in
    std::string data;      // the artifact bytes
    std::string sourceHash;
    std::string headerHash;
};

// The manifest a cache for the current platform would carry, computed without
// compiling anything: the platform, the ABI, the compiler identity and the
// hashes of the sources the artifacts are built from. It is what makes a
// recompile idempotent - an archive whose cache already matches this plan is
// left exactly as it is.
bool planPlatformCache(const std::vector<CacheSourceRef>& sources,
                       PlatformCacheManifest& out, std::string& err);

// Compiles `request.source` for the current platform. Returns false with a
// message in `err` when no toolchain is available or the compile fails.
bool buildPlatformArtifact(const CacheBuildRequest& request,
                           PlatformCacheBuild& out, std::string& err);

}  // namespace cp3::cli
