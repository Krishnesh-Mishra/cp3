// =============================================================================
// cli/private/cp3pak.hpp - the `.cp3pak` container DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/cp3pak.cpp. Container per CLI-README §5/§6:
// a gzip-compressed tar archive whose first member is `cp3pak.json`, a small
// manifest carrying the integer `formatVersion` field (incremented only when
// the container layout itself changes, independent of the package version).
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "private/tar.hpp"

namespace cp3::cli {

// The only container version this build reads or writes.
inline constexpr int kCp3pakFormatVersion = 1;

struct Cp3pakManifest {
    int formatVersion = 0;
    std::string name;
    std::string version;
    std::string main;
};

// Writes `outPath` (manifest header + `entries` in order).
bool writeCp3pak(const std::filesystem::path& outPath,
                 const Cp3pakManifest& manifest,
                 const std::vector<TarEntry>& entries, std::string& err);

// Reads a `.cp3pak`, validating the gzip trailer, the tar checksums, the
// manifest `formatVersion` and the presence of `package.json`. Every member
// except `cp3pak.json` is returned in `entries`.
bool readCp3pak(const std::filesystem::path& path, Cp3pakManifest& manifest,
                std::vector<TarEntry>& entries, std::string& err);

}  // namespace cp3::cli
