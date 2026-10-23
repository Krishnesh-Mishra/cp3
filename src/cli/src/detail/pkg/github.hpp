// =============================================================================
// src/cli/src/detail/github.hpp - GitHub release discovery/download DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/github.cpp (STYLE.md §3/§8: headers
// declare, `.cpp` defines). Beta one supports public GitHub repository release
// assets only; the system HTTP client (curl.exe on Windows, curl on POSIX) does
// the transfer. CP3_GITHUB_API_URL overrides the API base for tests, and
// CP3_GITHUB_TOKEN adds an Authorization header to API requests only.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cp3::cli {

struct GithubRepo {
    std::string owner;
    std::string repo;

    std::string fullName() const { return owner + "/" + repo; }
};

struct GithubAsset {
    std::string name;
    std::string url;  // browser_download_url (or the API asset URL)
};

struct GithubRelease {
    std::string tag;
    std::vector<GithubAsset> assets;
};

// Parses `https://github.com/<owner>/<repo>` (optional `.git`, trailing slash).
// Rejects other hosts and non-repository URLs.
bool parseGithubRepoUrl(const std::string& text, GithubRepo& out,
                        std::string& err);

// Fetches the latest non-prerelease release (GitHub's `/releases/latest`).
bool fetchLatestGithubRelease(const GithubRepo& repo, GithubRelease& out,
                              std::string& err);

// Downloads `url` to `dest` with the system curl. `err` is a clear message
// when curl is missing or the transfer fails.
bool downloadGithubAsset(const std::string& url, const std::filesystem::path& dest,
                         std::string& err);

// Exposed for the report/tests: the effective API base.
std::string githubApiBase();

}  // namespace cp3::cli
