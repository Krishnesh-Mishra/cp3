// =============================================================================
// src/cli/src/pkg/install.cpp - local package install DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/install.hpp.
//
// Both source forms land in .cp3/packages/<name>/ as plain files (CLI-README
// §4): a `.cp3pak` is decompressed once, a directory is copied once. The lock
// records version + source + a SHA-256 (artifact) or directory digest.
// =============================================================================
#include "detail/pkg/install.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>
#include <vector>

#include "cp3/meta/version.hpp"

#include "detail/cache/checksum.hpp"
#include "detail/pkg/cp3pak.hpp"
#include "detail/pkg/json.hpp"
#include "detail/pkg/native_pack.hpp"
#include "detail/cache/package_cache.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/core/semver.hpp"
#include "detail/archive/tar.hpp"
#include "detail/core/util.hpp"

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
    const std::string version = packageVersion(manifest);
    // A package that excluded its C++ source is only usable on a platform it
    // ships a cached binary for (CLI-README §5.4).
    PlatformCacheManifest cache;
    const bool hasCache = installedPlatformCache(source, cache);
    if (!checkPackageUsable(name, version, manifest, hasCache, err)) {
        return false;
    }
    fs::path packageDir;
    if (!packageDirFor(projectRoot, name, packageDir, err)) return false;

    removeTree(packageDir);
    if (!copyTree(source, packageDir, err)) return false;
    // The package's `.cp3/native` tree is not part of the installed package
    // copy (`copyTree` skips `.cp3`); it belongs to the consuming project so
    // `import native` resolves it. Relocate the assets under the binding's
    // self-contained `.cp3/native/<id>/` layout.
    if (!installNativeBindingsFromDirectory(source, projectRoot, err)) {
        return false;
    }
    entry.name = name;
    entry.version = packageVersion(manifest);
    entry.resolved = "file:" + relativeName(projectRoot, source);
    entry.integrity = "dir-sha256:" + treeDigest(packageDir);
    warnUnsatisfiedEngines(packageDir, name);
    reportPackageArtifactMode(name, packageDir);
    return true;
}

bool writePackageMember(const fs::path& packageDir, const TarEntry& member,
                        std::string& err) {
    std::string data = member.data;
    // Beta-one archives generated the export surface with a doubled keyword
    // (`export export function ...`) because the AST span already carried the
    // modifier. The generator is fixed, but an already-published archive must
    // keep installing; repair the generated surface on the way in.
    if (member.name == "index.cp3") {
        const std::string doubled = "export export ";
        std::size_t at = 0;
        while ((at = data.find(doubled, at)) != std::string::npos) {
            const bool lineStart =
                at == 0 || data[at - 1] == '\n' || data[at - 1] == '\r';
            if (!lineStart) {
                at += doubled.size();
                continue;
            }
            data.erase(at, std::string("export ").size());
        }
    }
    const fs::path dest = packageDir / fs::path(member.name);
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (!writeTextFile(dest, data, nullptr)) {
        err = "cannot write " + dest.string();
        return false;
    }
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
    const std::string version = packageVersion(manifest);
    // A package that excluded its C++ source is only usable on a platform it
    // ships a cached binary for (CLI-README §5.4).
    const bool hasCache =
        archiveHasPlatformCache(members, currentPlatform());
    if (!checkPackageUsable(name, version, manifest, hasCache, err)) {
        return false;
    }
    fs::path packageDir;
    if (!packageDirFor(projectRoot, name, packageDir, err)) return false;

    removeTree(packageDir);
    // Embedded native assets are installed at the project root, not inside the
    // package directory: the compiler resolves `import native "<id>"` through
    // `<project>/.cp3/native/<id>`.
    if (!installNativeMembers(members, projectRoot, err)) return false;
    for (const TarEntry& member : members) {
        if (isNativeArchiveMember(member.name)) continue;
        if (!writePackageMember(packageDir, member, err)) return false;
    }

    std::string bytes;
    if (!readTextFile(archive, bytes)) {
        err = "cannot re-read " + archive.string() + " for hashing";
        return false;
    }
    entry.name = name;
    entry.version = version;
    entry.resolved = "file:" + relativeName(projectRoot, archive);
    entry.integrity = "sha256:" + sha256Hex(bytes);
    warnUnsatisfiedEngines(packageDir, name);
    reportPackageArtifactMode(name, packageDir);
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
