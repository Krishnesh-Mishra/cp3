// =============================================================================
// src/cli/src/pkg/github_install.cpp - GitHub release install DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/github_install.hpp.
//
// Beta one scope: public GitHub repositories, latest non-prerelease release,
// `.cp3pak` assets only. One asset installs directly; multiple assets use the
// interactive selector, or --auto's engines.cp3 filter when it leaves exactly
// one candidate. The lock pins repo/tag/asset, the download URL and SHA-256.
// =============================================================================
#include "detail/pkg/github_install.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"
#include "cp3/meta/version.hpp"

#include "detail/cache/checksum.hpp"
#include "detail/pkg/cp3pak.hpp"
#include "detail/pkg/github.hpp"
#include "detail/pkg/install.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/link.hpp"
#include "detail/core/semver.hpp"
#include "detail/archive/tar.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

struct Candidate {
    GithubAsset asset;
    fs::path file;
    bool declaresEngines = false;
    bool compatible = false;
    bool inspected = false;
    std::string range;
};

std::string compilerVersion() {
    std::string version = kVersion;
    const std::string stage = kVersionStage;
    if (!stage.empty() && version.find('-') == std::string::npos) {
        version += "-" + stage;
    }
    return version;
}

bool isPakAsset(const std::string& name) {
    return name.size() > 7 && endsWith(name, ".cp3pak");
}

std::string safeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                        c == '.';
        out.push_back(ok ? c : '_');
    }
    return out.empty() ? "asset.cp3pak" : out;
}

fs::path makeTempRoot(const fs::path& projectRoot) {
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return projectRoot / ".cp3" / "tmp" /
           ("github-" + std::to_string(stamp));
}

bool readEngines(const fs::path& archive, std::string& range, bool& declares,
                 std::string& err) {
    Cp3pakManifest pak;
    std::vector<TarEntry> members;
    if (!readCp3pak(archive, pak, members, err)) {
        err = "release asset '" + archive.filename().string() +
              "' is not a valid .cp3pak: " + err;
        return false;
    }
    JsonValue manifest;
    const std::string* packageJson = nullptr;
    for (const TarEntry& member : members) {
        if (member.name == "package.json") packageJson = &member.data;
    }
    if (packageJson == nullptr) {
        err = "release asset '" + archive.filename().string() +
              "' has no package.json";
        return false;
    }
    if (!JsonValue::parse(*packageJson, manifest, err)) {
        err = "release asset '" + archive.filename().string() +
              "' has an invalid package.json";
        return false;
    }
    const JsonValue* engines = manifest.find("engines");
    const std::string* value =
        engines == nullptr ? nullptr : engines->stringField("cp3");
    declares = value != nullptr && !value->empty();
    range = declares ? *value : std::string();
    return true;
}

bool inspectCandidate(Candidate& candidate, std::string& err) {
    if (!readEngines(candidate.file, candidate.range,
                     candidate.declaresEngines, err)) {
        return false;
    }
    candidate.inspected = true;
    if (!candidate.declaresEngines) return true;
    const SemVer compiler = parseSemVer(compilerVersion());
    candidate.compatible = semverSatisfies(compiler, candidate.range);
    return true;
}

bool downloadCandidate(const GithubAsset& asset, const fs::path& tempRoot,
                       Candidate& out, std::string& err) {
    out.asset = asset;
    out.file = tempRoot / safeFileName(asset.name);
    return downloadGithubAsset(asset.url, out.file, err);
}

bool prepareCandidates(const std::vector<GithubAsset>& assets,
                       const fs::path& tempRoot,
                       std::vector<Candidate>& out, std::string& err) {
    out.clear();
    for (const GithubAsset& asset : assets) {
        Candidate candidate;
        if (!downloadCandidate(asset, tempRoot, candidate, err)) return false;
        if (!inspectCandidate(candidate, err)) return false;
        out.push_back(std::move(candidate));
    }
    return true;
}

