// =============================================================================
// src/cli/src/pkg/link.cpp - local package link DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/link.hpp.
//
// Directory links are resolved in place (edits are visible to the next build);
// `.cp3pak` links are immutable input, extracted into .cp3/link/<name>/ and
// refreshed when the artifact content changes.
// =============================================================================
#include "detail/link.hpp"

#include <cstdio>
#include <system_error>
#include <vector>

#include "detail/checksum.hpp"
#include "detail/cp3pak.hpp"
#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/semver.hpp"
#include "detail/tar.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

bool looksLikePackageName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos;
}

bool requireManifestString(const JsonValue& manifest, const char* field,
                           std::string& out, std::string& err) {
    const std::string* value = manifest.stringField(field);
    if (value == nullptr || value->empty()) {
        err = std::string("package.json has no \"") + field + "\"";
        return false;
    }
    out = *value;
    return true;
}

bool requireVersion(const std::string& version, std::string& err) {
    if (!parseSemVer(version).valid) {
        err = "package version '" + version + "' is not a valid semver";
        return false;
    }
    return true;
}

std::string readMainText(const fs::path& root, const JsonValue& manifest,
                         std::string& err) {
    const std::string main = packageMain(manifest);
    const fs::path mainPath = root / fs::path(main);
    std::string text;
    if (!readTextFile(mainPath, text)) {
        err = "package entry '" + main + "' cannot be read";
        return {};
    }
    if (text.find("export default") == std::string::npos) {
        err = "package entry '" + main +
              "' has no `export default { ... }` (CLI-README §5.2)";
        return {};
    }
    return text;
}

bool validateDirectoryPackage(const fs::path& root, std::string& name,
                              std::string& version, std::string& err) {
    JsonValue manifest;
    if (!loadPackageJson(root, manifest, err)) return false;
    if (!requireManifestString(manifest, "name", name, err)) return false;
    if (!requireManifestString(manifest, "version", version, err)) return false;
    if (!requireVersion(version, err)) return false;
    if (!looksLikePackageName(name)) {
        err = "package name '" + name + "' is not usable as a link name";
        return false;
    }
    if (readMainText(root, manifest, err).empty()) return false;
    return true;
}

bool validateArchivePackage(const fs::path& archive, std::string& name,
                            std::string& version, std::string& err) {
    Cp3pakManifest pak;
    std::vector<TarEntry> members;
    if (!readCp3pak(archive, pak, members, err)) return false;
    const TarEntry* packageJson = nullptr;
    JsonValue manifest;
    for (const TarEntry& member : members) {
        if (member.name == "package.json") {
            packageJson = &member;
            break;
        }
    }
    if (packageJson == nullptr) {
        err = "archive has no package.json";
        return false;
    }
    if (!JsonValue::parse(packageJson->data, manifest, err)) {
        err = "archive's package.json is invalid: " + err;
        return false;
    }
    if (!requireManifestString(manifest, "name", name, err)) return false;
    if (!requireManifestString(manifest, "version", version, err)) return false;
    if (!requireVersion(version, err)) return false;
    if (!looksLikePackageName(name)) {
        err = "package name '" + name + "' is not usable as a link name";
        return false;
    }
    bool sawIndex = false;
    for (const TarEntry& member : members) {
        if (member.name != "index.cp3") continue;
        sawIndex = member.data.find("export default") != std::string::npos;
    }
    if (!sawIndex) {
        err = "archive has no exported index.cp3 surface; rebuild it with "
              "`cp3c build`";
        return false;
    }
    return true;
}

bool extractMembers(const std::vector<TarEntry>& members, const fs::path& dest,
                    std::string& err) {
    removeTree(dest);
    for (const TarEntry& member : members) {
        const fs::path target = dest / fs::path(member.name);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (!writeTextFile(target, member.data, nullptr)) {
            err = "cannot write " + target.string();
            return false;
        }
    }
    return true;
}

std::string artifactTarget(const std::string& name) {
    return ".cp3/link/" + name;
}

