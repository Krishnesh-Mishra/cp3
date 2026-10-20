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
// release asset can be re-fetched and hash-verified. A locally linked package
// records `source: "link"`, its kind (directory/artifact) and its resolved
// target instead of a copy under .cp3/packages/.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cp3::cli {

// Lock `source` values. An empty source means a normal local package install
// (the shape written before links and GitHub installs existed).
inline constexpr const char* kSourceLink = "link";
inline constexpr const char* kSourceGithub = "github";

// Link kinds recorded in the lock.
inline constexpr const char* kLinkDirectory = "directory";
inline constexpr const char* kLinkArtifact = "artifact";

struct LockEntry {
    std::string name;
    std::string version;
    std::string resolved;   // "file:<path>" | "github:<owner>/<repo>#<tag>/<asset>"
    std::string integrity;  // "sha256:<hex>" (artifact) or "dir-sha256:<hex>"
    std::string repo;       // GitHub source, when applicable
    std::string tag;        // GitHub release tag, when applicable
    std::string asset;      // GitHub release asset name, when applicable
    std::string source;     // "", "link" or "github"
    std::string kind;       // link kind: "directory" or "artifact"
    std::string target;     // link target (absolute directory or .cp3/link/<name>)
};

// Reads `<projectRoot>/.cp3/lock.json`. A missing lock is an empty one;
// a malformed lock is an error (never silently discarded).
bool readLockfile(const std::filesystem::path& projectRoot,
                  std::vector<LockEntry>& out, std::string& err);

// Writes the lock with entries sorted by package name (deterministic output).
bool writeLockfile(const std::filesystem::path& projectRoot,
                   const std::vector<LockEntry>& entries, std::string& err);

// True when `entry` is a `cp3 link` record.
bool isLinkEntry(const LockEntry& entry);

// First entry named `name`, or nullptr.
LockEntry* findLockEntry(std::vector<LockEntry>& entries,
                         const std::string& name);
const LockEntry* findLockEntry(const std::vector<LockEntry>& entries,
                               const std::string& name);

// Replaces the entry with the same name, or appends it.
void upsertLockEntry(std::vector<LockEntry>& entries, const LockEntry& entry);

}  // namespace cp3::cli
