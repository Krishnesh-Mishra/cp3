// =============================================================================
// src/cli/src/commands/install.cpp - `cp3c install [<path-or-url>]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdInstall.
//
// Beta one sources: a local `.cp3pak` / unpacked package directory, or a public
// GitHub repository URL (latest non-prerelease release, `.cp3pak` assets,
// interactive selector or --auto). A local install is copied into
// `.cp3/packages/<name>/`; a GitHub install is downloaded with the system curl
// and recorded with repo/tag/asset and a SHA-256. Installing a local `.cp3pak`
// FILE (not a directory) asks first whether the source file may be deleted
// once the install succeeds; the default answer is no, and a non-interactive
// invocation never prompts nor deletes. Linked packages are never
// overwritten without --force, and a bare package name is not guessed as a
// path: named registry installs are explicitly out of beta one.
// =============================================================================
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/cli.hpp"

#include "detail/core/commands.hpp"
#include "detail/pkg/github_install.hpp"
#include "detail/pkg/install.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/link.hpp"
#include "detail/pkg/lockfile.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

bool isUrl(const std::string& text) {
    return text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
}

bool isGithubUrl(const std::string& text) {
    return text.rfind("https://github.com/", 0) == 0;
}

// "file:./x.cp3pak" -> "./x.cp3pak"; other specs are returned unchanged.
std::string stripFilePrefix(const std::string& spec) {
    if (spec.rfind("file:", 0) == 0) return spec.substr(5);
    return spec;
}

bool looksLikePath(const std::string& spec) {
    // An explicit file: spec is always local, including file:deps/kit.
    if (spec.rfind("file:", 0) == 0) return spec.size() > 5;
    const std::string path = stripFilePrefix(spec);
    if (path.empty()) return false;
    if (path == "." || path == "..") return true;
    if (path.rfind("./", 0) == 0 || path.rfind("../", 0) == 0 ||
        path.rfind(".\\", 0) == 0 || path.rfind("..\\", 0) == 0 ||
        path.rfind("/", 0) == 0 || path.rfind("\\\\", 0) == 0) {
        return true;
    }
    // A relative path with a separator is path-shaped, not a bare package
    // name. Bare names still stay registry-only, even if a same-named
    // directory happens to exist.
    if (path.find('/') != std::string::npos ||
        path.find('\\') != std::string::npos) {
        return true;
    }
    if (path.size() >= 2 && path[1] == ':') return true;  // C:\ or C:/
    return endsWith(path, ".cp3pak");
}

void namedRegistryError(const std::string& spec) {
    std::fprintf(stderr,
                 "cp3c: named registry installs are not available in beta "
                 "one ('%s'); use a local package directory or .cp3pak, a "
                 "GitHub release URL, or 'cp3 link <path>'\n",
                 spec.c_str());
}

bool linkConflict(const std::vector<LockEntry>& lock, const std::string& name,
                  bool force, std::string& err) {
    const LockEntry* existing = findLockEntry(lock, name);
    if (existing == nullptr || !isLinkEntry(*existing) || force) return true;
    err = "package '" + name +
          "' is linked; pass --force to replace the link with a package "
          "install";
    return false;
}

void printEntryJson(std::string& out, const LockEntry& entry) {
    out += "{";
    out += "\"name\":" + jsonQuote(entry.name);
    out += ",\"version\":" + jsonQuote(entry.version);
    out += ",\"source\":" + jsonQuote(entry.source);
    out += ",\"resolved\":" + jsonQuote(entry.resolved);
    if (!entry.integrity.empty()) {
        out += ",\"integrity\":" + jsonQuote(entry.integrity);
    }
    if (!entry.repo.empty()) out += ",\"repo\":" + jsonQuote(entry.repo);
    if (!entry.tag.empty()) out += ",\"tag\":" + jsonQuote(entry.tag);
    if (!entry.asset.empty()) out += ",\"asset\":" + jsonQuote(entry.asset);
    out += "}";
}

void printInstallJson(const std::vector<LockEntry>& entries) {
    std::string out = "{\"command\":\"install\",\"packages\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) out += ",";
        printEntryJson(out, entries[i]);
    }
    out += "]}\n";
    std::fputs(out.c_str(), stdout);
}

void reportInstalled(const LockEntry& entry, bool json) {
    if (json) return;
    if (entry.source == kSourceGithub) {
        std::fprintf(stderr, "cp3c: installed %s %s (github %s @ %s)\n",
                     entry.name.c_str(), entry.version.c_str(),
                     entry.repo.c_str(), entry.tag.c_str());
        return;
    }
    std::fprintf(stderr, "cp3c: installed %s %s -> .cp3/packages/%s\n",
                 entry.name.c_str(), entry.version.c_str(),
                 entry.name.c_str());
}

