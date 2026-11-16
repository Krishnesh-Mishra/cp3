// =============================================================================
// src/cli/src/detail/cp3pak.hpp - the `.cp3pak` container DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/cp3pak.cpp. Container per CLI-README §5/§6:
// a gzip-compressed tar archive whose first member is `cp3pak.json`, a small
// manifest carrying the integer `formatVersion` field (incremented only when
// the container layout itself changes, independent of the package version).
//
// formatVersion 2 is the source-package layout: the archive carries
// package.json verbatim and the package's authored sources; consumers compile
// them. formatVersion 1 archives (signature stubs + transpiled C++ + optional
// cached binaries) are rejected outright - there is no legacy read path.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/archive/tar.hpp"

namespace cp3::cli {

// The only container version this build reads or writes. 1 is not accepted.
inline constexpr int kCp3pakFormatVersion = 2;

struct Cp3pakManifest {
    int formatVersion = 0;
    std::string name;
    std::string version;
    std::string main;   // the declared `main`, exactly as authored
    // The platforms the archive carries a cached artifact for, and the
    // platform ids the package claims (CLI-README §5.4 of the prebuilt-era
    // format). Both are optional and unused by source packages: `cp3c build`
    // never writes them. They remain readable for hand-made archives and for
    // `cp3 recompile`, which can still refresh a cached object for a v2
    // archive that carries `pak/*.cpp` members.
    std::vector<std::string> cachePlatforms;
    std::vector<std::string> cacheClaims;
};

// Writes `outPath` (manifest header + `entries` in order).
bool writeCp3pak(const std::filesystem::path& outPath,
                 const Cp3pakManifest& manifest,
                 const std::vector<TarEntry>& entries, std::string& err);

// Reads a `.cp3pak`, validating the gzip trailer, the tar checksums, the
// manifest `formatVersion` and the presence of `package.json`. Every member
// except `cp3pak.json` is returned in `entries`. When `rawManifest` is
// non-null it receives the exact bytes of the `cp3pak.json` member, so a
// caller can install the manifest itself.
bool readCp3pak(const std::filesystem::path& path, Cp3pakManifest& manifest,
                std::vector<TarEntry>& entries, std::string& err,
                std::string* rawManifest = nullptr);

}  // namespace cp3::cli
