// =============================================================================
// src/cli/src/commands/pkg/update.cpp - `cp3 update` (project dependencies).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdUpdate.
//
// Beta one has no registry: a project's `dependencies` are GitHub releases,
// `file:` paths or `cp3 link` directories. `cp3 update` therefore has nothing
// to "search"; it means:
//
//   re-resolve each *GitHub* dependency to the latest release of the same
//   repository, and reinstall it if the release is newer than the locked tag
//
// `file:` and linked dependencies are left alone (they are local).
// `.cp3/lock.json` is updated in place. `--dry-run` prints what would
// change. `--json` prints a machine-readable summary.
// `cp3 update [name]` restricts the operation to one dependency.
// =============================================================================
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/core/commands.hpp"
#include "detail/core/util.hpp"
#include "detail/pkg/github_install.hpp"
#include "detail/pkg/github.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/lockfile.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/pkg/update_check.hpp"

namespace cp3::cli {

namespace {

namespace fs = std::filesystem;

struct UpdateReport {
    std::string name;
    std::string from;
    std::string to;
    bool changed = false;
    std::string status;  // "updated" | "up-to-date" | "skipped" | "error"
};

void printUpdateLine(const UpdateReport& report) {
    if (report.status == "updated") {
        std::fprintf(stderr, "cp3c: %s %s -> %s\n", report.name.c_str(),
                     report.from.c_str(), report.to.c_str());
    } else if (report.status == "up-to-date") {
        std::fprintf(stderr, "cp3c: %s (%s) is up to date\n",
                     report.name.c_str(), report.from.c_str());
    } else if (report.status == "skipped") {
        std::fprintf(stderr, "cp3c: %s is not a GitHub dependency; skipping\n",
                     report.name.c_str());
    } else {
        std::fprintf(stderr, "cp3c: could not update %s\n",
                     report.name.c_str());
    }
}

}  // namespace

int cmdUpdate(const CliOptions& opt) {
    std::error_code ec;
    fs::path projectRoot;
    if (!findPackageRoot(fs::current_path(ec), projectRoot)) {
        std::fprintf(stderr,
                     "cp3c: no package.json found here or in any parent; run "
                     "'cp3c init' first\n");
        return kExitCompileError;
    }
    std::vector<LockEntry> lock;
    std::string err;
    if (!readLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    const std::string only =
        opt.positionals.empty() ? std::string{} : opt.positionals.front();

    std::vector<UpdateReport> reports;
    int installFailures = 0;
    bool anyGithubDep = false;

    for (const LockEntry& entry : lock) {
        if (!only.empty() && entry.name != only) continue;
        if (entry.source != kSourceGithub) {
            if (!only.empty()) {
                std::fprintf(stderr,
                             "cp3c: '%s' is not a GitHub dependency; skipping\n",
                             entry.name.c_str());
            }
            continue;
        }
        anyGithubDep = true;
        UpdateReport report;
        report.name = entry.name;
        report.from = entry.tag;

        GithubRepo repo;
        if (!parseGithubRepoUrl(std::string("https://github.com/") + entry.repo,
                                repo, err)) {
            std::fprintf(stderr, "cp3c: bad locked repo '%s' for %s\n",
                         entry.repo.c_str(), entry.name.c_str());
            report.status = "error";
            reports.push_back(report);
            ++installFailures;
            continue;
        }
        GithubRelease release;
        if (!fetchLatestGithubRelease(repo, release, err)) {
            std::fprintf(stderr,
                         "cp3c: warning: could not check %s: %s; leaving it "
                         "alone\n",
                         entry.name.c_str(), err.c_str());
            if (err.find("rate limit") != std::string::npos) {
                std::fprintf(stderr,
                             "cp3c: GitHub's unauthenticated API is limited "
                             "to 60 requests/hour; run `gh auth login` to "
                             "authenticate (cp3 reuses the GitHub CLI's "
                             "credential), or set CP3_GITHUB_TOKEN\n");
            }
            report.status = "skipped";
            report.to = report.from;
            reports.push_back(report);
            continue;
        }
        report.to = release.tag;
        if (!isNewerVersion(release.tag, entry.tag)) {
            report.status = "up-to-date";
            report.changed = false;
            reports.push_back(report);
            continue;
        }
        if (opt.dryRun) {
            report.status = "updated";
            report.changed = true;
            reports.push_back(report);
            continue;
        }

        GithubInstallRequest request;
        request.url = std::string("https://github.com/") + entry.repo;
        request.projectRoot = projectRoot;
        request.autoSelect = true;  // non-interactive: pick the compatible pak
        LockEntry reinstalled;
        const int rc = installGithubPackage(request, lock, reinstalled, err);
        if (rc == kExitSuccess) {
            report.status = "updated";
            report.changed = true;
            report.to = reinstalled.tag;
        } else {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            report.status = "error";
            report.changed = false;
            ++installFailures;
        }
        reports.push_back(report);
    }

    if (!anyGithubDep) {
        if (!only.empty()) {
            std::fprintf(stderr,
                         "cp3c: no locked dependency matches '%s'; nothing to "
                         "update\n",
                         only.c_str());
        } else {
            std::fprintf(stderr,
                         "cp3c: no GitHub dependencies locked in %s; nothing "
                         "to update (file: and linked dependencies are "
                         "local)\n",
                         (projectRoot / ".cp3" / "lock.json").string().c_str());
        }
        return kExitSuccess;
    }

    if (!opt.dryRun) {
        if (!writeLockfile(projectRoot, lock, err)) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            return kExitCompileError;
        }
    }

    if (opt.json) {
        std::printf("{\n  \"command\": \"update\",\n  \"dryRun\": %s,\n",
                    opt.dryRun ? "true" : "false");
        std::printf("  \"packages\": [\n");
        for (std::size_t i = 0; i < reports.size(); ++i) {
            const UpdateReport& report = reports[i];
            std::printf("    {\"name\": %s, \"from\": %s, \"to\": %s, "
                        "\"status\": \"%s\"}%s\n",
                        jsonQuote(report.name).c_str(),
                        jsonQuote(report.from).c_str(),
                        jsonQuote(report.to).c_str(), report.status.c_str(),
                        i + 1 == reports.size() ? "" : ",");
        }
        std::printf("  ]\n}\n");
    } else {
        for (const UpdateReport& report : reports) printUpdateLine(report);
        int changed = 0;
        for (const UpdateReport& report : reports) {
            if (report.status == "updated") ++changed;
        }
        if (opt.dryRun) {
            std::fprintf(stderr,
                         "cp3c: dry-run: %d package(s) would be updated\n",
                         changed);
        } else if (changed > 0) {
            std::fprintf(stderr, "cp3c: %d package(s) updated\n", changed);
        } else if (installFailures == 0) {
            std::fprintf(stderr, "cp3c: everything is up to date\n");
        } else {
            std::fprintf(stderr,
                         "cp3c: %d package(s) could not be updated\n",
                         installFailures);
        }
    }
    return installFailures == 0 ? kExitSuccess : kExitCompileError;
}

}  // namespace cp3::cli
