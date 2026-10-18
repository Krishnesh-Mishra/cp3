// =============================================================================
// cli/private/tar.hpp - minimal POSIX ustar archive DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/tar.cpp. The `.cp3pak` payload is a ustar
// archive (gzip-compressed outside); only regular files are written, and reads
// accept what the format's mainstream writers produce (pax headers and GNU
// long names are tolerated and skipped/resolved).
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cp3::cli {

struct TarEntry {
    std::string name;  // relative, '/'-separated
    std::string data;
};

// Writes a deterministic ustar archive (fixed mode 0644, zero uid/gid/mtime).
bool tarWrite(const std::vector<TarEntry>& entries, std::string& out,
              std::string& err);

// Reads regular files from an archive. Rejects absolute paths and any `..`
// path component (path-traversal guard). Returns false on a malformed archive.
bool tarRead(std::string_view input, std::vector<TarEntry>& entries,
             std::string& err);

}  // namespace cp3::cli
