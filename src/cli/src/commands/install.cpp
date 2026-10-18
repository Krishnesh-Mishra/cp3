// =============================================================================
// src/cli/src/commands/install.cpp - `cp3c install [<path-or-url>]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdInstall.
//
// Local sources only in this build (CLI-README §4): a `.cp3pak` archive or an
// unpacked package directory, extracted into `.cp3/packages/<name>/` as plain
// files and recorded in `.cp3/lock.json` (version + source + SHA-256 of the
// artifact). With no argument, every `dependencies` entry of the project's
// package.json is installed. A package whose `engines.cp3` range does not
// match this compiler warns but still installs.
//
// Deferred (documented in the wave report): GitHub repo-URL installs
// (`https://github.com/<user>/<repo>`, interactive selector + --auto) and any
// registry-style ("name": "1.0.0") resolution - both report clearly instead of
// guessing.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "cp3/version.hpp"

#include "detail/checksum.hpp"
#include "detail/commands.hpp"
#include "detail/cp3pak.hpp"
#include "detail/json.hpp"
#include "detail/lockfile.hpp"
#include "detail/package_json.hpp"
#include "detail/semver.hpp"
#include "detail/tar.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

bool isUrl(const std::string& text) {
    return text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
}

bool isGithubUrl(const std::string& text) {
    return text.rfind("https://github.com/", 0) == 0 ||
           text.rfind("http://github.com/", 0) == 0;
}

// "file:./x.cp3pak" -> "./x.cp3pak"; other specs are returned unchanged.
std::string stripFilePrefix(const std::string& spec) {
    if (spec.rfind("file:", 0) == 0) return spec.substr(5);
    return spec;
}

std::string relativeName(const fs::path& base, const fs::path& path) {
    std::error_code ec;
    fs::path rel = fs::relative(path, base, ec);
    if (ec || rel.empty()) rel = path;
    return rel.generic_string();
}

std::string compilerVersion() {
    std::string version = kVersion;
    const std::string stage = kVersionStage;
    if (!stage.empty() && version.find('-') == std::string::npos) {
        version += "-" + stage;
    }
    return version;
}

void warnUnsatisfiedEngines(const fs::path& packageDir, const std::string& name) {
    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(packageDir, manifest, err)) return;
    const JsonValue* engines = manifest.find("engines");
    if (engines == nullptr) return;
    const std::string* range = engines->stringField("cp3");
    if (range == nullptr || range->empty()) return;
    const SemVer compiler = parseSemVer(compilerVersion());
    if (semverSatisfies(compiler, *range)) return;
    std::fprintf(stderr,
                 "cp3c: warning: '%s' declares engines.cp3 \"%s\", but this is "
                 "cp3c %s\n",
                 name.c_str(), range->c_str(), compilerVersion().c_str());
}

void upsertLockEntry(std::vector<LockEntry>& lock, const LockEntry& entry) {
    for (LockEntry& existing : lock) {
        if (existing.name == entry.name) {
            existing = entry;
            return;
        }
    }
    lock.push_back(entry);
}

// -----------------------------------------------------------------------------
// Directory copies (unpacked package installs)
// -----------------------------------------------------------------------------

bool skipCopyEntry(const fs::path& relative) {
    if (relative.empty()) return true;
    const std::string first = relative.begin()->string();
    return first == ".cp3" || first == ".git" || first == ".bin";
}

bool copyTree(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) {
        err = "cannot create " + to.string() + " (" + ec.message() + ")";
        return false;
    }
    for (fs::recursive_directory_iterator it(from, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            err = "cannot scan " + from.string() + " (" + ec.message() + ")";
            return false;
        }
        const fs::path rel = fs::relative(it->path(), from, ec);
        if (ec || skipCopyEntry(rel)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        const fs::path dest = to / rel;
        ec.clear();
        if (it->is_directory(ec)) {
            fs::create_directories(dest, ec);
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(dest.parent_path(), ec);
            if (!ec) {
                fs::copy_file(it->path(), dest,
                              fs::copy_options::overwrite_existing, ec);
            }
        }
        if (ec) {
            err = "cannot copy " + it->path().string() + " (" + ec.message() + ")";
            return false;
        }
    }
    return true;
}

