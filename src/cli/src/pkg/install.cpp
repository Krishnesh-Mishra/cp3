// =============================================================================
// src/cli/src/pkg/install.cpp - local package install DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/install.hpp.
//
// Both source forms land in .cp3/packages/<name>/ as plain files (CLI-README
// §4): a `.cp3pak` is decompressed once, a directory is copied once. The lock
// records version + source + a SHA-256 (artifact) or directory digest.
// =============================================================================
#include "detail/install.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>
#include <vector>

#include "cp3/version.hpp"

#include "detail/checksum.hpp"
#include "detail/cp3pak.hpp"
#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/semver.hpp"
#include "detail/tar.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

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

std::string relativeName(const fs::path& base, const fs::path& path) {
    std::error_code ec;
    fs::path rel = fs::relative(path, base, ec);
    if (ec || rel.empty()) rel = path;
    return rel.generic_string();
}

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

bool parseArchivePackageJson(const std::vector<TarEntry>& members,
                             JsonValue& manifest, std::string& err) {
    for (const TarEntry& member : members) {
        if (member.name != "package.json") continue;
        if (!JsonValue::parse(member.data, manifest, err)) {
            err = "archive's package.json is invalid: " + err;
            return false;
        }
        return true;
    }
    err = "archive has no package.json";
    return false;
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

}  // namespace

bool packageSourceExists(const fs::path& source) {
    std::error_code ec;
    return fs::is_directory(source, ec) || fs::is_regular_file(source, ec);
}

bool inspectPackageSource(const fs::path& source, std::string& name,
                          std::string& version, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        JsonValue manifest;
        if (!loadPackageJson(source, manifest, err)) return false;
        name = packageName(manifest, source);
        version = packageVersion(manifest);
        return true;
    }
    if (fs::is_regular_file(source, ec)) {
        Cp3pakManifest pak;
        std::vector<TarEntry> members;
        if (!readCp3pak(source, pak, members, err)) return false;
        JsonValue manifest;
        if (!parseArchivePackageJson(members, manifest, err)) return false;
        const std::string* declared = manifest.stringField("name");
        if (declared == nullptr || declared->empty()) {
            err = "archive's package.json has no \"name\"";
            return false;
        }
        name = *declared;
        version = packageVersion(manifest);
        return true;
    }
    err = "'" + source.string() +
          "' does not exist (expected a .cp3pak file or a package directory)";
    return false;
}

bool installArchivePackage(const fs::path& archive, const fs::path& projectRoot,
                           LockEntry& entry, std::string& err) {
    Cp3pakManifest pak;
    std::vector<TarEntry> members;
    if (!readCp3pak(archive, pak, members, err)) return false;

    JsonValue manifest;
    if (!parseArchivePackageJson(members, manifest, err)) return false;
    const std::string* declared = manifest.stringField("name");
    if (declared == nullptr || declared->empty()) {
        err = "archive's package.json has no \"name\"";
        return false;
    }
    const std::string name = *declared;
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
    entry.version = packageVersion(manifest);
    entry.resolved = "file:" + relativeName(projectRoot, archive);
    entry.integrity = "sha256:" + sha256Hex(bytes);
    warnUnsatisfiedEngines(packageDir, name);
    return true;
}

bool installLocalPackage(const fs::path& source, const fs::path& projectRoot,
                         LockEntry& entry, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        return installFromDirectory(source, projectRoot, entry, err);
    }
    if (fs::is_regular_file(source, ec)) {
        return installArchivePackage(source, projectRoot, entry, err);
    }
    err = "'" + source.string() +
          "' does not exist (expected a .cp3pak file or a package directory)";
    return false;
}

}  // namespace cp3::cli
