// =============================================================================
// src/cli/src/detail/archive/zip.hpp - minimal ZIP archive DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// GitHub native bundles are ZIP files. Reading them here keeps the install path
// free of an external unzip dependency, exactly like the `.cp3pak` gzip/tar
// reader. Only regular files are returned; paths are validated by the reader.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "detail/archive/tar.hpp"

namespace cp3::cli {

// Reads the regular files of a ZIP archive. Stored (method 0) and DEFLATE
// (method 8) members are supported; encrypted members and symlinks are
// rejected. Absolute paths and any `..` path component are rejected, so the
// entries are safe to join under a project root.
bool zipRead(std::string_view input, std::vector<TarEntry>& entries,
             std::string& err);

}  // namespace cp3::cli