int installGithubOne(const std::string& url, const CliOptions& opt,
                     const fs::path& projectRoot, std::vector<LockEntry>& lock,
                     LockEntry& entry) {
    GithubInstallRequest request;
    request.url = url;
    request.projectRoot = projectRoot;
    request.autoSelect = opt.autoSelect;
    request.force = opt.force;
    std::string err;
    const int rc = installGithubPackage(request, lock, entry, err);
    if (rc != kExitSuccess) std::fprintf(stderr, "cp3c: %s\n", err.c_str());
    return rc;
}

int installLocalOne(const fs::path& source, const fs::path& projectRoot,
                    const CliOptions& opt, std::vector<LockEntry>& lock,
                    LockEntry& entry) {
    std::string name;
    std::string version;
    std::string err;
    if (!inspectPackageSource(source, name, version, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (!linkConflict(lock, name, opt.force, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }
    const LockEntry* existing = findLockEntry(lock, name);
    const bool replacesLink = existing != nullptr && isLinkEntry(*existing);
    if (!installLocalPackage(source, projectRoot, entry, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (replacesLink) removeLinkCache(projectRoot, name);
    upsertLockEntry(lock, entry);
    return kExitSuccess;
}

// -----------------------------------------------------------------------------
// Dependency specs (bare `cp3c install`)
// -----------------------------------------------------------------------------

enum class DepKind { Local, Github, Registry, Malformed };

DepKind classifyDependency(const std::string& spec, fs::path& resolved) {
    if (spec.empty()) return DepKind::Malformed;
    if (isGithubUrl(spec)) return DepKind::Github;
    if (isUrl(spec)) return DepKind::Malformed;
    const std::string path = stripFilePrefix(spec);
    if (looksLikePath(spec)) {
        resolved = path;
        return DepKind::Local;
    }
    return DepKind::Registry;
}

// The state shared by every dependency in one `cp3c install` pass.
struct DependencyContext {
    const CliOptions& opt;
    const fs::path& projectRoot;
    std::vector<LockEntry>& lock;
};

bool installDependency(const std::string& name, const std::string& spec,
                       DependencyContext& ctx, LockEntry& installed) {
    fs::path resolved;
    const DepKind kind = classifyDependency(spec, resolved);
    if (kind == DepKind::Github) {
        const int rc = installGithubOne(spec, ctx.opt, ctx.projectRoot,
                                        ctx.lock, installed);
        return rc == kExitSuccess;
    }
    if (kind != DepKind::Local) {
        std::fprintf(stderr,
                     "cp3c: cannot install '%s' from '%s': named registry "
                     "installs are not available in beta one; use a file: "
                     "path, a GitHub release URL, or 'cp3 link <path>'\n",
                     name.c_str(), spec.c_str());
        return false;
    }
    const fs::path source = resolved.is_absolute() ? resolved
                                                   : ctx.projectRoot / resolved;
    const int rc = installLocalOne(source, ctx.projectRoot, ctx.opt, ctx.lock,
                                   installed);
    if (rc != kExitSuccess) {
        return false;
    }
    if (installed.name != name) {
        std::fprintf(stderr,
                     "cp3c: warning: dependency '%s' installs package '%s'\n",
                     name.c_str(), installed.name.c_str());
    }
    return true;
}

int installDependencies(const CliOptions& opt, const fs::path& projectRoot,
                        std::vector<LockEntry>& lock) {
    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(projectRoot, manifest, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    const JsonValue* deps = manifest.find("dependencies");
    if (deps == nullptr || !deps->isObject() || deps->keys.empty()) {
        std::fprintf(stderr, "cp3c: nothing to install (no dependencies in %s)\n",
                     (projectRoot / "package.json").string().c_str());
        return kExitSuccess;
    }

    std::vector<LockEntry> installed;
    DependencyContext context{opt, projectRoot, lock};
    int linked = 0;
    int failures = 0;
    for (std::size_t i = 0; i < deps->keys.size(); ++i) {
        const std::string& depName = deps->keys[i];
        const JsonValue& depValue = deps->values[i];
        const LockEntry* existing = findLockEntry(lock, depName);
        if (existing != nullptr && isLinkEntry(*existing)) {
            if (!opt.json) {
                std::fprintf(stderr,
                             "cp3c: '%s' is linked; leaving the link in place\n",
                             depName.c_str());
            }
            ++linked;
            continue;
        }
        if (depValue.type != JsonValue::Type::String) {
            std::fprintf(stderr,
                         "cp3c: cannot install '%s': dependency values must be "
                         "a file: path or a GitHub URL in this build\n",
                         depName.c_str());
            ++failures;
            continue;
        }
        LockEntry entry;
        if (installDependency(depName, depValue.string, context, entry)) {
            if (!opt.json) reportInstalled(entry, false);
            installed.push_back(entry);
        } else {
            ++failures;
        }
    }
    if (!writeLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (opt.json) {
        printInstallJson(installed);
    } else if (installed.size() == 1) {
        std::fprintf(stderr, "cp3c: 1 package installed\n");
    } else {
        std::fprintf(stderr, "cp3c: %d packages installed\n",
                     static_cast<int>(installed.size()));
    }
    if (linked > 0 && !opt.json) {
        std::fprintf(stderr, "cp3c: %d linked package(s) left unchanged\n",
                     linked);
    }
    if (failures > 0) {
        std::fprintf(stderr, "cp3c: %d of %zu dependencies could not be installed\n",
                     failures, deps->keys.size());
        return kExitCompileError;
    }
    return kExitSuccess;
}

}  // namespace

int cmdInstall(const CliOptions& opt) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    fs::path projectRoot;
    if (!findPackageRoot(cwd, projectRoot)) {
        std::fprintf(stderr,
                     "cp3c: no package.json found in %s or any parent; run "
                     "'cp3c init' first\n",
                     cwd.string().c_str());
        return kExitCompileError;
    }
    std::string err;
    std::vector<LockEntry> lock;
    if (!readLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }

    if (opt.positionals.empty()) return installDependencies(opt, projectRoot, lock);

    const std::string spec = opt.positionals[0];
    if (isGithubUrl(spec)) {
        LockEntry entry;
        const int rc = installGithubOne(spec, opt, projectRoot, lock, entry);
        if (rc != kExitSuccess) return rc;
        if (!writeLockfile(projectRoot, lock, err)) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            return kExitCompileError;
        }
        if (opt.json) {
            printInstallJson({entry});
        } else {
            reportInstalled(entry, false);
        }
        return kExitSuccess;
    }
    if (isUrl(spec)) {
        std::fprintf(stderr,
                     "cp3c: only https://github.com/<owner>/<repo> URLs are "
                     "supported for remote installs in beta one\n");
        return kExitUsageError;
    }

    // A bare name is always a registry name in beta one, even when a
    // same-named directory exists: local paths must be explicit ("./pkg",
    // "../pkg", an absolute path, or a .cp3pak file). This keeps a future
    // registry from silently resolving to whatever happens to be on disk.
    if (!looksLikePath(spec)) {
        namedRegistryError(spec);
        return kExitUsageError;
    }
    const std::string path = stripFilePrefix(spec);
    const fs::path source = fs::path(path).is_absolute() ? fs::path(path)
                                                         : cwd / fs::path(path);
    if (!packageSourceExists(source)) {
        std::fprintf(stderr,
                     "cp3c: '%s' does not exist (expected a .cp3pak file or a "
                     "package directory)\n",
                     source.string().c_str());
        return kExitCompileError;
    }
    // A local .cp3pak FILE (never a directory, a URL or a bare name) is a
    // disposable artifact the author may want gone once installed. Ask about
    // deleting it before the install starts; delete only after the whole
    // install path succeeded. A directory source is never offered (deleting
    // would destroy the author's project), and a non-interactive invocation
    // never prompts nor deletes.
    std::string deleteCandidate;
    {
        std::error_code absEc;
        std::error_code fileEc;
        const fs::path absolute = fs::absolute(source, absEc);
        const std::string shown =
            absEc ? source.string() : absolute.string();
        if (!opt.json && !opt.autoSelect && !opt.yes && isTty(stdin) &&
            fs::is_regular_file(source, fileEc)) {
            std::string name = source.filename().generic_string();
            for (char& c : name) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            if (endsWith(name, ".cp3pak")) deleteCandidate = shown;
        }
    }
    bool deleteSource = false;
    if (!deleteCandidate.empty()) {
        std::fprintf(stderr,
                     "delete source package file '%s' after successful "
                     "install? [y/N]: ",
                     deleteCandidate.c_str());
        std::string answer;
        // EOF (a closed stdin) reads as a default "no": the file stays.
        if (std::getline(std::cin, answer)) {
            const std::size_t first = answer.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                const std::size_t last = answer.find_last_not_of(" \t\r\n");
                std::string trimmed = answer.substr(first, last - first + 1);
                for (char& c : trimmed) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                deleteSource = trimmed == "y" || trimmed == "yes";
            }
        }
    }
    LockEntry entry;
    const int rc = installLocalOne(source, projectRoot, opt, lock, entry);
    if (rc != kExitSuccess) return rc;
    if (!writeLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (opt.json) {
        printInstallJson({entry});
    } else {
        reportInstalled(entry, false);
    }
    if (deleteSource) {
        std::error_code removeEc;
        if (!fs::remove(source, removeEc)) {
            std::fprintf(stderr,
                         "cp3c: warning: could not delete '%s' (%s)\n",
                         deleteCandidate.c_str(), removeEc.message().c_str());
        } else {
            std::fprintf(stderr, "cp3c: deleted '%s'\n",
                         deleteCandidate.c_str());
        }
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