bool linkDirectory(const LinkRequest& request, const fs::path& source,
                   LinkResult& result, std::string& err) {
    std::string name;
    std::string version;
    if (!validateDirectoryPackage(source, name, version, err)) return false;
    const std::string linkName =
        request.nameOverride.empty() ? name : request.nameOverride;
    if (!usableLinkName(linkName, err)) return false;
    if (!request.nameOverride.empty() && request.nameOverride != name) {
        std::fprintf(stderr,
                     "cp3c: warning: package declares name '%s'; linked as "
                     "'%s'\n",
                     name.c_str(), linkName.c_str());
    }
    result.entry = LockEntry{};
    result.entry.name = linkName;
    result.entry.version = version;
    result.entry.source = kSourceLink;
    result.entry.kind = kLinkDirectory;
    result.entry.resolved = source.generic_string();
    result.entry.target = source.generic_string();
    result.reusedCache = false;
    return true;
}

bool linkArtifact(const LinkRequest& request, const fs::path& source,
                  LinkResult& result, std::string& err) {
    std::string name;
    std::string version;
    if (!validateArchivePackage(source, name, version, err)) return false;
    const std::string linkName =
        request.nameOverride.empty() ? name : request.nameOverride;
    if (!usableLinkName(linkName, err)) return false;

    std::string bytes;
    if (!readTextFile(source, bytes)) {
        err = "cannot read " + source.string();
        return false;
    }
    const std::string integrity = "sha256:" + sha256Hex(bytes);
    const fs::path cacheDir = request.projectRoot / ".cp3" / "link" / linkName;
    std::error_code ec;
    const bool sameSource =
        request.existing != nullptr && isLinkEntry(*request.existing) &&
        request.existing->kind == kLinkArtifact &&
        request.existing->resolved == source.generic_string();
    const bool sameArtifact =
        sameSource && request.existing->integrity == integrity;
    const bool cacheExists = fs::is_directory(cacheDir, ec);

    // Re-linking the same artifact refreshes the cache in place; a cache owned
    // by some other link is only replaced on request.
    if (!request.force && cacheExists && !sameSource) {
        err = "link cache " + cacheDir.string() +
              " already exists; pass --force to replace it";
        return false;
    }
    if (sameArtifact && cacheExists) {
        result.reusedCache = true;
    } else {
        Cp3pakManifest pak;
        std::vector<TarEntry> members;
        if (!readCp3pak(source, pak, members, err)) return false;
        if (!extractMembers(members, cacheDir, err)) return false;
        result.reusedCache = false;
    }

    result.entry = LockEntry{};
    result.entry.name = linkName;
    result.entry.version = version;
    result.entry.source = kSourceLink;
    result.entry.kind = kLinkArtifact;
    result.entry.resolved = source.generic_string();
    result.entry.target = artifactTarget(linkName);
    result.entry.integrity = integrity;
    return true;
}

}  // namespace

bool usableLinkName(const std::string& name, std::string& err) {
    if (!looksLikePackageName(name)) {
        err = "package name '" + name + "' is not usable as a link name";
        return false;
    }
    return true;
}

bool linkPackage(const LinkRequest& request, LinkResult& result,
                 std::string& err) {
    std::error_code ec;
    fs::path source = request.source;
    if (!source.is_absolute()) {
        source = fs::absolute(source, ec);
        if (ec) source = request.source;
    }
    if (fs::is_directory(source, ec)) {
        return linkDirectory(request, fs::weakly_canonical(source, ec),
                             result, err);
    }
    if (fs::is_regular_file(source, ec)) {
        return linkArtifact(request, fs::weakly_canonical(source, ec), result,
                            err);
    }
    err = "'" + source.string() +
          "' does not exist (expected a package directory or .cp3pak)";
    return false;
}

void removeLinkCache(const fs::path& projectRoot, const std::string& name) {
    if (!looksLikePackageName(name)) return;
    removeTree(projectRoot / ".cp3" / "link" / name);
}

}  // namespace cp3::cli