// Deterministic digest of a package directory: SHA-256 over the sorted
// "relative-path content-hash" lines. Distinguishes a directory install from a
// single-artifact one in the lock ("dir-sha256:" vs "sha256:").
std::string treeDigest(const fs::path& dir) {
    std::vector<std::string> lines;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), dir, ec);
        if (ec || skipCopyEntry(rel)) continue;
        std::string data;
        if (!readTextFile(it->path(), data)) continue;
        lines.push_back(rel.generic_string() + " " + sha256Hex(data));
    }
    std::sort(lines.begin(), lines.end());
    std::string joined;
    for (const std::string& line : lines) {
        joined += line;
        joined += "\n";
    }
    return sha256Hex(joined);
}

// -----------------------------------------------------------------------------
// The two source forms
// -----------------------------------------------------------------------------

bool packageDirFor(const fs::path& projectRoot, const std::string& name,
                   fs::path& out, std::string& err) {
    if (name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos || name == "." || name == "..") {
        err = "package name '" + name + "' is not usable as a directory name";
        return false;
    }
    out = projectRoot / ".cp3" / "packages" / name;
    return true;
}

bool installFromArchive(const fs::path& archive, const fs::path& projectRoot,
                        LockEntry& entry, std::string& err) {
    Cp3pakManifest manifest;
    std::vector<TarEntry> members;
    if (!readCp3pak(archive, manifest, members, err)) return false;

    JsonValue packageManifest;
    const TarEntry* packageJson = nullptr;
    for (const TarEntry& member : members) {
        if (member.name == "package.json") packageJson = &member;
    }
    if (packageJson == nullptr) {
        err = "archive has no package.json";
        return false;
    }
    if (!JsonValue::parse(packageJson->data, packageManifest, err)) {
        err = "archive's package.json is invalid: " + err;
        return false;
    }
    const std::string* declaredName = packageManifest.stringField("name");
    if (declaredName == nullptr || declaredName->empty()) {
        err = "archive's package.json has no \"name\"";
        return false;
    }
    const std::string name = *declaredName;
    fs::path packageDir;
    if (!packageDirFor(projectRoot, name, packageDir, err)) return false;

    removeTree(packageDir);
    for (const TarEntry& member : members) {
        const fs::path dest = packageDir / fs::path(member.name);
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        if (!writeTextFile(dest, member.data, nullptr)) {
            err = "cannot write " + dest.string();
            return false;
        }
    }

    std::string bytes;
    if (!readTextFile(archive, bytes)) {
        err = "cannot re-read " + archive.string() + " for hashing";
        return false;
    }
    entry.name = name;
    entry.version = packageVersion(packageManifest);
    entry.resolved = "file:" + relativeName(projectRoot, archive);
    entry.integrity = "sha256:" + sha256Hex(bytes);
    warnUnsatisfiedEngines(packageDir, name);
    return true;
}

bool installFromDirectory(const fs::path& source, const fs::path& projectRoot,
                          LockEntry& entry, std::string& err) {
    JsonValue manifest;
    if (!loadPackageJson(source, manifest, err)) return false;
    const std::string name = packageName(manifest, source);
    fs::path packageDir;
    if (!packageDirFor(projectRoot, name, packageDir, err)) return false;

    removeTree(packageDir);
    if (!copyTree(source, packageDir, err)) return false;
    entry.name = name;
    entry.version = packageVersion(manifest);
    entry.resolved = "file:" + relativeName(projectRoot, source);
    entry.integrity = "dir-sha256:" + treeDigest(packageDir);
    warnUnsatisfiedEngines(packageDir, name);
    return true;
}

bool installLocal(const fs::path& source, const fs::path& projectRoot,
                  LockEntry& entry, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        return installFromDirectory(source, projectRoot, entry, err);
    }
    if (fs::is_regular_file(source, ec)) {
        return installFromArchive(source, projectRoot, entry, err);
    }
    err = "'" + source.string() +
          "' does not exist (expected a .cp3pak file or a package directory)";
    return false;
}

// -----------------------------------------------------------------------------
// Dependency specs (bare `cp3c install`)
// -----------------------------------------------------------------------------

enum class DepKind { Local, Github, Registry, Malformed };

DepKind classifyDependency(const std::string& spec, fs::path& resolved) {
    if (spec.empty()) return DepKind::Malformed;
    if (isGithubUrl(spec)) return DepKind::Github;
    if (isUrl(spec)) return DepKind::Malformed;
    std::string path = stripFilePrefix(spec);
    const bool prefixed = path != spec;
    const bool pathish = prefixed || path.rfind("./", 0) == 0 ||
                         path.rfind("../", 0) == 0 || path.rfind(".\\", 0) == 0 ||
                         path.rfind("..\\", 0) == 0 || path.rfind("/", 0) == 0 ||
                         (path.size() >= 2 && path[1] == ':');
    if (pathish) {
        resolved = path;
        return DepKind::Local;
    }
    return DepKind::Registry;
}

