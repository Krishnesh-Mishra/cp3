// =============================================================================
// src/cli/src/pkg/cache_build.cpp - compiling a package's C++ into a cached
// artifact DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/cache_build.hpp.
//
// The compile is the ordinary one the toolchain driver performs for a
// generated translation unit (detail/toolchain.hpp), with two deliberate
// differences:
//
//   * the profile is the plain optimized one (-O2, no LTO), so the artifact
//     links into a consumer build that does not link with LTO;
//   * the entry's `main` is renamed while the unit is compiled, so the
//     artifact is a library object. A package's transpiled source carries the
//     entry point codegen emits for it; a consumer program has its own.
//
// The artifact is therefore exactly the code a consumer would have compiled,
// minus the entry point it must not own.
// =============================================================================
#include "detail/cache_build.hpp"

#include <cstdio>
#include <system_error>
#include <vector>

#include "detail/cache.hpp"
#include "detail/checksum.hpp"
#include "detail/toolchain.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

// The symbol the package's entry `main` is renamed to.
std::string renamedMainSymbol(const std::string& module) {
    std::string out = "cp3_pak_main_";
    for (char c : module) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9');
        out.push_back(plain ? c : '_');
    }
    return out;
}

// The ABI the artifact was built for: the C++ standard and the runtime the
// compiler links against.
std::string cacheAbi(const Toolchain& tc) {
    std::string abi = "cxx20";
    if (tc.msvc) {
        abi += "-msvc";
    } else {
#if defined(__APPLE__)
        abi += "-libc++";
#else
        abi += "-libstdc++";
#endif
    }
    return abi;
}

}  // namespace

bool buildPlatformArtifact(const CacheBuildRequest& request,
                           PlatformCacheBuild& out, std::string& err) {
    const Toolchain& tc = detectToolchain();
    if (!tc.found()) {
        err = "no C++ toolchain found on this system (looked for cl, clang++, "
              "g++ on PATH)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(request.workDir, ec);
    if (ec) {
        err = "cannot create " + request.workDir.string() + " (" +
              ec.message() + ")";
        return false;
    }
    const fs::path cpp = request.workDir / (request.module + ".cpp");
    const fs::path hpp = request.workDir / (request.module + ".hpp");
    const fs::path obj = request.workDir / (request.module + ".o");
    if (!writeTextFile(cpp, request.source, nullptr) ||
        !writeTextFile(hpp, request.header, nullptr)) {
        err = "cannot write the cache translation unit under " +
              request.workDir.string();
        return false;
    }

    std::vector<fs::path> includes = compilerIncludeRoots();
    includes.push_back(request.workDir);
    CompileStep step{includes, cpp, obj, {}};
    // The package's entry point is renamed so the artifact is a library
    // object; the consumer program owns the real `main`.
    step.extraFlags.push_back((tc.msvc ? "/Dmain=" : "-Dmain=") +
                               renamedMainSymbol(request.module));
    const std::vector<std::string> args =
        compileArgs(tc, BuildProfile::Incremental, step);
    if (request.verbose) {
        std::fprintf(stderr, "cp3c: %s\n", describeCommand(args).c_str());
    }
    const int rc = runProcess(args);
    if (rc != 0) {
        err = "C++ compilation of the cached artifact failed (exit " +
              std::to_string(rc) + ")";
        return false;
    }
    if (!readTextFile(obj, out.data)) {
        err = "cannot read the compiled artifact " + obj.string();
        return false;
    }
    out.module = request.module;
    out.member = platformCacheArtifactMember(currentPlatform(), request.module);
    out.sourceHash = sha256Hex(request.source);
    out.headerHash = sha256Hex(request.header);
    return true;
}

bool planPlatformCache(const std::vector<CacheSourceRef>& sources,
                       PlatformCacheManifest& out, std::string& err) {
    const Toolchain& tc = detectToolchain();
    if (!tc.found()) {
        err = "no C++ toolchain found on this system (looked for cl, clang++, "
              "g++ on PATH)";
        return false;
    }
    out = PlatformCacheManifest{};
    out.formatVersion = 1;
    out.platform = platformIdText(currentPlatform());
    out.abi = cacheAbi(tc);
    out.kind = "object";
    out.compiler = describeToolchain(tc);
    out.compilerId = tc.id;
    out.compilerVersion = tc.version;
    out.compilerPath = tc.program;
    out.profile = profileNameOf(BuildProfile::Incremental);
    for (const CacheSourceRef& source : sources) {
        PlatformCacheArtifact artifact;
        artifact.module = source.module;
        artifact.member =
            platformCacheArtifactMember(currentPlatform(), source.module);
        artifact.sourceHash = sha256Hex(source.source);
        artifact.headerHash = sha256Hex(source.header);
        out.artifacts.push_back(artifact);
    }
    return true;
}

}  // namespace cp3::cli