void printCandidateLine(const Candidate& candidate, std::size_t index) {
    std::fprintf(stdout, "  %zu. %s", index + 1, candidate.asset.name.c_str());
    if (!candidate.inspected) {
        std::fputc('\n', stdout);
        return;
    }
    if (!candidate.declaresEngines) {
        std::fputs(" (no engines.cp3)\n", stdout);
        return;
    }
    std::fprintf(stdout, " (engines.cp3 \"%s\": %s)\n",
                 candidate.range.c_str(),
                 candidate.compatible ? "compatible" : "incompatible");
}

bool chooseInteractively(const std::vector<Candidate>& candidates,
                         std::size_t& selected, std::string& err) {
    if (!isTty(stdin)) {
        err = "multiple .cp3pak assets: run on a terminal to pick one, or "
              "pass --auto";
        return false;
    }
    std::fprintf(stdout, "cp3c: %zu .cp3pak assets in this release:\n",
                 candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        printCandidateLine(candidates[i], i);
    }
    std::fprintf(stdout, "Select asset [1-%zu]: ", candidates.size());
    std::fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) {
        err = "no asset selection was read";
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(line.c_str(), &end, 10);
    while (end != nullptr && (*end == ' ' || *end == '\t' || *end == '\r')) {
        ++end;
    }
    if (end == line.c_str() || (end != nullptr && *end != '\0') || value < 1 ||
        static_cast<std::size_t>(value) > candidates.size()) {
        err = "invalid asset selection '" + line + "'";
        return false;
    }
    selected = static_cast<std::size_t>(value - 1);
    return true;
}

// --auto keeps candidates whose engines.cp3 range matches plus candidates
// that declare no engines (still ambiguous); incompatible ones are skipped.
bool autoSelect(const std::vector<Candidate>& candidates,
                std::size_t& selected) {
    std::vector<std::size_t> remaining;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& candidate = candidates[i];
        if (!candidate.declaresEngines || candidate.compatible) {
            remaining.push_back(i);
        }
    }
    if (remaining.size() != 1) return false;
    selected = remaining.front();
    return true;
}

const LockEntry* pinnedEntry(const std::vector<LockEntry>& lock,
                             const GithubRepo& repo, const GithubRelease& release,
                             const GithubAsset& asset) {
    for (const LockEntry& entry : lock) {
        if (entry.source == kSourceGithub && entry.repo == repo.fullName() &&
            entry.tag == release.tag && entry.asset == asset.name) {
            return &entry;
        }
    }
    return nullptr;
}

struct Selection {
    std::vector<Candidate> candidates;
    std::size_t selected = 0;
};

bool collectAssets(const GithubRelease& release, std::vector<GithubAsset>& out,
                   const GithubRepo& repo, std::string& err) {
    out.clear();
    for (const GithubAsset& asset : release.assets) {
        if (isPakAsset(asset.name)) out.push_back(asset);
    }
    if (!out.empty()) return true;
    err = "GitHub release '" + release.tag + "' of " + repo.fullName() +
          " has no .cp3pak assets";
    return false;
}

int chooseReleaseAsset(const std::vector<GithubAsset>& assets,
                       const fs::path& tempRoot, bool wantAuto,
                       Selection& selection, std::string& err) {
    if (assets.size() == 1) {
        Candidate candidate;
        if (!downloadCandidate(assets.front(), tempRoot, candidate, err)) {
            return kExitCompileError;
        }
        selection.candidates.push_back(std::move(candidate));
        return kExitSuccess;
    }
    if (wantAuto) {
        if (!prepareCandidates(assets, tempRoot, selection.candidates, err)) {
            return kExitCompileError;
        }
        if (autoSelect(selection.candidates, selection.selected) ||
            chooseInteractively(selection.candidates, selection.selected, err)) {
            return kExitSuccess;
        }
        return kExitUsageError;
    }
    if (!isTty(stdin)) {
        err = "release has " + std::to_string(assets.size()) +
              " .cp3pak assets; run on a terminal to pick one, or pass --auto";
        return kExitUsageError;
    }
    for (const GithubAsset& asset : assets) {
        Candidate candidate;
        candidate.asset = asset;
        selection.candidates.push_back(candidate);
    }
    if (!chooseInteractively(selection.candidates, selection.selected, err)) {
        return kExitUsageError;
    }
    Candidate& chosen = selection.candidates[selection.selected];
    if (!downloadCandidate(chosen.asset, tempRoot, chosen, err)) {
        return kExitCompileError;
    }
    return kExitSuccess;
}

