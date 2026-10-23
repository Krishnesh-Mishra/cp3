// =============================================================================
// src/cli/src/detail/link.hpp - local package link DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/link.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines). A directory link records the source path and is
// used in place; an artifact link is extracted into .cp3/link/<name>/ (a link
// cache, not .cp3/packages/).
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

#include "detail/pkg/lockfile.hpp"

namespace cp3::cli {

// True when `name` can be a lock key and a .cp3/link/<name> directory.
bool usableLinkName(const std::string& name, std::string& err);

struct LinkRequest {
    std::filesystem::path source;
    std::filesystem::path projectRoot;
    std::string nameOverride;
    bool force = false;
    const LockEntry* existing = nullptr;  // existing link entry for this name
};

struct LinkResult {
    LockEntry entry;
    bool reusedCache = false;
};

// Validates `request.source` and records link metadata. Does not write the
// lock; the caller upserts `result.entry` and persists it.
bool linkPackage(const LinkRequest& request, LinkResult& result,
                 std::string& err);

// Removes .cp3/link/<name> only. Never touches a linked source directory or
// artifact.
void removeLinkCache(const std::filesystem::path& projectRoot,
                     const std::string& name);

}  // namespace cp3::cli
