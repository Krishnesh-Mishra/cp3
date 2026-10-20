// =============================================================================
// src/cli/src/detail/github_install.hpp - GitHub release install DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/github_install.cpp (STYLE.md §3/§8:
// headers declare, `.cpp` defines). Handles asset selection (single, --auto
// engines filter, interactive), hash verification against the existing lock,
// and the final install into .cp3/packages/<name>/.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "detail/lockfile.hpp"

namespace cp3::cli {

struct GithubInstallRequest {
    std::string url;
    std::filesystem::path projectRoot;
    bool autoSelect = false;
    bool force = false;
};

// Installs the latest release of a GitHub repository. On success `out` is the
// new lock entry and the caller persists `lock`. Returns a cp3 exit code.
int installGithubPackage(const GithubInstallRequest& request,
                         std::vector<LockEntry>& lock, LockEntry& out,
                         std::string& err);

}  // namespace cp3::cli
