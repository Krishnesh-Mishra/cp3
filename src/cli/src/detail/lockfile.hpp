// =============================================================================
// src/cli/src/detail/lockfile.hpp - `.cp3/lock.json` DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/lockfile.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines).
//
// The lock records, per installed package (CLI-README §4): the exact version,
// where it came from (`resolved`), and the SHA-256 of the artifact that was
// installed. A GitHub-sourced package also records repo/tag/asset so the same
// release asset can be re-fetched and hash-verified.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cp3::cli {

struct LockEntry {
    std::string name;
    std::string version;
    std::string resolved;   // "file:<path>" | "github:<owner>/<repo>#<tag>/<asset>"
    std::string integrity;  // "sha256:<hex>" (artifact) or "dir-sha256:<hex>"
    std::string repo;       // GitHub source, when applicable
    std::string tag;        // GitHub release tag, when applicable
    std::string asset;      // GitHub release asset name, when applicable
};

// Reads `<projectRoot>/.cp3/lock.json`. A missing lock is an empty one;
// a malformed lock is an error (never silently discarded).
bool readLockfile(const std::filesystem::path& projectRoot,
                  std::vector<LockEntry>& out, std::string& err);

// Writes the lock with entries sorted by package name (deterministic output).
bool writeLockfile(const std::filesystem::path& projectRoot,
                   const std::vector<LockEntry>& entries, std::string& err);

}  // namespace cp3::cli
