// =============================================================================
// src/cli/src/commands/pkg/upgrade.cpp - `cp3 upgrade` (self-update).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdUpgrade.
//
// `cp3 upgrade` reinstalls the compiler from the latest public GitHub release
// of this repository (Krishnesh-Mishra/cp3), for the current platform:
//
//   Windows x64  - run the release MSI asset with `msiexec /qb`
//   Linux x64    - extract the release tarball and run its `install.sh`
//
// The asset is verified against the release's SHA256SUMS-<tag>.txt before any
// installer is launched. `--dry-run` prints the plan without doing anything.
// `--yes` skips the confirmation prompt (required when not on a TTY).
//
// `cp3 upgrade` always checks the network; it does not consult the 24 h
// update-check cache (that cache only feeds the per-command reminder).
// =============================================================================
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/core/commands.hpp"
#include "detail/core/semver.hpp"
#include "detail/core/util.hpp"
#include "detail/archive/platform.hpp"
#include "detail/pkg/github.hpp"
#include "detail/pkg/update_check.hpp"
#include "cp3/meta/checksum.hpp"
#include "cp3/meta/version.hpp"

namespace cp3::cli {

namespace {

namespace fs = std::filesystem;

std::string assetPlatformSuffix() {
    const PlatformId& id = currentPlatform();
    const std::string family = id.family;
    const std::string arch = id.arch;
    if (family == "windows" && arch == "x64") return "-windows-x64.msi";
    if (family == "linux" && arch == "x64") return "-linux-x64.tar.gz";
    return {};  // unsupported (arm64 is not released in this line)
}

std::string assetKindSuffix() {
    return currentPlatform().family == "windows" ? ".msi" : ".tar.gz";
}

std::string sumsFileName(const std::string& tag) {
    return "SHA256SUMS-" + tag + ".txt";
}

std::string assetIndexSha256(const fs::path& sumsPath, const std::string& assetName,
                             std::string& err) {
    std::ifstream in(sumsPath, std::ios::binary);
    if (!in) {
        err = "cannot read " + sumsPath.string();
        return {};
    }
    std::string line;
    while (std::getline(in, line)) {
        // Format: "<64-hex>  <asset-name>" (two spaces, or one when listed absolute).
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string hash = line.substr(0, space);
        std::string name = line.substr(space);
        while (!name.empty() && name[0] == ' ') name.erase(name.begin());
        if (name == assetName) return hash;
    }
    err = "no SHA256 line for " + assetName + " in " + sumsPath.string();
    return {};
}

std::string readWholeFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool isInteractive() { return isTty(stdin); }

void printUpgradeInfo(FILE* out, const std::string& current,
                      const std::string& latest, bool available,
                      const std::string& assetName) {
    std::fprintf(out, "current version: %s\n", current.c_str());
    std::fprintf(out, "latest version:  %s\n", latest.c_str());
    std::fprintf(out, "update available: %s\n", available ? "yes" : "no");
    if (available) {
        std::fprintf(out, "asset:            %s\n", assetName.c_str());
        std::fprintf(out, "run:              cp3 upgrade\n");
    }
}

bool verifyAsset(const fs::path& assetPath, const std::string& expectedSha,
                 std::string& err) {
    const std::string bytes = readWholeFile(assetPath);
    if (bytes.empty()) {
        err = "downloaded asset is empty: " + assetPath.string();
        return false;
    }
    const std::string actual = sha256Hex(bytes);
    if (actual != expectedSha) {
        err = "SHA256 mismatch for " + assetPath.filename().string() +
              ": expected " + expectedSha + ", got " + actual +
              "; refusing to install";
        return false;
    }
    return true;
}

bool runWindowsInstaller(const fs::path& asset, std::string& err) {
    std::vector<std::string> args = {"msiexec", "/i", asset.string(), "/qb"};
    const int rc = runProcess(args);
    if (rc != 0) {
        err = "msiexec exited with " + std::to_string(rc);
        return false;
    }
    return true;
}

bool runLinuxInstaller(const fs::path& asset, const fs::path& extractRoot,
                       std::string& err) {
    // Extract like the documented flow: tar -xzf <asset> -C <extractRoot>.
    std::vector<std::string> untar = {"tar", "-xzf", asset.string(),
                                      "-C", extractRoot.string()};
    if (runProcess(untar) != 0) {
        err = "could not extract " + asset.string();
        return false;
    }
    // The tarball carries a top-level cp3/ tree and install.sh next to it.
    const fs::path script = extractRoot / "install.sh";
    std::error_code ec;
    if (!fs::is_regular_file(script, ec)) {
        err = "no install.sh in the release tarball";
        return false;
    }
    // The installer replaces the current per-user install with --force.
    std::vector<std::string> install = {script.string(), "--force"};
    if (runProcess(install) != 0) {
        err = "install.sh exited with a non-zero status";
        return false;
    }
    return true;
}

}  // namespace

int cmdUpgrade(const CliOptions& opt) {
    GithubRepo repo;
    std::string err;
    if (!parseGithubRepoUrl(std::string("https://github.com/") + kSelfRepo,
                            repo, err)) {
        std::fprintf(stderr, "cp3c: internal error: bad repository name\n");
        return kExitCompileError;
    }
    GithubRelease release;
    if (!fetchLatestGithubRelease(repo, release, err)) {
        std::fprintf(stderr,
                     "cp3c: could not check for updates: %s "
                     "(unauthenticated GitHub API is rate-limited; set "
                     "CP3_GITHUB_TOKEN for a higher limit)\n",
                     err.c_str());
        return kExitCompileError;
    }
    const std::string current = kVersion;
    const std::string latest = release.tag.empty() ? std::string("unknown") : release.tag;
    const bool available =
        !latest.empty() && isNewerVersion(latest, current);

    const std::string suffix = assetPlatformSuffix();
    if (suffix.empty()) {
        std::fprintf(stderr,
                     "cp3c: no release asset exists for this platform "
                     "(arm64 builds are not published; see UNTESTED.txt)\n");
        return kExitCompileError;
    }
    const std::string assetName =
        "cp3-" + (latest.empty() ? "" : latest.substr(latest[0] == 'v' ? 1 : 0)) +
        suffix;
    const GithubAsset* asset = nullptr;
    const GithubAsset* sums = nullptr;
    for (const GithubAsset& candidate : release.assets) {
        if (candidate.name == assetName) asset = &candidate;
        if (!latest.empty() && candidate.name == sumsFileName(latest)) sums = &candidate;
    }

    if (!available) {
        if (opt.json) {
            std::printf("{\"command\":\"upgrade\",\"current\":\"%s\",\"latest\":\"%s\",\"available\":false}\n",
                        current.c_str(), latest.c_str());
        } else {
            std::printf("cp3 upgrade: you are on the latest version (%s)\n",
                        current.c_str());
        }
        return kExitSuccess;
    }

    if (asset == nullptr) {
        std::fprintf(stderr,
                     "cp3c: latest release %s has no asset for this platform (%s)\n",
                     latest.c_str(), assetPlatformSuffix().c_str());
        return kExitCompileError;
    }
    const std::string& assetUrl = asset->url;
    if (opt.json) {
        std::printf(
            "{\"command\":\"upgrade\",\"current\":\"%s\",\"latest\":\"%s\","
            "\"available\":true,\"asset\":\"%s\",\"url\":\"%s\"}\n",
            current.c_str(), latest.c_str(), assetName.c_str(), assetUrl.c_str());
    } else {
        std::printf("current version: %s\n", current.c_str());
        std::printf("latest version:  %s\n", latest.c_str());
        std::printf("update:          %s available\n", assetName.c_str());
        if (opt.dryRun) {
            std::printf("dry-run: would download %s and reinstall\n",
                        assetUrl.c_str());
            return kExitSuccess;
        }
    }
    if (opt.dryRun) {
        std::printf("dry-run: would download %s and reinstall\n",
                    assetUrl.c_str());
        if (!opt.json) std::printf("re-run with --yes to install\n");
        return kExitSuccess;
    }
    if (!opt.yes) {
        if (!isInteractive()) {
            std::fprintf(stderr,
                         "cp3c: re-run with --yes to install the upgrade "
                         "(upgrade is not confirmed on non-TTY input)\n");
            return kExitUsageError;
        }
        std::printf("Upgrade cp3 %s -> %s? [y/N] ", current.c_str(),
                    latest.c_str());
        std::string answer;
        if (!std::getline(std::cin, answer)) return kExitUsageError;
        if (answer != "y" && answer != "Y" && answer != "yes" &&
            answer != "YES") {
            std::printf("cp3 upgrade: cancelled\n");
            return kExitSuccess;
        }
    }

    // Download to a temp directory (never overwrites anything outside it).
    std::error_code ec;
    const fs::path tmp =
        fs::temp_directory_path(ec) /
        ("cp3-upgrade-" + std::to_string(std::time(nullptr)));
    fs::create_directories(tmp, ec);
    if (ec) {
        std::fprintf(stderr, "cp3c: cannot create %s\n", tmp.string().c_str());
        return kExitCompileError;
    }
    const fs::path assetPath = tmp / assetName;
    const fs::path sumsPath = tmp / sumsFileName(latest);

    std::fprintf(stderr, "cp3 upgrade: downloading %s ...\n", assetName.c_str());
    if (!downloadGithubAsset(assetUrl, assetPath, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        fs::remove_all(tmp, ec);
        return kExitCompileError;
    }
    if (sums != nullptr) {
        if (!downloadGithubAsset(sums->url, sumsPath, err)) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            fs::remove_all(tmp, ec);
            return kExitCompileError;
        }
        const std::string expected = assetIndexSha256(sumsPath, assetName, err);
        if (expected.empty()) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            fs::remove_all(tmp, ec);
            return kExitCompileError;
        }
        if (!verifyAsset(assetPath, expected, err)) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            fs::remove_all(tmp, ec);
            return kExitCompileError;
        }
        std::fprintf(stderr, "cp3 upgrade: SHA256 verified\n");
    } else {
        std::fprintf(stderr,
                     "cp3 upgrade: warning: release has no checksums file; "
                     "installing UNVERIFIED asset\n");
    }

    const bool windows = currentPlatform().family == "windows";
    bool installed = false;
    if (windows) {
        installed = runWindowsInstaller(assetPath, err);
    } else {
        const fs::path extractRoot = tmp / "extract";
        fs::create_directories(extractRoot, ec);
        installed = runLinuxInstaller(assetPath, extractRoot, err);
    }
    if (!installed) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        fs::remove_all(tmp, ec);
        return kExitCompileError;
    }
    #if defined(_WIN32)
    std::fprintf(stderr,
                 "cp3 upgrade: upgraded to %s; reopen your terminal to use "
                 "the new version\n",
                 latest.c_str());
    #else
    std::fprintf(stderr,
                 "cp3 upgrade: upgraded to %s; re-run `cp3 doctor` to verify\n",
                 latest.c_str());
    #endif
    fs::remove_all(tmp, ec);
    return kExitSuccess;
}

}  // namespace cp3::cli