// Installs one `dependencies` entry and records it in `lock`; false counts as
// a failed dependency (the reason is already on stderr).
bool installDependency(const std::string& name, const std::string& spec,
                       const fs::path& projectRoot,
                       std::vector<LockEntry>& lock) {
    fs::path resolved;
    const DepKind kind = classifyDependency(spec, resolved);
    if (kind == DepKind::Github) {
        std::fprintf(stderr,
                     "cp3c: cannot install '%s': GitHub repo installs are "
                     "not implemented in this build (use a local .cp3pak)\n",
                     name.c_str());
        return false;
    }
    if (kind != DepKind::Local) {
        std::fprintf(stderr,
                     "cp3c: cannot install '%s' from '%s': registry lookups "
                     "are not implemented in this build (use a file: path "
                     "or a local .cp3pak)\n",
                     name.c_str(), spec.c_str());
        return false;
    }
    LockEntry entry;
    std::string installErr;
    const fs::path source =
        resolved.is_absolute() ? resolved : projectRoot / resolved;
    if (!installLocal(source, projectRoot, entry, installErr)) {
        std::fprintf(stderr, "cp3c: cannot install '%s': %s\n", name.c_str(),
                     installErr.c_str());
        return false;
    }
    if (entry.name != name) {
        std::fprintf(stderr,
                     "cp3c: warning: dependency '%s' installs package '%s'\n",
                     name.c_str(), entry.name.c_str());
    }
    upsertLockEntry(lock, entry);
    std::fprintf(stderr, "cp3c: installed %s %s -> .cp3/packages/%s\n",
                 entry.name.c_str(), entry.version.c_str(), entry.name.c_str());
    return true;
}

int installDependencies(const fs::path& projectRoot,
                        std::vector<LockEntry>& lock) {
    JsonValue manifest;
    std::string err;
    if (!loadPackageJson(projectRoot, manifest, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    const JsonValue* deps = manifest.find("dependencies");
    if (deps == nullptr || !deps->isObject() || deps->object.empty()) {
        std::fprintf(stderr, "cp3c: nothing to install (no dependencies in %s)\n",
                     (projectRoot / "package.json").string().c_str());
        return kExitSuccess;
    }

    int installed = 0;
    int failures = 0;
    for (const auto& dep : deps->object) {
        if (dep.second.type != JsonValue::Type::String) {
            std::fprintf(stderr,
                         "cp3c: cannot install '%s': dependency values must be "
                         "a file: path or a GitHub URL in this build\n",
                         dep.first.c_str());
            ++failures;
            continue;
        }
        if (installDependency(dep.first, dep.second.string, projectRoot, lock)) {
            ++installed;
        } else {
            ++failures;
        }
    }
    if (!writeLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    if (failures > 0) {
        std::fprintf(stderr, "cp3c: %d of %zu dependencies could not be installed\n",
                     failures, deps->object.size());
        return kExitCompileError;
    }
    if (installed == 1) {
        std::fprintf(stderr, "cp3c: 1 package installed\n");
    } else {
        std::fprintf(stderr, "cp3c: %d packages installed\n", installed);
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

    if (opt.positionals.empty()) {
        return installDependencies(projectRoot, lock);
    }

    const std::string spec = opt.positionals[0];
    if (isGithubUrl(spec)) {
        std::fprintf(stderr,
                     "cp3c: GitHub repo installs (interactive selector, --auto) "
                     "are not implemented in this build; install a local "
                     ".cp3pak instead\n");
        return notImplemented("install from GitHub URL");
    }
    if (isUrl(spec)) {
        std::fprintf(stderr,
                     "cp3c: only https://github.com/<user>/<repo> URLs are "
                     "recognized, and GitHub installs are not implemented in "
                     "this build\n");
        return kExitUsageError;
    }

    const std::string path = stripFilePrefix(spec);
    const fs::path source = fs::path(path).is_absolute() ? fs::path(path)
                                                         : cwd / fs::path(path);
    LockEntry entry;
    if (!installLocal(source, projectRoot, entry, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    upsertLockEntry(lock, entry);
    if (!writeLockfile(projectRoot, lock, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    std::fprintf(stderr, "cp3c: installed %s %s -> .cp3/packages/%s\n",
                 entry.name.c_str(), entry.version.c_str(), entry.name.c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
