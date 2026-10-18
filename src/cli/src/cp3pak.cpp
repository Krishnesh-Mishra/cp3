// =============================================================================
// cli/cp3pak.cpp - the `.cp3pak` container DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements private/cp3pak.hpp.
//
// See private/cp3pak.hpp for the format contract. The manifest is written
// first so a reader can validate the container version before doing anything
// with the payload.
// =============================================================================
#include "private/cp3pak.hpp"

#include <string>

#include "private/gzip.hpp"
#include "private/json.hpp"
#include "private/util.hpp"

namespace cp3::cli {

namespace {

constexpr const char* kManifestName = "cp3pak.json";

std::string manifestToJson(const Cp3pakManifest& manifest) {
    std::string out;
    out += "{\n";
    out += "  \"formatVersion\": " +
           std::to_string(manifest.formatVersion) + ",\n";
    out += "  \"name\": " + jsonQuote(manifest.name) + ",\n";
    out += "  \"version\": " + jsonQuote(manifest.version) + ",\n";
    out += "  \"main\": " + jsonQuote(manifest.main) + "\n";
    out += "}\n";
    return out;
}

bool parseManifest(const std::string& text, Cp3pakManifest& manifest,
                   std::string& err) {
    JsonValue doc;
    if (!JsonValue::parse(text, doc, err)) {
        err = std::string("invalid ") + kManifestName + ": " + err;
        return false;
    }
    const JsonValue* version = doc.find("formatVersion");
    if (version == nullptr || version->type != JsonValue::Type::Number) {
        err = std::string(kManifestName) + ": missing integer formatVersion";
        return false;
    }
    manifest.formatVersion = static_cast<int>(version->number);
    if (manifest.formatVersion <= 0 ||
        manifest.formatVersion > kCp3pakFormatVersion) {
        err = "unsupported .cp3pak formatVersion " +
              std::to_string(manifest.formatVersion) + " (this cp3c reads up to " +
              std::to_string(kCp3pakFormatVersion) + ")";
        return false;
    }
    if (const std::string* name = doc.stringField("name")) {
        manifest.name = *name;
    }
    if (const std::string* pkgVersion = doc.stringField("version")) {
        manifest.version = *pkgVersion;
    }
    if (const std::string* main = doc.stringField("main")) {
        manifest.main = *main;
    }
    return true;
}

}  // namespace

bool writeCp3pak(const std::filesystem::path& outPath,
                 const Cp3pakManifest& manifest,
                 const std::vector<TarEntry>& entries, std::string& err) {
    err.clear();
    std::vector<TarEntry> members;
    members.reserve(entries.size() + 1);
    TarEntry header;
    header.name = kManifestName;
    header.data = manifestToJson(manifest);
    members.push_back(std::move(header));
    members.insert(members.end(), entries.begin(), entries.end());

    std::string tarBytes;
    if (!tarWrite(members, tarBytes, err)) return false;
    std::string gzBytes;
    if (!gzipCompress(tarBytes, gzBytes, err)) return false;
    if (!writeTextFile(outPath, gzBytes, nullptr)) {
        err = "cannot write '" + outPath.string() + "'";
        return false;
    }
    return true;
}

bool readCp3pak(const std::filesystem::path& path, Cp3pakManifest& manifest,
                std::vector<TarEntry>& entries, std::string& err) {
    err.clear();
    manifest = Cp3pakManifest{};
    entries.clear();

    std::string bytes;
    if (!readTextFile(path, bytes)) {
        err = "cannot read '" + path.string() + "'";
        return false;
    }
    std::string tarBytes;
    if (!gzipDecompress(bytes, tarBytes, err)) {
        err = "'" + path.string() + "' is not a valid .cp3pak: " + err;
        return false;
    }
    std::vector<TarEntry> all;
    if (!tarRead(tarBytes, all, err)) {
        err = "'" + path.string() + "' is not a valid .cp3pak archive: " + err;
        return false;
    }

    bool sawManifest = false;
    bool sawPackageJson = false;
    for (TarEntry& member : all) {
        if (member.name == kManifestName) {
            sawManifest = true;
            if (!parseManifest(member.data, manifest, err)) return false;
            continue;
        }
        if (member.name == "package.json") sawPackageJson = true;
        entries.push_back(std::move(member));
    }
    if (!sawManifest) {
        err = "not a .cp3pak: no " + std::string(kManifestName) +
              " manifest inside";
        return false;
    }
    if (!sawPackageJson) {
        err = "invalid .cp3pak: no package.json inside";
        return false;
    }
    return true;
}

}  // namespace cp3::cli
