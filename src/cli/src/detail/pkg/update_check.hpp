// =============================================================================
// src/cli/src/detail/pkg/update_check.hpp - self-update check DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). DECLARATIONS live here; DEFINITIONS in
// src/pkg/remote/update_check.cpp.
//
// The `cp3` CLI checks this repository's GitHub releases at most once per
// 24 h and caches the answer in `<config dir>/update-check.json` (the same
// directory as `toolchain.conf`). The cache exists so ordinary commands do
// not hit the network; `cp3 upgrade` always checks live.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace cp3::cli {

struct UpdateCheckState {
    bool present = false;       // a readable cache existed
    bool stale = true;          // older than 24 h (or missing/corrupt)
    std::string latestTag;      // e.g. "v0.2.1"
    std::int64_t checkedAt = 0;  // unix seconds
};

extern const char* const kUpdateCheckFile;  // "update-check.json"
extern const char* const kSelfRepo;         // "Krishnesh-Mishra/cp3"

// Reads the cache. Missing or corrupt files yield present=false (and are
// never a reason to print or fail).
UpdateCheckState readUpdateCheck();

// Writes the cache (creating the config directory). Best effort; on normal
// user errors returns false with a message in `err`.
bool writeUpdateCheck(std::int64_t nowSec, const std::string& latestTag,
                      std::string& err);

//True when `latest` (a git tag such as "v0.2.1") is a newer version than
// `current` (a bare version such as "0.2.0"). A leading "v" is tolerated.
bool isNewerVersion(const std::string& latest, const std::string& current);

// Refreshes the cache from the network. `silent` (used by the per-command
// banner) suppresses all output and returns false on any failure.
bool refreshUpdateCheck(bool silent, UpdateCheckState& out, std::string& err);

}  // namespace cp3::cli