struct InstallContext {
    const GithubRepo& repo;
    const GithubRelease& release;
    const GithubInstallRequest& request;
    std::vector<LockEntry>& lock;
    LockEntry& out;
};

int installSelectedAsset(const Candidate& chosen, const InstallContext& ctx,
                         std::string& err) {
    std::string bytes;
    if (!readTextFile(chosen.file, bytes)) {
        err = "cannot read downloaded asset " + chosen.file.string();
        return kExitCompileError;
    }
    const std::string integrity = "sha256:" + sha256Hex(bytes);

    std::string name;
    std::string version;
    if (!inspectPackageSource(chosen.file, name, version, err)) {
        return kExitCompileError;
    }
    const LockEntry* existing = findLockEntry(ctx.lock, name);
    if (existing != nullptr && isLinkEntry(*existing) && !ctx.request.force) {
        err = "package '" + name +
              "' is linked; pass --force to replace the link with the GitHub "
              "release";
        return kExitUsageError;
    }
    const LockEntry* pinned =
        pinnedEntry(ctx.lock, ctx.repo, ctx.release, chosen.asset);
    if (pinned != nullptr && !pinned->integrity.empty() &&
        pinned->integrity != integrity && !ctx.request.force) {
        err = "release asset '" + chosen.asset.name + "' for tag '" +
              ctx.release.tag + "' changed since .cp3/lock.json pinned it "
              "(expected " + pinned->integrity + ", got " + integrity +
              "); pass --force to accept the new bytes";
        return kExitCompileError;
    }
    if (!installArchivePackage(chosen.file, ctx.request.projectRoot, ctx.out,
                               err)) {
        return kExitCompileError;
    }
    if (existing != nullptr && isLinkEntry(*existing)) {
        removeLinkCache(ctx.request.projectRoot, name);
    }
    ctx.out.source = kSourceGithub;
    ctx.out.repo = ctx.repo.fullName();
    ctx.out.tag = ctx.release.tag;
    ctx.out.asset = chosen.asset.name;
    ctx.out.resolved = chosen.asset.url;
    ctx.out.integrity = integrity;
    upsertLockEntry(ctx.lock, ctx.out);
    return kExitSuccess;
}

}  // namespace

int installGithubPackage(const GithubInstallRequest& request,
                         std::vector<LockEntry>& lock, LockEntry& out,
                         std::string& err) {
    GithubRepo repo;
    if (!parseGithubRepoUrl(request.url, repo, err)) return kExitUsageError;
    GithubRelease release;
    if (!fetchLatestGithubRelease(repo, release, err)) return kExitCompileError;
    std::vector<GithubAsset> assets;
    if (!collectAssets(release, assets, repo, err)) return kExitCompileError;

    const fs::path tempRoot = makeTempRoot(request.projectRoot);
    std::error_code ec;
    fs::create_directories(tempRoot, ec);
    if (ec) {
        err = "cannot create " + tempRoot.string() + " (" + ec.message() + ")";
        return kExitCompileError;
    }
    Selection selection;
    const int chosenRc =
        chooseReleaseAsset(assets, tempRoot, request.autoSelect, selection, err);
    if (chosenRc != kExitSuccess) {
        removeTree(tempRoot);
        return chosenRc;
    }
    const InstallContext ctx{repo, release, request, lock, out};
    const int installRc = installSelectedAsset(
        selection.candidates[selection.selected], ctx, err);
    removeTree(tempRoot);
    return installRc;
}

}  // namespace cp3::cli
